#include "task_check_info.h"

void Load_info_File(AppContext *ctx)
{
  if (!LittleFS.exists("/info.dat"))
  {
    Serial.println("[INFO] /info.dat not found, using default config");
    return;
  }

  File file = LittleFS.open("/info.dat", "r");
  if (!file)
  {
    return;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, file);
  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    file.close();
    return;
  }

  if (ctx != NULL && ctx->configMutex != NULL && xSemaphoreTake(ctx->configMutex, portMAX_DELAY) == pdTRUE)
  {
    ctx->wifiSsid = doc["WIFI_SSID"] | "";
    ctx->wifiPass = doc["WIFI_PASS"] | "";
    ctx->coreIotToken = doc["CORE_IOT_TOKEN"] | "";
    ctx->coreIotServer = doc["CORE_IOT_SERVER"] | "";
    ctx->coreIotPort = doc["CORE_IOT_PORT"] | "";
    xSemaphoreGive(ctx->configMutex);
  }

  file.close();
}

void Delete_info_File()
{
  if (LittleFS.exists("/info.dat"))
  {
    LittleFS.remove("/info.dat");
  }
  ESP.restart();
}

void Save_info_File(String wifiSsid, String wifiPass, String coreIotToken, String coreIotServer, String coreIotPort)
{
  DynamicJsonDocument doc(4096);
  doc["WIFI_SSID"] = wifiSsid;
  doc["WIFI_PASS"] = wifiPass;
  doc["CORE_IOT_TOKEN"] = coreIotToken;
  doc["CORE_IOT_SERVER"] = coreIotServer;
  doc["CORE_IOT_PORT"] = coreIotPort;

  File configFile = LittleFS.open("/info.dat", "w");
  if (configFile)
  {
    serializeJson(doc, configFile);
    configFile.close();
  }
  else
  {
    Serial.println("Unable to save the configuration.");
  }

  ESP.restart();
}

bool check_info_File(AppContext *ctx, bool check)
{
  if (!check)
  {
    if (!LittleFS.begin(true))
    {
      Serial.println("LittleFS init failed");
      return false;
    }

    Load_info_File(ctx);
  }

  bool missingWifi = true;
  if (ctx != NULL && ctx->configMutex != NULL && xSemaphoreTake(ctx->configMutex, portMAX_DELAY) == pdTRUE)
  {
    missingWifi = ctx->wifiSsid.isEmpty() && ctx->wifiPass.isEmpty();
    xSemaphoreGive(ctx->configMutex);
  }

  return !missingWifi;
}
