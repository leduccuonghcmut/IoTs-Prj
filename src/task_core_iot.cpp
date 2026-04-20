#include "task_core_iot.h"
#include "global.h"

constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;

WiFiClient wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);

constexpr char LED_STATE_ATTR[] = "ledState";

volatile int ledMode = 0;
volatile bool ledState = false;

constexpr uint16_t BLINKING_INTERVAL_MS_MIN = 10U;
constexpr uint16_t BLINKING_INTERVAL_MS_MAX = 60000U;
volatile uint16_t blinkingInterval = 1000U;

constexpr std::array<const char *, 1U> SHARED_ATTRIBUTES_LIST = {
    LED_STATE_ATTR,
};

void processSharedAttributes(const Shared_Attribute_Data &data)
{
    for (auto it = data.begin(); it != data.end(); ++it)
    {
        if (strcmp(it->key().c_str(), LED_STATE_ATTR) == 0)
        {
            ledState = it->value().as<bool>();
            Serial.print("LED state is set to: ");
            Serial.println(ledState);
        }
    }
}

RPC_Response setLedSwitchValue(const RPC_Data &data)
{
    Serial.println("Received Switch state");
    bool newState = data;
    ledState = newState;
    Serial.print("Switch state change: ");
    Serial.println(newState);
    return RPC_Response("setLedSwitchValue", newState);
}

const std::array<RPC_Callback, 1U> callbacks = {
    RPC_Callback{"setLedSwitchValue", setLedSwitchValue}
};

const Shared_Attribute_Callback attributes_callback(
    &processSharedAttributes,
    SHARED_ATTRIBUTES_LIST.cbegin(),
    SHARED_ATTRIBUTES_LIST.cend()
);

const Attribute_Request_Callback attribute_shared_request_callback(
    &processSharedAttributes,
    SHARED_ATTRIBUTES_LIST.cbegin(),
    SHARED_ATTRIBUTES_LIST.cend()
);

bool CORE_IOT_reconnect()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return false;
    }

    if (tb.connected())
    {
        return true;
    }

    Serial.println("[COREIOT] Connecting...");
    Serial.print("[COREIOT] Server: ");
    Serial.println(CORE_IOT_SERVER);
    Serial.print("[COREIOT] Port: ");
    Serial.println(CORE_IOT_PORT);
    Serial.print("[COREIOT] Token: ");
    Serial.println(CORE_IOT_TOKEN);

    if (!tb.connect(CORE_IOT_SERVER.c_str(), CORE_IOT_TOKEN.c_str(), CORE_IOT_PORT.toInt()))
    {
        Serial.println("[COREIOT] Connect failed");
        return false;
    }

    tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());
    tb.sendAttributeData("localIp", WiFi.localIP().toString().c_str());
    tb.sendAttributeData("ssid", WiFi.SSID().c_str());

    if (!tb.RPC_Subscribe(callbacks.cbegin(), callbacks.cend()))
    {
        Serial.println("[COREIOT] RPC subscribe failed");
    }

    if (!tb.Shared_Attributes_Subscribe(attributes_callback))
    {
        Serial.println("[COREIOT] Shared attribute subscribe failed");
    }

    if (!tb.Shared_Attributes_Request(attribute_shared_request_callback))
    {
        Serial.println("[COREIOT] Shared attribute request failed");
    }

    Serial.println("[COREIOT] Connected");
    return true;
}

void coreiot_thingsboard_task(void *pvParameters)
{
    while (1)
    {
        if (xBinarySemaphoreInternet != NULL)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                CORE_IOT_reconnect();

                if (tb.connected())
                {
                    tb.loop();

                    tb.sendTelemetryData("temperature", glob_temperature);
                    tb.sendTelemetryData("humidity", glob_humidity);

                    tb.sendAttributeData("rssi", WiFi.RSSI());
                    tb.sendAttributeData("channel", WiFi.channel());
                    tb.sendAttributeData("bssid", WiFi.BSSIDstr().c_str());
                    tb.sendAttributeData("localIp", WiFi.localIP().toString().c_str());
                    tb.sendAttributeData("ssid", WiFi.SSID().c_str());

                    Serial.print("[COREIOT] temperature = ");
                    Serial.print(glob_temperature);
                    Serial.print(" | humidity = ");
                    Serial.println(glob_humidity);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}