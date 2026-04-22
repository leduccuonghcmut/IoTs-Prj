#include "mainserver.h"

#include <LittleFS.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ctype.h>

#include "NeoPixel.h"
#include "espnow_link.h"
#include "fan_control.h"
#include "global.h"

#define RELAY1_PIN 16
#define RELAY2_PIN 17
#define DOOR_PIN 5

namespace
{
struct WifiUiState
{
  bool isApMode = true;
  bool connecting = false;
  uint32_t connectStartMs = 0;
  String wifiMessage = "AP mode";
};

void logWifiLine(const String &message)
{
  Serial.println("[WIFI] " + message);
}

Servo &doorServoDevice()
{
  static Servo servo;
  return servo;
}

bool lockWithTimeout(SemaphoreHandle_t mutex, TickType_t timeout)
{
  return mutex != NULL && xSemaphoreTake(mutex, timeout) == pdTRUE;
}

String boolToJson(bool value)
{
  return value ? "true" : "false";
}

String escapeJson(const String &value)
{
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t index = 0; index < value.length(); ++index)
  {
    const char current = value[index];
    if (current == '\\' || current == '"')
      escaped += '\\';
    escaped += current;
  }

  return escaped;
}

bool parseHexColor(String hex, uint8_t &red, uint8_t &green, uint8_t &blue)
{
  hex.trim();
  if (hex.startsWith("#"))
    hex.remove(0, 1);

  if (hex.length() != 6)
    return false;

  for (size_t index = 0; index < hex.length(); ++index)
  {
    if (!isxdigit(static_cast<unsigned char>(hex[index])))
      return false;
  }

  const long color = strtol(hex.c_str(), NULL, 16);
  red = static_cast<uint8_t>((color >> 16) & 0xFF);
  green = static_cast<uint8_t>((color >> 8) & 0xFF);
  blue = static_cast<uint8_t>(color & 0xFF);
  return true;
}

