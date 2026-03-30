#include "mainserver.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ESP32Servo.h>
#include "global.h"
#include "fan_control.h"

Servo doorServo;
WebServer server(80);

#define RELAY1_PIN 16
#define RELAY2_PIN 17
#define DOOR_PIN   5
#define FAN_PIN    18

bool led1_state = false;
bool led2_state = false;
bool isAPMode = true;
bool door_state = false;
bool fan_state = false;
uint8_t fan_speed = 0;

bool connecting = false;
unsigned long connect_start_ms = 0;

String sta_ip = "";
String wifi_message = "AP mode";

static void sendJson(const String &json, int code = 200)
{
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send(code, "application/json", json);
}

static void sendFileFromFS(const char *path, const char *contentType)
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

// =========================
// Device setup
// =========================
void setupDoor()
{
  doorServo.setPeriodHertz(50);
  doorServo.attach(DOOR_PIN, 500, 2400);
  door_state = false;
  doorServo.write(0);
  Serial.println("[DOOR] Servo initialized");
}

void setDoor(bool open)
{
  door_state = open;
  doorServo.write(open ? 90 : 0);
  Serial.printf("[DOOR] %s\n", open ? "OPEN" : "CLOSED");
}

void setupRelay()
{
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);

  digitalWrite(RELAY1_PIN, LOW);
  digitalWrite(RELAY2_PIN, LOW);

  led1_state = false;
  led2_state = false;
}

void setupFan()
{
  fan_init();
  fan_state = false;
  fan_speed = 0;
  Serial.println("[FAN] PWM initialized");
}

void setFan(bool on)
{
  fan_state = on;

  if (on)
  {
    if (fan_speed == 0) fan_speed = 100;
    fan_set_speed(fan_speed);
  }
  else
  {
    fan_speed = 0;
    fan_off();
  }

  Serial.printf("[FAN] %s | speed=%d%%\n", on ? "ON" : "OFF", fan_speed);
}

void setFanSpeed(uint8_t percent)
{
  if (percent > 100) percent = 100;

  fan_speed = percent;

  if (fan_speed == 0)
  {
    fan_state = false;
    fan_off();
  }
  else
  {
    fan_state = true;
    fan_set_speed(fan_speed);
  }

  Serial.printf("[FAN] speed set to %d%%\n", fan_speed);
}

// =========================
// WiFi
// =========================
void startAP()
{
  WiFi.disconnect(false, false);
  delay(200);

  WiFi.mode(WIFI_AP_STA);

  bool ok = WiFi.softAP(ssid.c_str(), password.c_str());
  Serial.printf("[WIFI] softAP start: %s\n", ok ? "OK" : "FAIL");

  Serial.print("[WIFI] AP IP: ");
  Serial.println(WiFi.softAPIP());

  isAPMode = true;
  connecting = false;
  isWifiConnected = false;
  sta_ip = "";
  wifi_message = "AP mode";
}

void connectToWiFi()
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  if (wifi_password.isEmpty())
    WiFi.begin(wifi_ssid.c_str());
  else
    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

  Serial.print("[WIFI] Connecting to SSID: ");
  Serial.println(wifi_ssid);
}

// =========================
// JSON state helpers
// =========================
String buildDeviceStateJson()
{
  String json = "{";
  json += "\"led1\":" + String(led1_state ? "true" : "false") + ",";
  json += "\"led2\":" + String(led2_state ? "true" : "false") + ",";
  json += "\"door\":\"" + String(door_state ? "open" : "closed") + "\",";
  json += "\"fan_on\":" + String(fan_state ? "true" : "false") + ",";
  json += "\"fan_speed\":" + String(fan_speed) + ",";
  json += "\"fan\":\"" + String(fan_state ? "ON" : "OFF") + "\"";
  json += "}";
  return json;
}

// =========================
// HTTP Handlers
// =========================
void handleRoot()
{
  sendFileFromFS("/index.html", "text/html");
}

void handleStyle()
{
  sendFileFromFS("/styles.css", "text/css");
}

void handleScript()
{
  sendFileFromFS("/script.js", "application/javascript");
}

