#include "task_core_iot.h"
#include "global.h"

constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;

static const char *tinyMLStateToString(TinyMLState state)
{
    switch (state)
    {
    case TINYML_NORMAL:
        return "NORMAL";
    case TINYML_WARNING:
        return "WARNING";
    case TINYML_ANOMALY:
        return "ANOMALY";
    case TINYML_IDLE:
    default:
        return "IDLE";
    }
}

bool CORE_IOT_reconnect()
{
    return false;
}

void CORE_IOT_sendata(String mode, String feed, String data)
{
    (void)mode;
    (void)feed;
    (void)data;
}

void coreiot_thingsboard_task(void *pvParameters)
{
    AppContext *ctx = static_cast<AppContext *>(pvParameters);
    WiFiClient wifiClient;
    Arduino_MQTT_Client mqttClient(wifiClient);
    ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);

    while (1)
    {
        if (ctx != NULL && ctx->internetSemaphore != NULL && WiFi.status() == WL_CONNECTED)
        {
            String server;
            String token;
            String port;
            float temperature = 0.0f;
            float humidity = 0.0f;
            float tinymlScore = 0.0f;
            TinyMLState tinymlState = TINYML_IDLE;

            if (ctx->configMutex != NULL && xSemaphoreTake(ctx->configMutex, portMAX_DELAY) == pdTRUE)
            {
                server = ctx->coreIotServer;
                token = ctx->coreIotToken;
                port = ctx->coreIotPort;
                xSemaphoreGive(ctx->configMutex);
            }

            if (ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
            {
                temperature = ctx->temperature;
                humidity = ctx->humidity;
                tinymlScore = ctx->tinymlScore;
                tinymlState = ctx->tinymlState;
                xSemaphoreGive(ctx->stateMutex);
            }

            if (!tb.connected())
            {
                if (!tb.connect(server.c_str(), token.c_str(), port.toInt()))
                {
                    Serial.println("[COREIOT] Connect failed");
                    vTaskDelay(pdMS_TO_TICKS(10000));
                    continue;
                }

                tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());
                tb.sendAttributeData("localIp", WiFi.localIP().toString().c_str());
                tb.sendAttributeData("ssid", WiFi.SSID().c_str());
            }

            tb.loop();
            tb.sendTelemetryData("temperature", temperature);
            tb.sendTelemetryData("humidity", humidity);
            tb.sendTelemetryData("tinyml_score", tinymlScore);
            tb.sendAttributeData("tinyml_state", tinyMLStateToString(tinymlState));
            tb.sendAttributeData("rssi", WiFi.RSSI());
            tb.sendAttributeData("channel", WiFi.channel());
            tb.sendAttributeData("bssid", WiFi.BSSIDstr().c_str());
            tb.sendAttributeData("localIp", WiFi.localIP().toString().c_str());
            tb.sendAttributeData("ssid", WiFi.SSID().c_str());
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