void streamFsFile(WebServer &server, const char *path, const char *contentType)
{
  File file = LittleFS.open(path, "r");
  if (!file)
  {
    server.send(404, "text/plain", String("File not found: ") + path);
    return;
  }

  server.streamFile(file, contentType);
  file.close();
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

const char *tinyMLStateDescription(TinyMLState state)
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

void sendJson(WebServer &server, const String &json, int code = 200)
{
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(code, "application/json", json);
}

void setLocalActuatorState(AppContext *ctx, bool relay1On, bool relay2On, bool doorOpen, bool fanOn, uint8_t fanSpeed)
{
  if (ctx != NULL && lockWithTimeout(ctx->stateMutex, pdMS_TO_TICKS(100)))
  {
    ctx->relay1On = relay1On;
    ctx->relay2On = relay2On;
    ctx->doorOpen = doorOpen;
    ctx->fanOn = fanOn;
    ctx->fanSpeed = fanSpeed;
    xSemaphoreGive(ctx->stateMutex);
  }
}

String buildUnifiedStateJson(AppContext *ctx)
{
  bool relay1On = false;
  bool relay2On = false;
  bool doorOpen = false;
  bool fanOn = false;
  bool rgbLedOn = false;
  bool tinymlReady = false;
  bool mnistReady = false;
  bool remoteOnline = false;
  bool espNowReady = false;
  bool espNowPeerConfigured = false;
  bool remoteDoorOpen = false;
  bool remoteFanOn = false;
  bool remoteRgbOn = false;
  float temperature = 0.0f;
  float humidity = 0.0f;
  float tinymlScore = 0.0f;
  float mnistConfidence = 0.0f;
  float remoteTemperature = 0.0f;
  float remoteHumidity = 0.0f;
  int mnistDigit = -1;
  uint8_t rgbRed = 0;
  uint8_t rgbGreen = 0;
  uint8_t rgbBlue = 0;
  uint8_t fanSpeed = 0;
  uint8_t remoteFanSpeed = 0;
  uint8_t remoteRgbRed = 0;
  uint8_t remoteRgbGreen = 0;
  uint8_t remoteRgbBlue = 0;
  uint32_t remoteLastSeenMs = 0;
  uint32_t espNowPacketsRx = 0;
  uint32_t espNowPacketsTx = 0;
  TinyMLState tinymlState = TINYML_IDLE;
  String mnistStatus = "Camera host not configured.";
  String cameraHost;
  String peerMac;
  String localMac;
  String espNowStatus = "ESP-NOW not initialized.";
  String rgbHexText = "#000000";
  String remoteBoardName = "Remote Board";
  String remoteRgbHexText = "#000000";

  if (ctx != NULL && lockWithTimeout(ctx->stateMutex, portMAX_DELAY))
  {
    relay1On = ctx->relay1On;
    relay2On = ctx->relay2On;
    doorOpen = ctx->doorOpen;
    fanOn = ctx->fanOn;
    rgbLedOn = ctx->rgbLedOn;
    tinymlReady = ctx->tinymlReady;
    mnistReady = ctx->mnistReady;
    remoteOnline = ctx->remoteOnline;
    espNowReady = ctx->espNowReady;
    espNowPeerConfigured = ctx->espNowPeerConfigured;
    remoteDoorOpen = ctx->remoteDoorOpen;
    remoteFanOn = ctx->remoteFanOn;
    remoteRgbOn = ctx->remoteRgbOn;
    temperature = ctx->temperature;
    humidity = ctx->humidity;
    tinymlScore = ctx->tinymlScore;
    mnistConfidence = ctx->mnistConfidence;
    remoteTemperature = ctx->remoteTemperature;
    remoteHumidity = ctx->remoteHumidity;
    mnistDigit = ctx->mnistDigit;
    rgbRed = ctx->rgbRed;
    rgbGreen = ctx->rgbGreen;
    rgbBlue = ctx->rgbBlue;
    fanSpeed = ctx->fanSpeed;
    remoteFanSpeed = ctx->remoteFanSpeed;
    remoteRgbRed = ctx->remoteRgbRed;
    remoteRgbGreen = ctx->remoteRgbGreen;
    remoteRgbBlue = ctx->remoteRgbBlue;
    remoteLastSeenMs = ctx->remoteLastSeenMs;
    espNowPacketsRx = ctx->espNowPacketsRx;
    espNowPacketsTx = ctx->espNowPacketsTx;
    tinymlState = ctx->tinymlState;
    mnistStatus = ctx->mnistStatus;
    espNowStatus = ctx->espNowStatus;
    rgbHexText = ctx->rgbHexText;
    remoteRgbHexText = ctx->remoteRgbHexText;
    xSemaphoreGive(ctx->stateMutex);
  }

  if (ctx != NULL && lockWithTimeout(ctx->configMutex, portMAX_DELAY))
  {
    cameraHost = ctx->cameraHost;
    peerMac = ctx->peerMac;
    localMac = ctx->localMac;
    remoteBoardName = ctx->remoteBoardName;
    xSemaphoreGive(ctx->configMutex);
  }

  String json = "{";
  json += "\"door\":\"" + String(doorOpen ? "open" : "closed") + "\",";
  json += "\"fan_on\":" + boolToJson(fanOn) + ",";
  json += "\"fan_speed\":" + String(fanSpeed) + ",";
  json += "\"fan\":\"" + String(fanOn ? "ON" : "OFF") + "\",";
  json += "\"rgb_on\":" + boolToJson(rgbLedOn) + ",";
  json += "\"rgb_hex\":\"" + escapeJson(rgbHexText) + "\",";
  json += "\"rgb_red\":" + String(rgbRed) + ",";
  json += "\"rgb_green\":" + String(rgbGreen) + ",";
  json += "\"rgb_blue\":" + String(rgbBlue) + ",";
  json += "\"temp\":" + String(temperature, 1) + ",";
  json += "\"hum\":" + String(humidity, 1) + ",";
  json += "\"tinyml_ready\":" + boolToJson(tinymlReady) + ",";
  json += "\"tinyml_score\":" + String(tinymlScore, 6) + ",";
  json += "\"tinyml_state\":\"" + String(tinyMLStateToString(tinymlState)) + "\",";
  json += "\"tinyml_desc\":\"" + String(tinyMLStateDescription(tinymlState)) + "\",";
  json += "\"camera_host\":\"" + cameraHost + "\",";
  json += "\"mnist_ready\":" + boolToJson(mnistReady) + ",";
  json += "\"mnist_digit\":" + String(mnistDigit) + ",";
  json += "\"mnist_confidence\":" + String(mnistConfidence, 4) + ",";
  json += "\"mnist_status\":\"" + mnistStatus + "\",";
  json += "\"peer_mac\":\"" + peerMac + "\",";
  json += "\"local_mac\":\"" + localMac + "\",";
  json += "\"espnow_ready\":" + boolToJson(espNowReady) + ",";
  json += "\"espnow_peer_configured\":" + boolToJson(espNowPeerConfigured) + ",";
  json += "\"espnow_status\":\"" + espNowStatus + "\",";
  json += "\"espnow_packets_rx\":" + String(espNowPacketsRx) + ",";
  json += "\"espnow_packets_tx\":" + String(espNowPacketsTx) + ",";
  json += "\"remote_name\":\"" + remoteBoardName + "\",";
  json += "\"remote_online\":" + boolToJson(remoteOnline) + ",";
  json += "\"remote_last_seen_ms\":" + String(remoteLastSeenMs) + ",";
  json += "\"remote_temp\":" + String(remoteTemperature, 1) + ",";
  json += "\"remote_hum\":" + String(remoteHumidity, 1) + ",";
  json += "\"remote_door\":\"" + String(remoteDoorOpen ? "open" : "closed") + "\",";
  json += "\"remote_fan_on\":" + boolToJson(remoteFanOn) + ",";
  json += "\"remote_fan_speed\":" + String(remoteFanSpeed) + ",";
  json += "\"remote_fan\":\"" + String(remoteFanOn ? "ON" : "OFF") + "\",";
  json += "\"remote_rgb_on\":" + boolToJson(remoteRgbOn) + ",";
  json += "\"remote_rgb_hex\":\"" + escapeJson(remoteRgbHexText) + "\",";
  json += "\"remote_rgb_red\":" + String(remoteRgbRed) + ",";
  json += "\"remote_rgb_green\":" + String(remoteRgbGreen) + ",";
  json += "\"remote_rgb_blue\":" + String(remoteRgbBlue);
  json += "}";
  return json;
}
}