void handleSettings()
{
  sendFileFromFS("/settings.html", "text/html");
}

void handleToggle()
{
  if (!server.hasArg("led"))
  {
    sendJson("{\"error\":\"Missing led parameter\"}", 400);
    return;
  }

  int led = server.arg("led").toInt();

  if (led == 1)
  {
    led1_state = !led1_state;
    digitalWrite(RELAY1_PIN, led1_state ? HIGH : LOW);
    Serial.printf("LED1 -> %s\n", led1_state ? "ON" : "OFF");
  }
  else if (led == 2)
  {
    led2_state = !led2_state;
    digitalWrite(RELAY2_PIN, led2_state ? HIGH : LOW);
    Serial.printf("LED2 -> %s\n", led2_state ? "ON" : "OFF");
  }
  else
  {
    sendJson("{\"error\":\"Invalid led value\"}", 400);
    return;
  }

  String json = "{";
  json += "\"led1\":\"" + String(led1_state ? "ON" : "OFF") + "\",";
  json += "\"led2\":\"" + String(led2_state ? "ON" : "OFF") + "\"";
  json += "}";

  sendJson(json);
}

void handleDoor()
{
  if (!server.hasArg("state"))
  {
    sendJson("{\"error\":\"missing state\"}", 400);
    return;
  }

  String state = server.arg("state");

  if (state == "open")
    setDoor(true);
  else if (state == "close")
    setDoor(false);
  else
  {
    sendJson("{\"error\":\"invalid state\"}", 400);
    return;
  }

  String response = "{";
  response += "\"door\":\"";
  response += (door_state ? "open" : "closed");
  response += "\"";
  response += "}";

  sendJson(response);
}

void handleFan()
{
  if (server.hasArg("speed"))
  {
    int speed = server.arg("speed").toInt();

    if (speed < 0) speed = 0;
    if (speed > 100) speed = 100;

    setFanSpeed((uint8_t)speed);

    String response = "{";
    response += "\"fan\":\"";
    response += (fan_state ? "on" : "off");
    response += "\",";
    response += "\"fan_on\":";
    response += (fan_state ? "true" : "false");
    response += ",";
    response += "\"speed\":";
    response += String(fan_speed);
    response += ",";
    response += "\"fan_speed\":";
    response += String(fan_speed);
    response += "}";

    sendJson(response);
    return;
  }

  if (!server.hasArg("state"))
  {
    sendJson("{\"error\":\"missing state or speed\"}", 400);
    return;
  }

  String state = server.arg("state");

  if (state == "on")
  {
    setFan(true);
  }
  else if (state == "off")
  {
    setFan(false);
  }
  else if (state == "toggle")
  {
    setFan(!fan_state);
  }
  else
  {
    sendJson("{\"error\":\"invalid state\"}", 400);
    return;
  }

  String response = "{";
  response += "\"fan\":\"";
  response += (fan_state ? "on" : "off");
  response += "\",";
  response += "\"fan_on\":";
  response += (fan_state ? "true" : "false");
  response += ",";
  response += "\"speed\":";
  response += String(fan_speed);
  response += ",";
  response += "\"fan_speed\":";
  response += String(fan_speed);
  response += "}";

  sendJson(response);
}

void handleState()
{
  sendJson(buildDeviceStateJson());
}

void handleSensors()
{
  String json = "{";
  json += "\"temp\":" + String(glob_temperature, 1) + ",";
  json += "\"hum\":" + String(glob_humidity, 1) + ",";
  json += "\"led1\":\"" + String(led1_state ? "ON" : "OFF") + "\",";
  json += "\"led2\":\"" + String(led2_state ? "ON" : "OFF") + "\",";
  json += "\"door\":\"" + String(door_state ? "open" : "closed") + "\",";
  json += "\"fan\":\"" + String(fan_state ? "ON" : "OFF") + "\",";
  json += "\"fan_on\":" + String(fan_state ? "true" : "false") + ",";
  json += "\"fan_speed\":" + String(fan_speed) + ",";
  json += "\"fanSpeed\":" + String(fan_speed);
  json += "}";

  sendJson(json);
}

