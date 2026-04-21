#include "espnow_link.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "mainserver.h"

namespace
{
constexpr uint8_t kPacketTypeTelemetry = 1;
constexpr uint8_t kPacketTypeCommand = 2;
constexpr TickType_t kEspNowLoopDelay = pdMS_TO_TICKS(2000);
constexpr uint32_t kRemoteOfflineAfterMs = 8000;

AppContext *g_ctx = nullptr;
uint8_t g_peerMac[6] = {0};
bool g_peerRegistered = false;
uint32_t g_sequence = 0;

struct __attribute__((packed)) EspNowPacket
{
    uint8_t packetType;
    uint8_t doorOpen;
    uint8_t fanOn;
    uint8_t fanSpeed;
    uint8_t rgbRed;
    uint8_t rgbGreen;
    uint8_t rgbBlue;
    uint8_t rgbOn;
    uint8_t commandType;
    uint8_t commandValue;
    float temperature;
    float humidity;
    uint32_t sequence;
};

bool parseMacAddress(const String &macText, uint8_t mac[6])
{
    unsigned int values[6] = {0};
    if (sscanf(macText.c_str(), "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) != 6)
    {
        return false;
    }

    for (int index = 0; index < 6; ++index)
    {
        mac[index] = static_cast<uint8_t>(values[index]);
    }
    return true;
}

String formatMacAddress(const uint8_t mac[6])
{
    char buffer[18];
    snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buffer);
}

bool refreshPeerRegistration()
{
    if (g_ctx == nullptr || g_ctx->configMutex == nullptr)
        return false;

    String peerMacText;
    if (xSemaphoreTake(g_ctx->configMutex, portMAX_DELAY) == pdTRUE)
    {
        peerMacText = g_ctx->peerMac;
        xSemaphoreGive(g_ctx->configMutex);
    }

    if (peerMacText.isEmpty())
    {
        g_peerRegistered = false;
        if (g_ctx->stateMutex != nullptr && xSemaphoreTake(g_ctx->stateMutex, portMAX_DELAY) == pdTRUE)
        {
            g_ctx->espNowPeerConfigured = false;
            g_ctx->espNowStatus = "Peer MAC not configured.";
            xSemaphoreGive(g_ctx->stateMutex);
        }
        return false;
    }

    uint8_t parsedMac[6] = {0};
    if (!parseMacAddress(peerMacText, parsedMac))
    {
        if (g_ctx->stateMutex != nullptr && xSemaphoreTake(g_ctx->stateMutex, portMAX_DELAY) == pdTRUE)
        {
            g_ctx->espNowPeerConfigured = false;
            g_ctx->espNowStatus = "Peer MAC format invalid.";
            xSemaphoreGive(g_ctx->stateMutex);
        }
        return false;
    }

    if (g_peerRegistered && memcmp(parsedMac, g_peerMac, sizeof(g_peerMac)) == 0)
        return true;

    memcpy(g_peerMac, parsedMac, sizeof(g_peerMac));
    if (esp_now_is_peer_exist(g_peerMac))
    {
        g_peerRegistered = true;
    }
    else
    {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, g_peerMac, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        g_peerRegistered = esp_now_add_peer(&peerInfo) == ESP_OK;
    }

    if (g_ctx->stateMutex != nullptr && xSemaphoreTake(g_ctx->stateMutex, portMAX_DELAY) == pdTRUE)
    {
        g_ctx->espNowPeerConfigured = g_peerRegistered;
        g_ctx->espNowStatus = g_peerRegistered ? "Peer ready for ESP-NOW." : "Failed to register peer.";
        xSemaphoreGive(g_ctx->stateMutex);
    }

    return g_peerRegistered;
}

bool sendPacket(const EspNowPacket &packet)
{
    if (!refreshPeerRegistration())
        return false;

    return esp_now_send(g_peerMac, reinterpret_cast<const uint8_t *>(&packet), sizeof(packet)) == ESP_OK;
}

void updateRemoteBoardState(const EspNowPacket &packet)
{
    if (g_ctx == nullptr || g_ctx->stateMutex == nullptr)
        return;

    if (xSemaphoreTake(g_ctx->stateMutex, portMAX_DELAY) == pdTRUE)
    {
        g_ctx->remoteTemperature = packet.temperature;
        g_ctx->remoteHumidity = packet.humidity;
        g_ctx->remoteDoorOpen = packet.doorOpen != 0;
        g_ctx->remoteFanOn = packet.fanOn != 0;
        g_ctx->remoteFanSpeed = packet.fanSpeed;
        g_ctx->remoteRgbRed = packet.rgbRed;
        g_ctx->remoteRgbGreen = packet.rgbGreen;
        g_ctx->remoteRgbBlue = packet.rgbBlue;
        g_ctx->remoteRgbOn = packet.rgbOn != 0;
        char hexBuffer[8];
        snprintf(hexBuffer, sizeof(hexBuffer), "#%02X%02X%02X", packet.rgbRed, packet.rgbGreen, packet.rgbBlue);
        g_ctx->remoteRgbHexText = hexBuffer;
        g_ctx->remoteOnline = true;
        g_ctx->remoteLastSeenMs = millis();
        g_ctx->espNowPacketsRx += 1;
        g_ctx->espNowStatus = "Telemetry received from remote board.";
        xSemaphoreGive(g_ctx->stateMutex);
    }
}

void applyRemoteCommand(const EspNowPacket &packet)
{
    if (g_ctx == nullptr)
        return;

    switch (static_cast<RemoteCommandType>(packet.commandType))
    {
        case REMOTE_CMD_RELAY1:
            local_set_relay(g_ctx, 1, packet.commandValue != 0);
            break;
        case REMOTE_CMD_RELAY2:
            local_set_relay(g_ctx, 2, packet.commandValue != 0);
            break;
        case REMOTE_CMD_DOOR:
            local_set_door(g_ctx, packet.commandValue != 0);
            break;
        case REMOTE_CMD_FAN_POWER:
        {
            uint8_t currentSpeed = 100;
            if (g_ctx->stateMutex != nullptr && xSemaphoreTake(g_ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                currentSpeed = g_ctx->fanSpeed == 0 ? 100 : g_ctx->fanSpeed;
                xSemaphoreGive(g_ctx->stateMutex);
            }
            local_set_fan(g_ctx, packet.commandValue != 0, currentSpeed);
            break;
        }
        case REMOTE_CMD_FAN_SPEED:
            local_set_fan(g_ctx, packet.commandValue > 0, packet.commandValue);
            break;
        case REMOTE_CMD_RGB:
            local_set_rgb(g_ctx, packet.rgbRed, packet.rgbGreen, packet.rgbBlue);
            break;
        case REMOTE_CMD_NONE:
        default:
            break;
    }
}

void onDataSent(const uint8_t *, esp_now_send_status_t status)
{
    if (g_ctx == nullptr || g_ctx->stateMutex == nullptr)
        return;

    if (xSemaphoreTake(g_ctx->stateMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        if (status == ESP_NOW_SEND_SUCCESS)
        {
            g_ctx->espNowPacketsTx += 1;
            g_ctx->espNowStatus = "ESP-NOW packet delivered.";
        }
        else
        {
            g_ctx->espNowStatus = "ESP-NOW send failed.";
        }
        xSemaphoreGive(g_ctx->stateMutex);
    }
}

void onDataRecv(const uint8_t *, const uint8_t *incomingData, int dataLen)
{
    if (incomingData == nullptr || dataLen != static_cast<int>(sizeof(EspNowPacket)))
        return;

    EspNowPacket packet = {};
    memcpy(&packet, incomingData, sizeof(packet));

    if (packet.packetType == kPacketTypeTelemetry)
    {
        updateRemoteBoardState(packet);
    }
    else if (packet.packetType == kPacketTypeCommand)
    {
        applyRemoteCommand(packet);
    }
}

bool initEspNow(AppContext *ctx)
{
    g_ctx = ctx;

    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect(false, false);

    if (esp_now_init() != ESP_OK)
    {
        if (ctx != nullptr && ctx->stateMutex != nullptr && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
        {
            ctx->espNowReady = false;
            ctx->espNowStatus = "esp_now_init failed.";
            xSemaphoreGive(ctx->stateMutex);
        }
        return false;
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    uint8_t localMac[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_STA, localMac) == ESP_OK)
    {
        const String localMacText = formatMacAddress(localMac);
        if (ctx != nullptr && ctx->configMutex != nullptr && xSemaphoreTake(ctx->configMutex, portMAX_DELAY) == pdTRUE)
        {
            ctx->localMac = localMacText;
            xSemaphoreGive(ctx->configMutex);
        }
    }

    if (ctx != nullptr && ctx->stateMutex != nullptr && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
    {
        ctx->espNowReady = true;
        ctx->espNowStatus = "ESP-NOW initialized.";
        xSemaphoreGive(ctx->stateMutex);
    }

    return true;
}

void sendLocalTelemetry()
{
    if (g_ctx == nullptr || g_ctx->stateMutex == nullptr)
        return;

    EspNowPacket packet = {};
    packet.packetType = kPacketTypeTelemetry;

    if (xSemaphoreTake(g_ctx->stateMutex, portMAX_DELAY) == pdTRUE)
    {
        packet.doorOpen = g_ctx->doorOpen ? 1 : 0;
        packet.fanOn = g_ctx->fanOn ? 1 : 0;
        packet.fanSpeed = g_ctx->fanSpeed;
        packet.rgbRed = g_ctx->rgbRed;
        packet.rgbGreen = g_ctx->rgbGreen;
        packet.rgbBlue = g_ctx->rgbBlue;
        packet.rgbOn = g_ctx->rgbLedOn ? 1 : 0;
        packet.temperature = g_ctx->temperature;
        packet.humidity = g_ctx->humidity;
        packet.sequence = ++g_sequence;
        xSemaphoreGive(g_ctx->stateMutex);
    }

    sendPacket(packet);
}
}

bool espnow_send_remote_command(AppContext *ctx, RemoteCommandType commandType, uint8_t value)
{
    g_ctx = ctx;

    EspNowPacket packet = {};
    packet.packetType = kPacketTypeCommand;
    packet.commandType = static_cast<uint8_t>(commandType);
    packet.commandValue = value;
    packet.sequence = ++g_sequence;

    if (!sendPacket(packet))
    {
        if (ctx != nullptr && ctx->stateMutex != nullptr && xSemaphoreTake(ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            ctx->espNowStatus = "Cannot send command. Peer not ready.";
            xSemaphoreGive(ctx->stateMutex);
        }
        return false;
    }

    return true;
}

bool espnow_send_remote_rgb(AppContext *ctx, uint8_t red, uint8_t green, uint8_t blue)
{
    g_ctx = ctx;

    EspNowPacket packet = {};
    packet.packetType = kPacketTypeCommand;
    packet.commandType = static_cast<uint8_t>(REMOTE_CMD_RGB);
    packet.commandValue = 1;
    packet.rgbRed = red;
    packet.rgbGreen = green;
    packet.rgbBlue = blue;
    packet.rgbOn = (red == 0 && green == 0 && blue == 0) ? 0 : 1;
    packet.sequence = ++g_sequence;

    if (!sendPacket(packet))
    {
        if (ctx != nullptr && ctx->stateMutex != nullptr && xSemaphoreTake(ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            ctx->espNowStatus = "Cannot send RGB command. Peer not ready.";
            xSemaphoreGive(ctx->stateMutex);
        }
        return false;
    }

    return true;
}

void espnow_link_task(void *pvParameters)
{
    AppContext *ctx = static_cast<AppContext *>(pvParameters);
    if (!initEspNow(ctx))
    {
        vTaskDelete(NULL);
        return;
    }

    while (1)
    {
        refreshPeerRegistration();
        sendLocalTelemetry();

        if (ctx != nullptr && ctx->stateMutex != nullptr && xSemaphoreTake(ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            if (ctx->remoteLastSeenMs > 0)
            {
                ctx->remoteOnline = millis() - ctx->remoteLastSeenMs <= kRemoteOfflineAfterMs;
            }
            else
            {
                ctx->remoteOnline = false;
            }
            xSemaphoreGive(ctx->stateMutex);
        }

        vTaskDelay(kEspNowLoopDelay);
    }
}