void local_set_relay(AppContext *ctx, uint8_t relayIndex, bool on)
{
  if (relayIndex == 1)
  {
    digitalWrite(RELAY1_PIN, on ? HIGH : LOW);
  }
  else if (relayIndex == 2)
  {
    digitalWrite(RELAY2_PIN, on ? HIGH : LOW);
  }

  bool relay1On = false;
  bool relay2On = false;
  bool doorOpen = false;
  bool fanOn = false;
  uint8_t fanSpeed = 0;

  if (ctx != NULL && lockWithTimeout(ctx->stateMutex, pdMS_TO_TICKS(100)))
  {
    if (relayIndex == 1)
      ctx->relay1On = on;
    if (relayIndex == 2)
      ctx->relay2On = on;

    relay1On = ctx->relay1On;
    relay2On = ctx->relay2On;
    doorOpen = ctx->doorOpen;
    fanOn = ctx->fanOn;
    fanSpeed = ctx->fanSpeed;
    xSemaphoreGive(ctx->stateMutex);
  }

  setLocalActuatorState(ctx, relay1On, relay2On, doorOpen, fanOn, fanSpeed);
}

void local_set_door(AppContext *ctx, bool open)
{
  doorServoDevice().write(open ? 90 : 0);

  bool relay1On = false;
  bool relay2On = false;
  bool fanOn = false;
  uint8_t fanSpeed = 0;
  if (ctx != NULL && lockWithTimeout(ctx->stateMutex, pdMS_TO_TICKS(100)))
  {
    relay1On = ctx->relay1On;
    relay2On = ctx->relay2On;
    fanOn = ctx->fanOn;
    fanSpeed = ctx->fanSpeed;
    xSemaphoreGive(ctx->stateMutex);
  }

  setLocalActuatorState(ctx, relay1On, relay2On, open, fanOn, fanSpeed);
}

void local_set_fan(AppContext *ctx, bool on, uint8_t speed)
{
  if (!on || speed == 0)
  {
    fan_off();
    speed = 0;
    on = false;
  }
  else
  {
    if (speed > 100)
      speed = 100;
    fan_set_speed(speed);
  }

  bool relay1On = false;
  bool relay2On = false;
  bool doorOpen = false;
  if (ctx != NULL && lockWithTimeout(ctx->stateMutex, pdMS_TO_TICKS(100)))
  {
    relay1On = ctx->relay1On;
    relay2On = ctx->relay2On;
    doorOpen = ctx->doorOpen;
    xSemaphoreGive(ctx->stateMutex);
  }

  setLocalActuatorState(ctx, relay1On, relay2On, doorOpen, on, speed);
}