void handleScanWifi()
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

    json += "{";
    json += "\"ssid\":\"" + ssidName + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
    json += "}";
  }

  json += "]";
  sendJson(json);
}

void handleConnect()
{
  if (!server.hasArg("ssid"))
  {
    sendJson("{\"ok\":false,\"error\":\"Missing SSID\"}", 400);
    return;
  }

  wifi_ssid = server.arg("ssid");
  wifi_password = server.hasArg("pass") ? server.arg("pass") : "";

  if (wifi_ssid.length() == 0)
  {
    sendJson("{\"ok\":false,\"error\":\"SSID is empty\"}", 400);
    return;
  }

  Serial.println("[WIFI] Receive config from web");
  Serial.println("[WIFI] SSID: " + wifi_ssid);
  Serial.println("[WIFI] PASS: " + wifi_password);

  connecting = true;
  connect_start_ms = millis();
  wifi_message = "Connecting...";
  isWifiConnected = false;

  sendJson("{\"ok\":true,\"message\":\"Connecting...\"}");

  delay(100);
  connectToWiFi();
}

void handleWifiStatus()
{
  String json = "{";
  json += "\"apMode\":" + String(isAPMode ? "true" : "false") + ",";
  json += "\"connecting\":" + String(connecting ? "true" : "false") + ",";
  json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"ssid\":\"" + wifi_ssid + "\",";
  json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"sta_ip\":\"" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "") + "\",";
  json += "\"message\":\"" + wifi_message + "\"";
  json += "}";

  sendJson(json);
}

void handleNotFound()
{
  server.send(404, "text/plain", "404 Not Found");
}

// =========================
// Server setup
// =========================
void setupServer()
{
  server.enableCORS(true);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/styles.css", HTTP_GET, handleStyle);
  server.on("/script.js", HTTP_GET, handleScript);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/settings.html", HTTP_GET, handleSettings);

  server.on("/scan_wifi", HTTP_GET, handleScanWifi);
  server.on("/toggle", HTTP_GET, handleToggle);
  server.on("/sensors", HTTP_GET, handleSensors);
  server.on("/state", HTTP_GET, handleState);
  server.on("/door", HTTP_ANY, handleDoor);
  server.on("/fan", HTTP_ANY, handleFan);

  server.on("/connect", HTTP_GET, handleConnect);
  server.on("/connect", HTTP_POST, handleConnect);

  server.on("/wifi_status", HTTP_GET, handleWifiStatus);

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[HTTP] server started");
}

// =========================
// Main task
// =========================
void main_server_task(void *pvParameters)
{
  pinMode(BOOT_PIN, INPUT_PULLUP);

  if (!LittleFS.begin(true))
    Serial.println("LittleFS mount failed");
  else
    Serial.println("LittleFS mounted");

  if (xBinarySemaphoreInternet == NULL)
    xBinarySemaphoreInternet = xSemaphoreCreateBinary();

  setupDoor();
  setupRelay();
  setupFan();
  startAP();
  setupServer();

  while (1)
  {
    server.handleClient();

    if (digitalRead(BOOT_PIN) == LOW)
    {
      vTaskDelay(pdMS_TO_TICKS(100));
      if (digitalRead(BOOT_PIN) == LOW)
      {
        if (!isAPMode)
        {
          Serial.println("[WIFI] BOOT pressed -> back to AP mode");
          startAP();
        }
      }
    }

    if (connecting)
    {
      wl_status_t status = WiFi.status();

      if (status == WL_CONNECTED)
      {
        sta_ip = WiFi.localIP().toString();
        wifi_message = "Connected";
        isWifiConnected = true;
        isAPMode = false;
        connecting = false;

        Serial.print("[WIFI] STA IP: ");
        Serial.println(sta_ip);

        if (xBinarySemaphoreInternet != NULL)
          xSemaphoreGive(xBinarySemaphoreInternet);
      }
      else if (millis() - connect_start_ms > 15000)
      {
        Serial.println("[WIFI] Connect timeout");
        wifi_message = "Connect timeout";
        connecting = false;
        isWifiConnected = false;

        WiFi.disconnect(false, false);
        WiFi.mode(WIFI_AP_STA);
        isAPMode = true;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}