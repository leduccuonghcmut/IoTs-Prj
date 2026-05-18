#include "task_core_iot.h"

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <math.h>

#include "mainserver.h"

namespace
{
constexpr uint32_t kReconnectDelayMs = 5000U;
constexpr uint32_t kPublishIntervalMs = 10000U;
constexpr float kHighTempThreshold = 50.0f;
constexpr float kLowTempThreshold = 30.0f;
constexpr size_t kMqttBufferSize = 1024U;
constexpr uint8_t kRuleRelayIndex = 1U;
constexpr uint8_t kLedWhiteValue = 255U;

constexpr const char *kTelemetryTopic = "v1/devices/me/telemetry";
constexpr const char *kAttributesTopic = "v1/devices/me/attributes";
constexpr const char *kRpcRequestTopic = "v1/devices/me/rpc/request/+";

WiFiClient g_wifiClient;
PubSubClient g_mqttClient(g_wifiClient);
AppContext *g_ctx = nullptr;
bool g_lastAutoRelayState = false;
bool g_lastAutoRelayStateValid = false;
unsigned long g_lastReconnectAttemptMs = 0;
unsigned long g_lastPublishMs = 0;

bool lockWithTimeout(SemaphoreHandle_t mutex, TickType_t timeout)
{
    return mutex != NULL && xSemaphoreTake(mutex, timeout) == pdTRUE;
}

const char *tinyMLStateToString(TinyMLState state)
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

String buildClientId()
{
    String clientId = "ESP32-";
    clientId += WiFi.macAddress();
    clientId.replace(":", "");
    return clientId;
}

String makeRpcResponseTopic(const String &requestId)
{
    return String("v1/devices/me/rpc/response/") + requestId;
}

void publishJson(const char *topic, const JsonDocument &doc, bool retained = false)
{
    if (!g_mqttClient.connected() || topic == nullptr)
        return;

    String payload;
    serializeJson(doc, payload);
    g_mqttClient.publish(topic, payload.c_str(), retained);
}

void publishStatusAttribute(const char *reason, bool powerOn, float temperature, float humidity)
{
    DynamicJsonDocument doc(256);
    doc["POWER"] = powerOn ? "ON" : "OFF";
    doc["rule_reason"] = reason != nullptr ? reason : "unknown";
    doc["rule_temperature"] = temperature;
    doc["rule_humidity"] = humidity;
    doc["device"] = "SMART_PLUG_003";
    publishJson(kAttributesTopic, doc);
}

void publishRuntimeSnapshot(AppContext *ctx, float temperature, float humidity)
{
    bool relay1On = false;
    bool relay2On = false;
    bool doorOpen = false;
    bool fanOn = false;
    bool rgbOn = false;
    float tinymlScore = 0.0f;
    float tinymlProbNormal = 0.0f;
    float tinymlProbThreshold = 0.0f;
    float tinymlProbSpike = 0.0f;
    TinyMLState tinymlState = TINYML_IDLE;
    float mnistConfidence = 0.0f;
    int mnistDigit = -1;
    uint8_t fanSpeed = 0;
    uint8_t rgbRed = 0;
    uint8_t rgbGreen = 0;
    uint8_t rgbBlue = 0;
    String localMac; 

    if (ctx != NULL && lockWithTimeout(ctx->stateMutex, pdMS_TO_TICKS(100)))
    {
        relay1On = ctx->relay1On;
        relay2On = ctx->relay2On;
        doorOpen = ctx->doorOpen;
        fanOn = ctx->fanOn;
        rgbOn = ctx->rgbLedOn;
        tinymlScore = ctx->tinymlScore;
        tinymlProbNormal = ctx->tinymlProbNormal;
        tinymlProbThreshold = ctx->tinymlProbThreshold;
        tinymlProbSpike = ctx->tinymlProbSpike;
        tinymlState = ctx->tinymlState;
        mnistConfidence = ctx->mnistConfidence;
        mnistDigit = ctx->mnistDigit;
        fanSpeed = ctx->fanSpeed;
        rgbRed = ctx->rgbRed;
        rgbGreen = ctx->rgbGreen;
        rgbBlue = ctx->rgbBlue;
        xSemaphoreGive(ctx->stateMutex);
    }

    if (ctx != NULL && lockWithTimeout(ctx->configMutex, pdMS_TO_TICKS(100)))
    {
        localMac = ctx->localMac;
        xSemaphoreGive(ctx->configMutex);
    }

    if (localMac.isEmpty())
        localMac = WiFi.macAddress();

    DynamicJsonDocument telemetry(512);
    telemetry["temperature"] = temperature;
    telemetry["humidity"] = humidity;
    telemetry["tinyml_score"] = tinymlScore;
    telemetry["tinyml_prob_normal"] = tinymlProbNormal;
    telemetry["tinyml_prob_threshold"] = tinymlProbThreshold;
    telemetry["tinyml_prob_spike"] = tinymlProbSpike;
    telemetry["tinyml_state"] = tinyMLStateToString(tinymlState);
    telemetry["mnist_confidence"] = mnistConfidence;
    telemetry["mnist_digit"] = mnistDigit;
    telemetry["relay1"] = relay1On;
    telemetry["relay2"] = relay2On;
    telemetry["led_state"] = rgbOn;
    telemetry["door"] = doorOpen ? "open" : "closed";
    telemetry["door_open"] = doorOpen;
    telemetry["fan"] = fanOn ? "ON" : "OFF";
    telemetry["fan_state"] = fanOn;
    telemetry["fan_speed"] = fanSpeed;
    telemetry["rgb_on"] = rgbOn;
    telemetry["rgb_red"] = rgbRed;
    telemetry["rgb_green"] = rgbGreen;
    telemetry["rgb_blue"] = rgbBlue;
    telemetry["rgb_r"] = rgbRed;
    telemetry["rgb_g"] = rgbGreen;
    telemetry["rgb_b"] = rgbBlue;
    telemetry["macAddress"] = localMac;
    telemetry["localIp"] = WiFi.localIP().toString();
    telemetry["ssid"] = WiFi.SSID();
    telemetry["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    publishJson(kTelemetryTopic, telemetry);
}

bool parseBoolLike(const JsonVariantConst &value, bool &result)
{
    if (value.is<bool>())
    {
        result = value.as<bool>();
        return true;
    }

    if (value.is<int>())
    {
        result = value.as<int>() != 0;
        return true;
    }

    if (value.is<float>())
    {
        result = value.as<float>() != 0.0f;
        return true;
    }

    if (value.is<const char *>())
    {
        String text = value.as<const char *>();
        text.trim();
        text.toUpperCase();
        if (text == "ON" || text == "TRUE" || text == "1")
        {
            result = true;
            return true;
        }
        if (text == "OFF" || text == "FALSE" || text == "0")
        {
            result = false;
            return true;
        }
    }

    return false;
}

bool parseUint8Like(const JsonVariantConst &value, uint8_t &result)
{
    long parsedValue = 0;

    if (value.is<int>())
    {
        parsedValue = value.as<int>();
    }
    else if (value.is<float>())
    {
        parsedValue = lroundf(value.as<float>());
    }
    else if (value.is<const char *>())
    {
        String text = value.as<const char *>();
        text.trim();
        if (text.isEmpty())
            return false;

        char *endPtr = nullptr;
        parsedValue = strtol(text.c_str(), &endPtr, 10);
        if (endPtr == nullptr || *endPtr != '\0')
            return false;
    }
    else
    {
        return false;
    }

    if (parsedValue < 0)
        parsedValue = 0;
    if (parsedValue > 255)
        parsedValue = 255;

    result = static_cast<uint8_t>(parsedValue);
    return true;
}

bool parseRgbLike(const JsonVariantConst &value, uint8_t &red, uint8_t &green, uint8_t &blue)
{
    if (!value.is<JsonObjectConst>())
        return false;

    const JsonObjectConst object = value.as<JsonObjectConst>();
    uint8_t parsedRed = 0;
    uint8_t parsedGreen = 0;
    uint8_t parsedBlue = 0;

    if (!parseUint8Like(object["r"], parsedRed))
        return false;
    if (!parseUint8Like(object["g"], parsedGreen))
        return false;
    if (!parseUint8Like(object["b"], parsedBlue))
        return false;

    red = parsedRed;
    green = parsedGreen;
    blue = parsedBlue;
    return true;
}

bool parseDoorLike(const JsonVariantConst &value, bool &open)
{
    if (parseBoolLike(value, open))
        return true;

    if (value.is<const char *>())
    {
        String text = value.as<const char *>();
        text.trim();
        text.toUpperCase();

        if (text == "OPEN")
        {
            open = true;
            return true;
        }

        if (text == "CLOSE" || text == "CLOSED")
        {
            open = false;
            return true;
        }
    }

    return false;
}

void sendRpcResponse(const String &requestId, bool ok, const String &message)
{
    if (requestId.isEmpty() || !g_mqttClient.connected())
        return;

    DynamicJsonDocument doc(256);
    doc["ok"] = ok;
    doc["message"] = message;

    const String topic = makeRpcResponseTopic(requestId);
    publishJson(topic.c_str(), doc);
}

void sendRpcBoolResponse(const String &requestId, bool value)
{
    if (requestId.isEmpty() || !g_mqttClient.connected())
        return;

    const String topic = makeRpcResponseTopic(requestId);
    g_mqttClient.publish(topic.c_str(), value ? "true" : "false");
}

void sendRpcIntResponse(const String &requestId, int value)
{
    if (requestId.isEmpty() || !g_mqttClient.connected())
        return;

    const String topic = makeRpcResponseTopic(requestId);
    const String payload = String(value);
    g_mqttClient.publish(topic.c_str(), payload.c_str());
}

void sendRpcRgbResponse(const String &requestId, uint8_t red, uint8_t green, uint8_t blue)
{
    if (requestId.isEmpty() || !g_mqttClient.connected())
        return;

    DynamicJsonDocument doc(128);
    doc["r"] = red;
    doc["g"] = green;
    doc["b"] = blue;

    const String topic = makeRpcResponseTopic(requestId);
    publishJson(topic.c_str(), doc);
}

String extractRequestId(const String &topic)
{
    const int slashIndex = topic.lastIndexOf('/');
    if (slashIndex < 0 || slashIndex + 1 >= static_cast<int>(topic.length()))
        return "";
    return topic.substring(slashIndex + 1);
}

void handleRpcCommand(const String &topic, const JsonDocument &doc)
{
    if (g_ctx == nullptr)
        return;

    const String method = doc["method"] | "";
    const JsonVariantConst params = doc["params"];
    const String requestId = extractRequestId(topic);
    bool powerOn = false;
    bool fanOn = false;
    bool doorOpen = false;
    uint8_t fanSpeed = 0;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;

    if (method.equalsIgnoreCase("POWER") || method.equalsIgnoreCase("setValue"))
    {
        if (!parseBoolLike(params, powerOn))
        {
            sendRpcResponse(requestId, false, "Invalid POWER parameter");
            return;
        }

        local_set_relay(g_ctx, kRuleRelayIndex, powerOn);
        publishStatusAttribute("rpc", powerOn, 0.0f, 0.0f);
        sendRpcResponse(requestId, true, powerOn ? "POWER ON" : "POWER OFF");
        return;
    }

    if (method.equalsIgnoreCase("RELAY1"))
    {
        if (!parseBoolLike(params, powerOn))
        {
            sendRpcResponse(requestId, false, "Invalid RELAY1 parameter");
            return;
        }

        local_set_relay(g_ctx, 1, powerOn);
        sendRpcResponse(requestId, true, powerOn ? "RELAY1 ON" : "RELAY1 OFF");
        return;
    }

    if (method.equalsIgnoreCase("RELAY2"))
    {
        if (!parseBoolLike(params, powerOn))
        {
            sendRpcResponse(requestId, false, "Invalid RELAY2 parameter");
            return;
        }

        local_set_relay(g_ctx, 2, powerOn);
        sendRpcResponse(requestId, true, powerOn ? "RELAY2 ON" : "RELAY2 OFF");
        return;
    }

    if (method.equalsIgnoreCase("setLed") || method.equalsIgnoreCase("setStateLED"))
    {
        if (params.is<JsonObjectConst>())
        {
            if (!parseBoolLike(params["value"], powerOn))
            {
                sendRpcResponse(requestId, false, "Invalid LED parameter");
                return;
            }
        }
        else if (!parseBoolLike(params, powerOn))
        {
            sendRpcResponse(requestId, false, "Invalid LED parameter");
            return;
        }

        local_set_rgb(
            g_ctx,
            powerOn ? kLedWhiteValue : 0,
            powerOn ? kLedWhiteValue : 0,
            powerOn ? kLedWhiteValue : 0);
        sendRpcResponse(requestId, true, powerOn ? "LED ON" : "LED OFF");
        return;
    }

    if (method.equalsIgnoreCase("getLed"))
    {
        bool ledState = false;
        if (g_ctx->stateMutex != nullptr && xSemaphoreTake(g_ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            ledState = g_ctx->rgbLedOn;
            xSemaphoreGive(g_ctx->stateMutex);
        }

        sendRpcBoolResponse(requestId, ledState);
        return;
    }

    if (method.equalsIgnoreCase("setFan"))
    {
        if (params.is<JsonObjectConst>())
        {
            if (!parseBoolLike(params["value"], fanOn))
            {
                sendRpcResponse(requestId, false, "Invalid FAN parameter");
                return;
            }
        }
        else if (!parseBoolLike(params, fanOn))
        {
            sendRpcResponse(requestId, false, "Invalid FAN parameter");
            return;
        }

        uint8_t currentSpeed = 100;
        if (g_ctx->stateMutex != nullptr && xSemaphoreTake(g_ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            currentSpeed = g_ctx->fanSpeed == 0 ? 100 : g_ctx->fanSpeed;
            xSemaphoreGive(g_ctx->stateMutex);
        }

        local_set_fan(g_ctx, fanOn, fanOn ? currentSpeed : 0);
        sendRpcResponse(requestId, true, fanOn ? "FAN ON" : "FAN OFF");
        return;
    }

    if (method.equalsIgnoreCase("getFan"))
    {
        if (g_ctx->stateMutex != nullptr && xSemaphoreTake(g_ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            fanOn = g_ctx->fanOn;
            xSemaphoreGive(g_ctx->stateMutex);
        }

        sendRpcBoolResponse(requestId, fanOn);
        return;
    }

    if (method.equalsIgnoreCase("setDoor"))
    {
        if (params.is<JsonObjectConst>())
        {
            const JsonObjectConst object = params.as<JsonObjectConst>();
            if (!object["value"].isNull())
            {
                if (!parseDoorLike(object["value"], doorOpen))
                {
                    sendRpcResponse(requestId, false, "Invalid DOOR parameter");
                    return;
                }
            }
            else if (!parseDoorLike(object["state"], doorOpen))
            {
                sendRpcResponse(requestId, false, "Invalid DOOR parameter");
                return;
            }
        }
        else if (!parseDoorLike(params, doorOpen))
        {
            sendRpcResponse(requestId, false, "Invalid DOOR parameter");
            return;
        }

        local_set_door(g_ctx, doorOpen);
        sendRpcResponse(requestId, true, doorOpen ? "DOOR OPEN" : "DOOR CLOSED");
        return;
    }

    if (method.equalsIgnoreCase("getDoor"))
    {
        if (g_ctx->stateMutex != nullptr && xSemaphoreTake(g_ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            doorOpen = g_ctx->doorOpen;
            xSemaphoreGive(g_ctx->stateMutex);
        }

        sendRpcBoolResponse(requestId, doorOpen);
        return;
    }

    if (method.equalsIgnoreCase("setFanSpeed"))
    {
        if (params.is<JsonObjectConst>())
        {
            if (!parseUint8Like(params["value"], fanSpeed))
            {
                sendRpcResponse(requestId, false, "Invalid FAN SPEED parameter");
                return;
            }
        }
        else if (!parseUint8Like(params, fanSpeed))
        {
            sendRpcResponse(requestId, false, "Invalid FAN SPEED parameter");
            return;
        }

        if (fanSpeed > 100)
            fanSpeed = 100;

        local_set_fan(g_ctx, fanSpeed > 0, fanSpeed);
        sendRpcResponse(requestId, true, "FAN SPEED " + String(fanSpeed));
        return;
    }

    if (method.equalsIgnoreCase("getFanSpeed"))
    {
        if (g_ctx->stateMutex != nullptr && xSemaphoreTake(g_ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            fanSpeed = g_ctx->fanSpeed;
            xSemaphoreGive(g_ctx->stateMutex);
        }

        sendRpcIntResponse(requestId, fanSpeed);
        return;
    }

    if (method.equalsIgnoreCase("setRgb"))
    {
        if (!parseRgbLike(params, red, green, blue))
        {
            sendRpcResponse(requestId, false, "Invalid RGB parameter");
            return;
        }

        local_set_rgb(g_ctx, red, green, blue);
        sendRpcResponse(requestId, true, "RGB updated");
        return;
    }

    if (method.equalsIgnoreCase("getRgb"))
    {
        if (g_ctx->stateMutex != nullptr && xSemaphoreTake(g_ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            red = g_ctx->rgbRed;
            green = g_ctx->rgbGreen;
            blue = g_ctx->rgbBlue;
            xSemaphoreGive(g_ctx->stateMutex);
        }

        sendRpcRgbResponse(requestId, red, green, blue);
        return;
    }

    sendRpcResponse(requestId, false, "Unsupported method: " + method);
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    String body;
    body.reserve(length + 1);
    for (unsigned int index = 0; index < length; ++index)
        body += static_cast<char>(payload[index]);

    DynamicJsonDocument doc(512);
    const DeserializationError error = deserializeJson(doc, body);
    if (error)
    {
        Serial.print("[COREIOT] RPC parse failed: ");
        Serial.println(error.c_str());
        return;
    }

    handleRpcCommand(String(topic), doc);
}

bool connectCoreIot(AppContext *ctx)
{
    String server;
    String token;
    uint16_t port = 1883;

    if (ctx != NULL && lockWithTimeout(ctx->configMutex, portMAX_DELAY))
    {
        server = ctx->coreIotServer;
        token = ctx->coreIotToken;
        port = static_cast<uint16_t>(ctx->coreIotPort.toInt() > 0 ? ctx->coreIotPort.toInt() : 1883);
        xSemaphoreGive(ctx->configMutex);
    }

    if (server.isEmpty() || token.isEmpty())
    {
        Serial.println("[COREIOT] Missing server or token");
        return false;
    }

    g_mqttClient.setServer(server.c_str(), port);
    g_mqttClient.setCallback(mqttCallback);
    g_mqttClient.setBufferSize(kMqttBufferSize);
    g_mqttClient.setKeepAlive(60);

    const String clientId = buildClientId();
    if (!g_mqttClient.connect(clientId.c_str(), token.c_str(), ""))
    {
        Serial.println("[COREIOT] MQTT connect failed");
        return false;
    }

    g_mqttClient.subscribe(kRpcRequestTopic);
    g_lastPublishMs = millis() - kPublishIntervalMs;
    Serial.println("[COREIOT] MQTT connected and RPC subscription active");
    return true;
}

bool shouldForceAutoRelay(float temperature, float humidity)
{
    if (temperature == 0.0f && humidity == 0.0f)
        return false;

    return temperature >= kHighTempThreshold || temperature <= kLowTempThreshold;
}

void applyTemperatureRule(AppContext *ctx, float temperature, float humidity)
{
    if (ctx == nullptr || !shouldForceAutoRelay(temperature, humidity))
        return;

    const bool desiredPowerOn = temperature >= kHighTempThreshold;
    bool currentPowerOn = false;

    if (ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        currentPowerOn = ctx->relay1On;
        xSemaphoreGive(ctx->stateMutex);
    }

    if (g_lastAutoRelayStateValid && g_lastAutoRelayState == desiredPowerOn && currentPowerOn == desiredPowerOn)
        return;

    if (currentPowerOn != desiredPowerOn)
    {
        local_set_relay(ctx, kRuleRelayIndex, desiredPowerOn);
    }

    publishStatusAttribute(desiredPowerOn ? "temperature_high" : "temperature_low", desiredPowerOn, temperature, humidity);
    g_lastAutoRelayState = desiredPowerOn;
    g_lastAutoRelayStateValid = true;
}
} // namespace

void CORE_IOT_sendata(String mode, String feed, String data)
{
    if (!g_mqttClient.connected())
        return;

    if (mode == "telemetry")
    {
        g_mqttClient.publish(kTelemetryTopic, data.c_str());
        return;
    }

    if (mode == "attributes")
    {
        g_mqttClient.publish(kAttributesTopic, data.c_str());
        return;
    }

    if (!feed.isEmpty())
        g_mqttClient.publish(feed.c_str(), data.c_str());
}

bool CORE_IOT_reconnect()
{
    if (g_ctx == nullptr)
        return false;

    if (WiFi.status() != WL_CONNECTED)
        return false;

    if (g_mqttClient.connected())
        return true;

    return connectCoreIot(g_ctx);
}

void coreiot_thingsboard_task(void *pvParameters)
{
    g_ctx = static_cast<AppContext *>(pvParameters);
    if (g_ctx == nullptr)
    {
        vTaskDelete(NULL);
        return;
    }

    g_mqttClient.setBufferSize(kMqttBufferSize);
    g_mqttClient.setCallback(mqttCallback);

    while (1)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            if (!g_mqttClient.connected())
            {
                if (millis() - g_lastReconnectAttemptMs >= kReconnectDelayMs)
                {
                    g_lastReconnectAttemptMs = millis();
                    (void)CORE_IOT_reconnect();
                }
            }
            else
            {
                g_mqttClient.loop();

                float temperature = 0.0f;
                float humidity = 0.0f;
                float tinymlScore = 0.0f;
                float tinymlProbNormal = 0.0f;
                float tinymlProbThreshold = 0.0f;
                float tinymlProbSpike = 0.0f;
                TinyMLState tinymlState = TINYML_IDLE;

                if (g_ctx->stateMutex != NULL && xSemaphoreTake(g_ctx->stateMutex, portMAX_DELAY) == pdTRUE)
                {
                    temperature = g_ctx->temperature;
                    humidity = g_ctx->humidity;
                    tinymlScore = g_ctx->tinymlScore;
                    tinymlProbNormal = g_ctx->tinymlProbNormal;
                    tinymlProbThreshold = g_ctx->tinymlProbThreshold;
                    tinymlProbSpike = g_ctx->tinymlProbSpike;
                    tinymlState = g_ctx->tinymlState;
                    xSemaphoreGive(g_ctx->stateMutex);
                }

                if (millis() - g_lastPublishMs >= kPublishIntervalMs)
                {
                    publishRuntimeSnapshot(g_ctx, temperature, humidity);
                    g_lastPublishMs = millis();

                    Serial.printf(
                        "[COREIOT] T=%.1f H=%.1f Score=%.4f P=[%.3f %.3f %.3f] State=%s\n",
                        temperature,
                        humidity,
                        tinymlScore,
                        tinymlProbNormal,
                        tinymlProbThreshold,
                        tinymlProbSpike,
                        tinyMLStateToString(tinymlState));
                }

                applyTemperatureRule(g_ctx, temperature, humidity);
            }
        }
        else
        {
            g_lastAutoRelayStateValid = false;
            if (g_mqttClient.connected())
                g_mqttClient.disconnect();
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