void local_set_rgb(AppContext *ctx, uint8_t red, uint8_t green, uint8_t blue)
{
  if (ctx != NULL && lockWithTimeout(ctx->stateMutex, pdMS_TO_TICKS(100)))
  {
    ctx->rgbRed = red;
    ctx->rgbGreen = green;
    ctx->rgbBlue = blue;
    ctx->rgbLedOn = !(red == 0 && green == 0 && blue == 0);

    char hexBuffer[8];
    snprintf(hexBuffer, sizeof(hexBuffer), "#%02X%02X%02X", red, green, blue);
    ctx->rgbHexText = hexBuffer;
    xSemaphoreGive(ctx->stateMutex);
  }

  if (ctx != NULL && ctx->rgbSemaphore != NULL)
    xSemaphoreGive(ctx->rgbSemaphore);
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
  WifiUiState wifiUi;
  WebServer server(80);

  auto startApMode = [&]()
  {
    String apSsid = "ESP32 Local Board";
    String apPassword = "12345678";
    if (ctx != NULL && lockWithTimeout(ctx->configMutex, portMAX_DELAY))
    {
      apSsid = ctx->apSsid;
      apPassword = ctx->apPassword;
      xSemaphoreGive(ctx->configMutex);
    }

    if (WiFi.getMode() != WIFI_AP_STA)
      WiFi.mode(WIFI_AP_STA);

    if (WiFi.softAPIP().toString() == "0.0.0.0")
    {
      WiFi.softAP(apSsid.c_str(), apPassword.c_str());
    }

    wifiUi.isApMode = true;
    wifiUi.connecting = false;
    wifiUi.connectStartMs = 0;
    wifiUi.wifiMessage = "AP mode";

    if (ctx != NULL && lockWithTimeout(ctx->stateMutex, portMAX_DELAY))
    {
      ctx->wifiConnected = false;
      xSemaphoreGive(ctx->stateMutex);
    }

    logWifiLine("AP started. SSID: " + apSsid + " | AP IP: " + WiFi.softAPIP().toString());
  };

  auto beginStation = [&]()
  {
    String wifiSsid;
    String wifiPass;
    if (ctx != NULL && lockWithTimeout(ctx->configMutex, portMAX_DELAY))
    {
      wifiSsid = ctx->wifiSsid;
      wifiPass = ctx->wifiPass;
      xSemaphoreGive(ctx->configMutex);
    }

    if (wifiSsid.isEmpty())
    {
      wifiUi.connecting = false;
      wifiUi.wifiMessage = "Chua co SSID de ket noi";
      logWifiLine("STA skipped because SSID is empty.");
      return;
    }

    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.disconnect(false, false);
    delay(100);

    if (wifiPass.isEmpty())
      WiFi.begin(wifiSsid.c_str());
    else
      WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());

    wifiUi.connecting = true;
    wifiUi.connectStartMs = millis();
    wifiUi.wifiMessage = "Dang thu ket noi WiFi";
    logWifiLine("Trying STA connection to SSID: " + wifiSsid);
  };

  pinMode(BOOT_PIN, INPUT_PULLUP);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);
  fan_init();
  fan_off();

  doorServoDevice().setPeriodHertz(50);
  doorServoDevice().attach(DOOR_PIN, 500, 2400);
  doorServoDevice().write(0);
  setLocalActuatorState(ctx, false, false, false, false, 0);

  if (!LittleFS.begin(true))
    Serial.println("LittleFS mount failed");

  startApMode();
  beginStation();

  server.enableCORS(true);
  server.on("/", HTTP_GET, [&]() { streamFsFile(server, "/index.html", "text/html"); });
  server.on("/index.html", HTTP_GET, [&]() { streamFsFile(server, "/index.html", "text/html"); });
  server.on("/styles.css", HTTP_GET, [&]() { streamFsFile(server, "/styles.css", "text/css"); });
  server.on("/script.js", HTTP_GET, [&]() { streamFsFile(server, "/script.js", "application/javascript"); });
  server.on("/settings", HTTP_GET, [&]() { streamFsFile(server, "/settings.html", "text/html"); });
  server.on("/settings.html", HTTP_GET, [&]() { streamFsFile(server, "/settings.html", "text/html"); });

  server.on("/toggle", HTTP_GET, [&]()
  {
    if (!server.hasArg("led"))
    {
      sendJson(server, "{\"error\":\"Missing led parameter\"}", 400);
      return;
    }

    const int relayIndex = server.arg("led").toInt();
    bool currentState = false;
    if (ctx != NULL && lockWithTimeout(ctx->stateMutex, pdMS_TO_TICKS(100)))
    {
      currentState = relayIndex == 1 ? ctx->relay1On : ctx->relay2On;
      xSemaphoreGive(ctx->stateMutex);
    }

    if (relayIndex == 1 || relayIndex == 2)
    {
      local_set_relay(ctx, static_cast<uint8_t>(relayIndex), !currentState);
      sendJson(server, buildUnifiedStateJson(ctx));
      return;
    }

    sendJson(server, "{\"error\":\"Invalid led value\"}", 400);
  });

  server.on("/door", HTTP_ANY, [&]()
  {
    if (!server.hasArg("state"))
    {
      sendJson(server, "{\"error\":\"missing state\"}", 400);
      return;
    }

    const String doorState = server.arg("state");
    if (doorState == "open")
      local_set_door(ctx, true);
    else if (doorState == "close")
      local_set_door(ctx, false);
    else
    {
      sendJson(server, "{\"error\":\"invalid state\"}", 400);
      return;
    }

    sendJson(server, buildUnifiedStateJson(ctx));
  });

  server.on("/fan", HTTP_ANY, [&]()
  {
    if (server.hasArg("speed"))
    {
      int speed = server.arg("speed").toInt();
      if (speed < 0)
        speed = 0;
      if (speed > 100)
        speed = 100;
      local_set_fan(ctx, speed > 0, static_cast<uint8_t>(speed));
      sendJson(server, buildUnifiedStateJson(ctx));
      return;
    }

    if (!server.hasArg("state"))
    {
      sendJson(server, "{\"error\":\"missing state or speed\"}", 400);
      return;
    }

    const String fanState = server.arg("state");
    if (fanState == "on")
      local_set_fan(ctx, true, 100);
    else if (fanState == "off")
      local_set_fan(ctx, false, 0);
    else if (fanState == "toggle")
    {
      bool currentFanOn = false;
      uint8_t currentFanSpeed = 100;
      if (ctx != NULL && lockWithTimeout(ctx->stateMutex, pdMS_TO_TICKS(100)))
      {
        currentFanOn = ctx->fanOn;
        currentFanSpeed = ctx->fanSpeed == 0 ? 100 : ctx->fanSpeed;
        xSemaphoreGive(ctx->stateMutex);
      }
      local_set_fan(ctx, !currentFanOn, currentFanSpeed);
    }
    else
    {
      sendJson(server, "{\"error\":\"invalid state\"}", 400);
      return;
    }

    sendJson(server, buildUnifiedStateJson(ctx));
  });

  server.on("/rgb", HTTP_ANY, [&]()
  {
    if (!server.hasArg("hex"))
    {
      sendJson(server, "{\"error\":\"missing hex\"}", 400);
      return;
    }

    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    if (!parseHexColor(server.arg("hex"), red, green, blue))
    {
      sendJson(server, "{\"error\":\"invalid hex\"}", 400);
      return;
    }

    local_set_rgb(ctx, red, green, blue);
    sendJson(server, buildUnifiedStateJson(ctx));
  });

  server.on("/remote/door", HTTP_GET, [&]()
  {
    if (!server.hasArg("state"))
    {
      sendJson(server, "{\"error\":\"missing state\"}", 400);
      return;
    }

    const String doorState = server.arg("state");
    uint8_t value = 0;
    if (doorState == "open")
      value = 1;
    else if (doorState == "close")
      value = 0;
    else
    {
      sendJson(server, "{\"error\":\"invalid door state\"}", 400);
      return;
    }

    const bool ok = espnow_send_remote_command(ctx, REMOTE_CMD_DOOR, value);
    if (!ok)
    {
      sendJson(server, String("{\"ok\":") + boolToJson(ok) + "}", 503);
      return;
    }

    sendJson(server, buildUnifiedStateJson(ctx));
  });

  server.on("/remote/fan", HTTP_GET, [&]()
  {
    if (server.hasArg("speed"))
    {
      int speed = server.arg("speed").toInt();
      if (speed < 0)
        speed = 0;
      if (speed > 100)
        speed = 100;
      const bool ok = espnow_send_remote_command(ctx, REMOTE_CMD_FAN_SPEED, static_cast<uint8_t>(speed));
      if (!ok)
      {
        sendJson(server, String("{\"ok\":") + boolToJson(ok) + "}", 503);
        return;
      }

      sendJson(server, buildUnifiedStateJson(ctx));
      return;
    }

    if (!server.hasArg("state"))
    {
      sendJson(server, "{\"error\":\"missing state or speed\"}", 400);
      return;
    }

    const String fanState = server.arg("state");
    uint8_t value = 0;
    if (fanState == "on")
      value = 1;
    else if (fanState == "off")
      value = 0;
    else if (fanState == "toggle")
    {
      bool currentRemoteFan = false;
      if (ctx != NULL && lockWithTimeout(ctx->stateMutex, pdMS_TO_TICKS(100)))
      {
        currentRemoteFan = ctx->remoteFanOn;
        xSemaphoreGive(ctx->stateMutex);
      }
      value = currentRemoteFan ? 0 : 1;
    }
    else
    {
      sendJson(server, "{\"error\":\"invalid fan state\"}", 400);
      return;
    }

    const bool ok = espnow_send_remote_command(ctx, REMOTE_CMD_FAN_POWER, value);
    if (!ok)
    {
      sendJson(server, String("{\"ok\":") + boolToJson(ok) + "}", 503);
      return;
    }

    sendJson(server, buildUnifiedStateJson(ctx));
  });

  server.on("/remote/rgb", HTTP_ANY, [&]()
  {
    if (!server.hasArg("hex"))
    {
      sendJson(server, "{\"error\":\"missing hex\"}", 400);
      return;
    }

    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    if (!parseHexColor(server.arg("hex"), red, green, blue))
    {
      sendJson(server, "{\"error\":\"invalid hex\"}", 400);
      return;
    }

    const bool ok = espnow_send_remote_rgb(ctx, red, green, blue);
    if (!ok)
    {
      sendJson(server, String("{\"ok\":") + boolToJson(ok) + "}", 503);
      return;
    }

    sendJson(server, buildUnifiedStateJson(ctx));
  });

  server.on("/state", HTTP_GET, [&]() { sendJson(server, buildUnifiedStateJson(ctx)); });
  server.on("/sensors", HTTP_GET, [&]() { sendJson(server, buildUnifiedStateJson(ctx)); });

  server.on("/scan_wifi", HTTP_GET, [&]()
  {
    WiFi.scanDelete();
    const int networkCount = WiFi.scanNetworks();
    String json = "[";
    bool first = true;

    for (int index = 0; index < networkCount; ++index)
    {
      const String ssidName = WiFi.SSID(index);
      if (ssidName.isEmpty())
        continue;

      if (!first)
        json += ",";
      first = false;

      json += "{\"ssid\":\"" + ssidName + "\",\"rssi\":" + String(WiFi.RSSI(index)) + ",\"secure\":" + boolToJson(WiFi.encryptionType(index) != WIFI_AUTH_OPEN) + "}";
    }

    json += "]";
    sendJson(server, json);
  });

  server.on("/connect", HTTP_ANY, [&]()
  {
    if (!server.hasArg("ssid"))
    {
      sendJson(server, "{\"ok\":false,\"error\":\"Missing SSID\"}", 400);
      return;
    }

    if (ctx != NULL && lockWithTimeout(ctx->configMutex, portMAX_DELAY))
    {
      ctx->wifiSsid = server.arg("ssid");
      ctx->wifiPass = server.hasArg("pass") ? server.arg("pass") : "";
      xSemaphoreGive(ctx->configMutex);
    }

    beginStation();
    sendJson(server, "{\"ok\":true,\"message\":\"Connecting...\"}");
  });

  server.on("/camera_config", HTTP_ANY, [&]()
  {
    if (server.hasArg("host"))
    {
      const String host = server.arg("host");
      if (ctx != NULL && lockWithTimeout(ctx->configMutex, portMAX_DELAY))
      {
        ctx->cameraHost = host;
        xSemaphoreGive(ctx->configMutex);
      }
      if (ctx != NULL && lockWithTimeout(ctx->stateMutex, portMAX_DELAY))
      {
        ctx->mnistReady = false;
        ctx->mnistDigit = -1;
        ctx->mnistConfidence = 0.0f;
        ctx->mnistStatus = host.isEmpty() ? "Camera host cleared." : "Camera host saved. Waiting for next frame.";
        xSemaphoreGive(ctx->stateMutex);
      }
    }

    sendJson(server, buildUnifiedStateJson(ctx));
  });

  server.on("/espnow_config", HTTP_ANY, [&]()
  {
    if (server.hasArg("peer"))
    {
      const String peerMac = server.arg("peer");
      if (ctx != NULL && lockWithTimeout(ctx->configMutex, portMAX_DELAY))
      {
        ctx->peerMac = peerMac;
        xSemaphoreGive(ctx->configMutex);
      }
      if (ctx != NULL && lockWithTimeout(ctx->stateMutex, portMAX_DELAY))
      {
        ctx->espNowPeerConfigured = !peerMac.isEmpty();
        ctx->espNowStatus = peerMac.isEmpty() ? "Peer MAC cleared." : "Peer MAC saved. Waiting for ESP-NOW link.";
        xSemaphoreGive(ctx->stateMutex);
      }
    }

    sendJson(server, buildUnifiedStateJson(ctx));
  });

  server.on("/wifi_status", HTTP_GET, [&]()
  {
    String wifiSsid;
    bool wifiConnected = false;
    if (ctx != NULL && lockWithTimeout(ctx->configMutex, portMAX_DELAY))
    {
      wifiSsid = ctx->wifiSsid;
      xSemaphoreGive(ctx->configMutex);
    }
    if (ctx != NULL && lockWithTimeout(ctx->stateMutex, portMAX_DELAY))
    {
      wifiConnected = ctx->wifiConnected;
      xSemaphoreGive(ctx->stateMutex);
    }

    String json = "{";
    json += "\"apMode\":" + boolToJson(wifiUi.isApMode) + ",";
    json += "\"connecting\":" + boolToJson(wifiUi.connecting) + ",";
    json += "\"connected\":" + boolToJson(wifiConnected) + ",";
    json += "\"ssid\":\"" + wifiSsid + "\",";
    json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"sta_ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "") + "\",";
    json += "\"message\":\"" + wifiUi.wifiMessage + "\"}";
    sendJson(server, json);
  });

  server.onNotFound([&]() { server.send(404, "text/plain", "404 Not Found"); });
  server.begin();

  while (1)
  {
    server.handleClient();

    if (digitalRead(BOOT_PIN) == LOW)
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      if (digitalRead(BOOT_PIN) == LOW && !wifiUi.isApMode)
      {
        startApMode();
      }
    }

    if (wifiUi.connecting)
    {
      const wl_status_t status = WiFi.status();
      if (status == WL_CONNECTED)
      {
        wifiUi.wifiMessage = "Da ket noi WiFi";
        wifiUi.isApMode = false;
        wifiUi.connecting = false;
        logWifiLine("STA connected. SSID: " + WiFi.SSID() + " | STA IP: " + WiFi.localIP().toString() + " | AP IP: " + WiFi.softAPIP().toString());

        if (ctx != NULL && lockWithTimeout(ctx->stateMutex, portMAX_DELAY))
        {
          ctx->wifiConnected = true;
          xSemaphoreGive(ctx->stateMutex);
        }

        if (ctx != NULL && ctx->internetSemaphore != NULL)
          xSemaphoreGive(ctx->internetSemaphore);
      }
      else if (millis() - wifiUi.connectStartMs > 15000)
      {
        wifiUi.wifiMessage = "Ket noi timeout, giu AP de cau hinh";
        wifiUi.connecting = false;
        wifiUi.isApMode = true;
        logWifiLine("STA connection timed out. Keeping AP mode active.");
      }
    }
    else
    {
      const wl_status_t status = WiFi.status();
      if (status == WL_CONNECTED)
      {
        if (ctx != NULL && lockWithTimeout(ctx->stateMutex, pdMS_TO_TICKS(50)))
        {
          ctx->wifiConnected = true;
          xSemaphoreGive(ctx->stateMutex);
        }
      }
      else
      {
        if (ctx != NULL && lockWithTimeout(ctx->stateMutex, pdMS_TO_TICKS(50)))
        {
          ctx->wifiConnected = false;
          xSemaphoreGive(ctx->stateMutex);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
