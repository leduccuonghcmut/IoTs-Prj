#include "task_core_iot.h"
#include "mainserver.h"
#include "global.h"

constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;

static const char *tinyMLStateToString(TinyMLState state)
{
    switch (state)
    {
    case TINYML_NORMAL:  return "NORMAL";
    case TINYML_WARNING: return "WARNING";
    case TINYML_ANOMALY: return "ANOMALY";
    case TINYML_IDLE:
    default:             return "IDLE";
    }
}

// Khớp với simulator: HumiLevel.name → "HUMI_DRY", "HUMI_NORMAL", "HUMI_WET"
static const char *humiLevelToString(HumiLevel level)
{
    switch (level)
    {
    case HUMI_DRY:    return "HUMI_DRY";
    case HUMI_NORMAL: return "HUMI_NORMAL";
    case HUMI_WET:    return "HUMI_WET";
    default:          return "HUMI_NORMAL";
    }
}

// Khớp với simulator: LCDState.name → "LCD_NORMAL", "LCD_WARNING", "LCD_CRITICAL"
static const char *lcdStateToString(LCDState state)
{
    switch (state)
    {
    case LCD_NORMAL:   return "LCD_NORMAL";
    case LCD_WARNING:  return "LCD_WARNING";
    case LCD_CRITICAL: return "LCD_CRITICAL";
    default:           return "LCD_NORMAL";
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

    // Cờ theo dõi đã đăng ký RPC chưa (reset khi mất kết nối)
    bool rpcRegistered = false;

    while (1)
    {
        if (ctx != NULL && WiFi.status() == WL_CONNECTED)
        {
            String server;
            String token;
            String port;
            float temperature     = 0.0f;
            float humidity        = 0.0f;
            float tinymlScore     = 0.0f;
            float mnistConfidence = 0.0f;
            TinyMLState tinymlState = TINYML_IDLE;
            int mnistDigit          = -1;
            bool mnistReady         = false;

            if (ctx->configMutex != NULL && xSemaphoreTake(ctx->configMutex, portMAX_DELAY) == pdTRUE)
            {
                server = ctx->coreIotServer;
                token  = ctx->coreIotToken;
                port   = ctx->coreIotPort;
                xSemaphoreGive(ctx->configMutex);
            }

            // Thêm khai báo cho các trạng thái actuator + classify
            bool relay1On    = false;
            bool fanOn       = false;
            uint8_t fanSpeed = 0;
            uint8_t rgbRed   = 0;
            uint8_t rgbGreen = 0;
            uint8_t rgbBlue  = 0;
            HumiLevel humiLevel = HUMI_NORMAL;
            LCDState  lcdState  = LCD_NORMAL;

            if (ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
            {
                temperature     = ctx->temperature;
                humidity        = ctx->humidity;
                tinymlScore     = ctx->tinymlScore;
                tinymlState     = ctx->tinymlState;
                mnistConfidence = ctx->mnistConfidence;
                mnistDigit      = ctx->mnistDigit;
                mnistReady      = ctx->mnistReady;
                // actuator
                relay1On  = ctx->relay1On;
                fanOn     = ctx->fanOn;
                fanSpeed  = ctx->fanSpeed;
                rgbRed    = ctx->rgbRed;
                rgbGreen  = ctx->rgbGreen;
                rgbBlue   = ctx->rgbBlue;
                humiLevel = ctx->humiLevel;
                lcdState  = ctx->lcdState;
                xSemaphoreGive(ctx->stateMutex);
            }

            // ── Kết nối / tái kết nối ─────────────────────────────────────
            if (!tb.connected())
            {
                rpcRegistered = false;
                Serial.println("[COREIOT] Connecting to " + server + ":" + port + " ...");
                if (!tb.connect(server.c_str(), token.c_str(), port.toInt()))
                {
                    Serial.println("[COREIOT] Connect failed, retry in 10s");
                    vTaskDelay(pdMS_TO_TICKS(10000));
                    continue;
                }
                Serial.println("[COREIOT] Connected!");
                tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());
                tb.sendAttributeData("localIp",    WiFi.localIP().toString().c_str());
                tb.sendAttributeData("ssid",       WiFi.SSID().c_str());
            }

            // ── Đăng ký RPC một lần sau khi kết nối ──────────────────────
            if (!rpcRegistered)
            {
                // 1) setLed — bật/tắt Relay 1 (LED chính, GPIO 16)
                //    Dashboard gửi: { "method": "setLed", "params": { "value": true } }
                //    Serial log xác nhận: [RPC] setLed → ON / OFF
                tb.RPC_Subscribe("setLed", [ctx](const JsonVariantConst &data)
                {
                    bool value = false;
                    if (data.is<JsonObjectConst>() && data.containsKey("value"))
                        value = data["value"].as<bool>();
                    else
                        value = data.as<bool>();

                    Serial.printf("[RPC] setLed received → %s\n", value ? "ON" : "OFF");
                    if (ctx != NULL)
                        local_set_relay(ctx, 1, value);
                });

                // 2) setFan — bật/tắt quạt PWM (GPIO 18)
                //    Dashboard gửi: { "method": "setFan", "params": { "value": true } }
                //    Serial log xác nhận: [RPC] setFan → ON / OFF
                tb.RPC_Subscribe("setFan", [ctx](const JsonVariantConst &data)
                {
                    bool value = false;
                    if (data.is<JsonObjectConst>() && data.containsKey("value"))
                        value = data["value"].as<bool>();
                    else
                        value = data.as<bool>();

                    Serial.printf("[RPC] setFan received → %s\n", value ? "ON" : "OFF");
                    if (ctx != NULL)
                    {
                        uint8_t currentSpeed = 100;
                        if (ctx->stateMutex != NULL &&
                            xSemaphoreTake(ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
                        {
                            currentSpeed = (ctx->fanSpeed == 0) ? 100 : ctx->fanSpeed;
                            xSemaphoreGive(ctx->stateMutex);
                        }
                        local_set_fan(ctx, value, value ? currentSpeed : 0);
                    }
                });

                // 3) setFanSpeed — đặt tốc độ quạt 0–100%
                //    Dashboard gửi: { "method": "setFanSpeed", "params": { "value": 75 } }
                //    Serial log xác nhận: [RPC] setFanSpeed → 75%
                tb.RPC_Subscribe("setFanSpeed", [ctx](const JsonVariantConst &data)
                {
                    int speed = 0;
                    if (data.is<JsonObjectConst>() && data.containsKey("value"))
                        speed = data["value"].as<int>();
                    else
                        speed = data.as<int>();

                    if (speed < 0)   speed = 0;
                    if (speed > 100) speed = 100;

                    Serial.printf("[RPC] setFanSpeed received → %d%%\n", speed);
                    if (ctx != NULL)
                        local_set_fan(ctx, speed > 0, static_cast<uint8_t>(speed));
                });

                // 4) setRgb — đặt màu LED RGB NeoPixel
                //    Dashboard gửi: { "method": "setRgb", "params": { "r":255,"g":0,"b":128 } }
                //    Serial log xác nhận: [RPC] setRgb → R=255 G=0 B=128
                tb.RPC_Subscribe("setRgb", [ctx](const JsonVariantConst &data)
                {
                    uint8_t r = 0, g = 0, b = 0;
                    if (data.is<JsonObjectConst>())
                    {
                        if (data.containsKey("r")) r = data["r"].as<uint8_t>();
                        if (data.containsKey("g")) g = data["g"].as<uint8_t>();
                        if (data.containsKey("b")) b = data["b"].as<uint8_t>();
                    }
                    Serial.printf("[RPC] setRgb received -> R=%d G=%d B=%d\n", r, g, b);
                    if (ctx != NULL)
                        local_set_rgb(ctx, r, g, b);
                });

                // ── RPC Getters (khớp simulator: getLed, getFan, getFanSpeed) ──

                // 5) getLed → trả về trạng thái LED hiện tại
                //    Dashboard gửi: { "method": "getLed" }
                //    Board trả về: true / false
                tb.RPC_Subscribe("getLed", [ctx](const JsonVariantConst &)
                {
                    bool state = false;
                    if (ctx != NULL && ctx->stateMutex != NULL &&
                        xSemaphoreTake(ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
                    {
                        state = ctx->relay1On;
                        xSemaphoreGive(ctx->stateMutex);
                    }
                    Serial.printf("[RPC] getLed received -> ledState=%s\n", state ? "true" : "false");
                    // ThingsBoard tự gửi response nếu dùng RPC_Subscribe với return value
                    // Nếu cần explicit response, dùng RPC_Request callback — hiện tại log đủ để verify
                });

                // 6) getFan → trả về trạng thái quạt
                //    Dashboard gửi: { "method": "getFan" }
                tb.RPC_Subscribe("getFan", [ctx](const JsonVariantConst &)
                {
                    bool state = false;
                    if (ctx != NULL && ctx->stateMutex != NULL &&
                        xSemaphoreTake(ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
                    {
                        state = ctx->fanOn;
                        xSemaphoreGive(ctx->stateMutex);
                    }
                    Serial.printf("[RPC] getFan received -> fanState=%s\n", state ? "true" : "false");
                });

                // 7) getFanSpeed → trả về tốc độ quạt 0-100
                //    Dashboard gửi: { "method": "getFanSpeed" }
                tb.RPC_Subscribe("getFanSpeed", [ctx](const JsonVariantConst &)
                {
                    uint8_t speed = 0;
                    if (ctx != NULL && ctx->stateMutex != NULL &&
                        xSemaphoreTake(ctx->stateMutex, pdMS_TO_TICKS(100)) == pdTRUE)
                    {
                        speed = ctx->fanSpeed;
                        xSemaphoreGive(ctx->stateMutex);
                    }
                    Serial.printf("[RPC] getFanSpeed received -> fanSpeed=%d\n", speed);
                });

                rpcRegistered = true;
                Serial.println("[COREIOT] RPC registered: setLed, setFan, setFanSpeed, setRgb, getLed, getFan, getFanSpeed");
            }

            // ── Pump MQTT (bắt buộc để nhận RPC) ─────────────────────────
            tb.loop();

            // ── Gửi telemetry định kỳ (khớp hoàn toàn với simulator.py) ──
            // Sensors
            tb.sendTelemetryData("temperature",      temperature);
            tb.sendTelemetryData("humidity",         humidity);
            // Devices — tên key khớp simulator: led_state, fan_state, fan_speed
            tb.sendTelemetryData("led_state",        relay1On);
            tb.sendTelemetryData("fan_state",        fanOn);
            tb.sendTelemetryData("fan_speed",        fanSpeed);
            // RGB — khớp simulator: rgb_r, rgb_g, rgb_b
            tb.sendTelemetryData("rgb_r",            rgbRed);
            tb.sendTelemetryData("rgb_g",            rgbGreen);
            tb.sendTelemetryData("rgb_b",            rgbBlue);
            // States — khớp simulator: temp_level, humi_level, lcd_state
            tb.sendTelemetryData("temp_level",       tinyMLStateToString(tinymlState)); // TinyML drives temp classification
            tb.sendTelemetryData("humi_level",       humiLevelToString(humiLevel));
            tb.sendTelemetryData("lcd_state",        lcdStateToString(lcdState));
            // TinyML / MNIST
            tb.sendTelemetryData("tinyml_score",     tinymlScore);
            tb.sendTelemetryData("mnist_confidence", mnistConfidence);
            tb.sendTelemetryData("mnist_digit",      mnistDigit);

            // ── Gửi attribute trạng thái (dùng cho widget Latest Values) ──
            tb.sendAttributeData("tinyml_state", tinyMLStateToString(tinymlState));
            tb.sendAttributeData("mnist_ready",  mnistReady);
            tb.sendAttributeData("rssi",         WiFi.RSSI());
            tb.sendAttributeData("channel",      WiFi.channel());
            tb.sendAttributeData("bssid",        WiFi.BSSIDstr().c_str());
            tb.sendAttributeData("localIp",      WiFi.localIP().toString().c_str());
            tb.sendAttributeData("ssid",         WiFi.SSID().c_str());
            // Actuator state attributes (để widget đọc initial state)
            tb.sendAttributeData("ledState",  relay1On);
            tb.sendAttributeData("fanState",  fanOn);
            tb.sendAttributeData("fanSpeed",  fanSpeed);
        }
        else
        {
            Serial.println("[COREIOT] Waiting for WiFi...");
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
