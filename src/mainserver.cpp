#include "mainserver.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ESP32Servo.h>
#include "global.h"
#include "fan_control.h"

#define RELAY1_PIN 16
#define RELAY2_PIN 17
#define DOOR_PIN   5

struct MainServerState
{
  bool led1 = false;
  bool led2 = false;
  bool isAPMode = true;
  bool doorOpen = false;
  bool fanOn = false;
  uint8_t fanSpeed = 0;
  bool connecting = false;
  unsigned long connectStartMs = 0;
  String staIp = "";
  String wifiMessage = "AP mode";
};

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

static const char *tinyMLStateDescription(TinyMLState state)
{
  switch (state)
  {
    case TINYML_NORMAL:
      return "Du lieu hien tai nam trong vung an toan.";
    case TINYML_WARNING:
      return "Moi truong dang co dau hieu lech khoi mau binh thuong.";
    case TINYML_ANOMALY:
      return "Model phat hien bat thuong ro rang, can kiem tra ngay.";
    case TINYML_IDLE:
    default:
      return "TinyML dang cho du lieu cam bien.";
  }
}

void startAP()
{
}

void setupServer()
{
}

void connectToWiFi()
{
}

void main_server_task(void *pvParameters)
{
  AppContext *ctx = static_cast<AppContext *>(pvParameters);
  MainServerState state;
  Servo doorServo;
  WebServer server(80);

  auto sendJson = [&](const String &json, int code = 200)
  {
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.send(code, "application/json", json);
  };

  auto startApMode = [&]()
  {
    String apSsid = "ESP32-YOUR NETWORK HERE!!!";
    String apPassword = "12345678";
    if (ctx != NULL && ctx->configMutex != NULL && xSemaphoreTake(ctx->configMutex, portMAX_DELAY) == pdTRUE)
    {
      apSsid = ctx->apSsid;
      apPassword = ctx->apPassword;
      xSemaphoreGive(ctx->configMutex);
    }

    WiFi.disconnect(false, false);
    delay(200);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSsid.c_str(), apPassword.c_str());
    state.isAPMode = true;
    state.connecting = false;
    state.staIp = "";
    state.wifiMessage = "AP mode";

    if (ctx != NULL && ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
    {
      ctx->wifiConnected = false;
      xSemaphoreGive(ctx->stateMutex);
    }
  };

  auto beginStation = [&]()
  {
    String wifiSsid;
    String wifiPass;
    if (ctx != NULL && ctx->configMutex != NULL && xSemaphoreTake(ctx->configMutex, portMAX_DELAY) == pdTRUE)
    {
      wifiSsid = ctx->wifiSsid;
      wifiPass = ctx->wifiPass;
      xSemaphoreGive(ctx->configMutex);
    }

    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    if (wifiPass.isEmpty())
      WiFi.begin(wifiSsid.c_str());
    else
      WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  };

  auto buildDeviceStateJson = [&]()
  {
    float temperature = 0.0f;
    float humidity = 0.0f;
    float tinymlScore = 0.0f;
    bool tinymlReady = false;
    TinyMLState tinymlState = TINYML_IDLE;

    if (ctx != NULL && ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
    {
      temperature = ctx->temperature;
      humidity = ctx->humidity;
      tinymlScore = ctx->tinymlScore;
      tinymlReady = ctx->tinymlReady;
      tinymlState = ctx->tinymlState;
      xSemaphoreGive(ctx->stateMutex);
    }

    String json = "{";
    json += "\"led1\":" + String(state.led1 ? "true" : "false") + ",";
    json += "\"led2\":" + String(state.led2 ? "true" : "false") + ",";
    json += "\"door\":\"" + String(state.doorOpen ? "open" : "closed") + "\",";
    json += "\"fan_on\":" + String(state.fanOn ? "true" : "false") + ",";
    json += "\"fan_speed\":" + String(state.fanSpeed) + ",";
    json += "\"fan\":\"" + String(state.fanOn ? "ON" : "OFF") + "\",";
    json += "\"temp\":" + String(temperature, 1) + ",";
    json += "\"hum\":" + String(humidity, 1) + ",";
    json += "\"tinyml_ready\":" + String(tinymlReady ? "true" : "false") + ",";
    json += "\"tinyml_score\":" + String(tinymlScore, 6) + ",";
    json += "\"tinyml_state\":\"" + String(tinyMLStateToString(tinymlState)) + "\",";
    json += "\"tinyml_desc\":\"" + String(tinyMLStateDescription(tinymlState)) + "\"";
    json += "}";
    return json;
  };

  pinMode(BOOT_PIN, INPUT_PULLUP);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);
  fan_init();
  fan_off();

  doorServo.setPeriodHertz(50);
  doorServo.attach(DOOR_PIN, 500, 2400);
  doorServo.write(0);

  if (!LittleFS.begin(true))
    Serial.println("LittleFS mount failed");

  startApMode();

  server.enableCORS(true);
  server.on("/", HTTP_GET, [&]() { File file = LittleFS.open("/index.html", "r"); server.streamFile(file, "text/html"); file.close(); });
  server.on("/index.html", HTTP_GET, [&]() { File file = LittleFS.open("/index.html", "r"); server.streamFile(file, "text/html"); file.close(); });
  server.on("/styles.css", HTTP_GET, [&]() { File file = LittleFS.open("/styles.css", "r"); server.streamFile(file, "text/css"); file.close(); });
  server.on("/script.js", HTTP_GET, [&]() { File file = LittleFS.open("/script.js", "r"); server.streamFile(file, "application/javascript"); file.close(); });
  server.on("/settings", HTTP_GET, [&]() { File file = LittleFS.open("/settings.html", "r"); server.streamFile(file, "text/html"); file.close(); });
  server.on("/settings.html", HTTP_GET, [&]() { File file = LittleFS.open("/settings.html", "r"); server.streamFile(file, "text/html"); file.close(); });

  server.on("/toggle", HTTP_GET, [&]()
  {
    if (!server.hasArg("led"))
    {
      sendJson("{\"error\":\"Missing led parameter\"}", 400);
      return;
    }

    int led = server.arg("led").toInt();
    if (led == 1)
    {
      state.led1 = !state.led1;
      digitalWrite(RELAY1_PIN, state.led1 ? HIGH : LOW);
    }
    else if (led == 2)
    {
      state.led2 = !state.led2;
      digitalWrite(RELAY2_PIN, state.led2 ? HIGH : LOW);
    }
    else
    {
      sendJson("{\"error\":\"Invalid led value\"}", 400);
      return;
    }

    String json = "{";
    json += "\"led1\":\"" + String(state.led1 ? "ON" : "OFF") + "\",";
    json += "\"led2\":\"" + String(state.led2 ? "ON" : "OFF") + "\"";
    json += "}";
    sendJson(json);
  });

  server.on("/door", HTTP_ANY, [&]()
  {
    if (!server.hasArg("state"))
    {
      sendJson("{\"error\":\"missing state\"}", 400);
      return;
    }

    String doorState = server.arg("state");
    state.doorOpen = (doorState == "open");
    doorServo.write(state.doorOpen ? 90 : 0);
    sendJson("{\"door\":\"" + String(state.doorOpen ? "open" : "closed") + "\"}");
  });

  server.on("/fan", HTTP_ANY, [&]()
  {
    if (server.hasArg("speed"))
    {
      int speed = server.arg("speed").toInt();
      if (speed < 0) speed = 0;
      if (speed > 100) speed = 100;
      state.fanSpeed = static_cast<uint8_t>(speed);
      state.fanOn = state.fanSpeed > 0;
      if (state.fanOn)
        fan_set_speed(state.fanSpeed);
      else
        fan_off();
    }
    else if (server.hasArg("state"))
    {
      String fanState = server.arg("state");
      if (fanState == "on")
      {
        state.fanOn = true;
        if (state.fanSpeed == 0) state.fanSpeed = 100;
        fan_set_speed(state.fanSpeed);
      }
      else if (fanState == "off")
      {
        state.fanOn = false;
        state.fanSpeed = 0;
        fan_off();
      }
      else if (fanState == "toggle")
      {
        state.fanOn = !state.fanOn;
        if (state.fanOn)
        {
          if (state.fanSpeed == 0) state.fanSpeed = 100;
          fan_set_speed(state.fanSpeed);
        }
        else
        {
          state.fanSpeed = 0;
          fan_off();
        }
      }
    }

    String response = "{";
    response += "\"fan\":\"" + String(state.fanOn ? "on" : "off") + "\",";
    response += "\"fan_on\":" + String(state.fanOn ? "true" : "false") + ",";
    response += "\"speed\":" + String(state.fanSpeed) + ",";
    response += "\"fan_speed\":" + String(state.fanSpeed) + "}";
    sendJson(response);
  });

  server.on("/state", HTTP_GET, [&]() { sendJson(buildDeviceStateJson()); });
  server.on("/sensors", HTTP_GET, [&]() { sendJson(buildDeviceStateJson()); });
  server.on("/scan_wifi", HTTP_GET, [&]()
  {
    WiFi.scanDelete();
    int n = WiFi.scanNetworks();
    String json = "[";
    bool first = true;
    for (int i = 0; i < n; i++)
    {
      String ssidName = WiFi.SSID(i);
      if (ssidName.length() == 0)
        continue;
      if (!first) json += ",";
      first = false;
      json += "{\"ssid\":\"" + ssidName + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
    }
    json += "]";
    sendJson(json);
  });

  server.on("/connect", HTTP_ANY, [&]()
  {
    if (!server.hasArg("ssid"))
    {
      sendJson("{\"ok\":false,\"error\":\"Missing SSID\"}", 400);
      return;
    }

    if (ctx != NULL && ctx->configMutex != NULL && xSemaphoreTake(ctx->configMutex, portMAX_DELAY) == pdTRUE)
    {
      ctx->wifiSsid = server.arg("ssid");
      ctx->wifiPass = server.hasArg("pass") ? server.arg("pass") : "";
      xSemaphoreGive(ctx->configMutex);
    }

    state.connecting = true;
    state.connectStartMs = millis();
    state.wifiMessage = "Connecting...";
    beginStation();
    sendJson("{\"ok\":true,\"message\":\"Connecting...\"}");
  });

  server.on("/wifi_status", HTTP_GET, [&]()
  {
    String wifiSsid;
    bool wifiConnected = false;
    if (ctx != NULL && ctx->configMutex != NULL && xSemaphoreTake(ctx->configMutex, portMAX_DELAY) == pdTRUE)
    {
      wifiSsid = ctx->wifiSsid;
      xSemaphoreGive(ctx->configMutex);
    }
    if (ctx != NULL && ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
    {
      wifiConnected = ctx->wifiConnected;
      xSemaphoreGive(ctx->stateMutex);
    }

    String json = "{";
    json += "\"apMode\":" + String(state.isAPMode ? "true" : "false") + ",";
    json += "\"connecting\":" + String(state.connecting ? "true" : "false") + ",";
    json += "\"connected\":" + String(wifiConnected ? "true" : "false") + ",";
    json += "\"ssid\":\"" + wifiSsid + "\",";
    json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"sta_ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "") + "\",";
    json += "\"message\":\"" + state.wifiMessage + "\"}";
    sendJson(json);
  });

  server.onNotFound([&]() { server.send(404, "text/plain", "404 Not Found"); });
  server.begin();

  while (1)
  {
    server.handleClient();

    if (digitalRead(BOOT_PIN) == LOW)
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      if (digitalRead(BOOT_PIN) == LOW && !state.isAPMode)
      {
        startApMode();
      }
    }

    if (state.connecting)
    {
      wl_status_t status = WiFi.status();
      if (status == WL_CONNECTED)
      {
        state.staIp = WiFi.localIP().toString();
        state.wifiMessage = "Connected";
        state.isAPMode = false;
        state.connecting = false;

        if (ctx != NULL && ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
        {
          ctx->wifiConnected = true;
          xSemaphoreGive(ctx->stateMutex);
        }

        if (ctx != NULL && ctx->internetSemaphore != NULL)
          xSemaphoreGive(ctx->internetSemaphore);
      }
      else if (millis() - state.connectStartMs > 15000)
      {
        state.wifiMessage = "Connect timeout";
        state.connecting = false;
        startApMode();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
