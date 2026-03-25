#include "mainserver.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "global.h"

bool led1_state = false;
bool led2_state = false;
bool isAPMode = true;

WebServer server(80);

unsigned long connect_start_ms = 0;
bool connecting = false;

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

// ========= Handlers =========
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
    server.send(400, "application/json", "{\"error\":\"Missing led parameter\"}");
    return;
  }

  int led = server.arg("led").toInt();

  if (led == 1)
  {
    led1_state = !led1_state;
    Serial.printf("LED1 -> %s\n", led1_state ? "ON" : "OFF");
  }
  else if (led == 2)
  {
    led2_state = !led2_state;
    Serial.printf("LED2 -> %s\n", led2_state ? "ON" : "OFF");
  }
  else
  {
    server.send(400, "application/json", "{\"error\":\"Invalid led value\"}");
    return;
  }

  String json = "{";
  json += "\"led1\":\"" + String(led1_state ? "ON" : "OFF") + "\",";
  json += "\"led2\":\"" + String(led2_state ? "ON" : "OFF") + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleSensors()
{
  String json = "{";
  json += "\"temp\":" + String(glob_temperature, 1) + ",";
  json += "\"hum\":" + String(glob_humidity, 1) + ",";
  json += "\"led1\":\"" + String(led1_state ? "ON" : "OFF") + "\",";
  json += "\"led2\":\"" + String(led2_state ? "ON" : "OFF") + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleConnect()
{
  if (!server.hasArg("ssid"))
  {
    server.send(400, "text/plain", "Missing SSID");
    return;
  }

  wifi_ssid = server.arg("ssid");
  wifi_password = server.hasArg("pass") ? server.arg("pass") : "";

  server.send(200, "text/plain", "Connecting...");
  isAPMode = false;
  connecting = true;
  connect_start_ms = millis();
  connectToWiFi();
}

void handleNotFound()
{
  server.send(404, "text/plain", "404 Not Found");
}

// ========= WiFi / Server =========
void setupServer()
{
  server.enableCORS(true);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/styles.css", HTTP_GET, handleStyle);
  server.on("/script.js", HTTP_GET, handleScript);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/settings.html", HTTP_GET, handleSettings);

  server.on("/toggle", HTTP_GET, handleToggle);
  server.on("/sensors", HTTP_GET, handleSensors);
  server.on("/connect", HTTP_GET, handleConnect);

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void startAP()
{
  WiFi.disconnect(true, true);
  delay(200);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid.c_str(), password.c_str());

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  isAPMode = true;
  connecting = false;
}

void connectToWiFi()
{
  WiFi.disconnect(true, true);
  delay(200);

  WiFi.mode(WIFI_STA);

  if (wifi_password.isEmpty())
    WiFi.begin(wifi_ssid.c_str());
  else
    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

  Serial.print("Connecting to: ");
  Serial.print(wifi_ssid);
  Serial.print(" | Password: ");
  Serial.println(wifi_password);
}

// ========= Main task =========
void main_server_task(void *pvParameters)
{
  pinMode(BOOT_PIN, INPUT_PULLUP);

  if (!LittleFS.begin(true))
  {
    Serial.println("LittleFS mount failed");
  }
  else
  {
    Serial.println("LittleFS mounted");
  }

  if (xBinarySemaphoreInternet == NULL)
  {
    xBinarySemaphoreInternet = xSemaphoreCreateBinary();
  }

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
          Serial.println("BOOT pressed -> switching back to AP mode");
          startAP();
        }
      }
    }

    if (connecting)
    {
      if (WiFi.status() == WL_CONNECTED)
      {
        Serial.print("STA IP address: ");
        Serial.println(WiFi.localIP());

        isWifiConnected = true;
        if (xBinarySemaphoreInternet != NULL)
        {
          xSemaphoreGive(xBinarySemaphoreInternet);
        }

        isAPMode = false;
        connecting = false;
      }
      else if (millis() - connect_start_ms > 10000)
      {
        Serial.println("WiFi connect timeout, back to AP mode");
        startAP();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}