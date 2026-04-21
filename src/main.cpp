#include "global.h"

#include "led_blinky.h"
#include "NeoPixel.h"
#include "neo_blinky.h"
#include "temp_humi_monitor.h"
#include "mainserver.h"
#include "espnow_link.h"
#include "lcd_display_task.h"
#include "task_check_info.h"
#include "task_toogle_boot.h"
#include "task_core_iot.h"

void setup()
{
  Serial.begin(115200);
  AppContext *ctx = new AppContext{};

  ctx->sensorQueue = xQueueCreate(1, sizeof(SensorData));
  ctx->semLCDNormal = xSemaphoreCreateBinary();
  ctx->semLCDWarning = xSemaphoreCreateBinary();
  ctx->semLCDCritical = xSemaphoreCreateBinary();
  ctx->internetSemaphore = xSemaphoreCreateBinary();
  ctx->ledTempSemaphore = xSemaphoreCreateBinary();
  ctx->neoHumiSemaphore = xSemaphoreCreateBinary();
  ctx->rgbSemaphore = xSemaphoreCreateBinary();
  ctx->stateMutex = xSemaphoreCreateMutex();
  ctx->configMutex = xSemaphoreCreateMutex();
  ctx->serialMutex = xSemaphoreCreateMutex();
  ctx->i2cMutex = xSemaphoreCreateMutex();

  ctx->tempLevel = TEMP_NORMAL;
  ctx->humiLevel = HUMI_NORMAL;
  ctx->lcdState = LCD_NORMAL;
  ctx->tinymlState = TINYML_IDLE;
  ctx->wifiConnected = false;
  ctx->tinymlReady = false;
  ctx->mnistReady = false;
  ctx->relay1On = false;
  ctx->relay2On = false;
  ctx->doorOpen = false;
  ctx->fanOn = false;
  ctx->rgbLedOn = false;
  ctx->remoteOnline = false;
  ctx->espNowReady = false;
  ctx->espNowPeerConfigured = false;
  ctx->remoteDoorOpen = false;
  ctx->remoteFanOn = false;
  ctx->remoteRgbOn = false;
  ctx->temperature = 0.0f;
  ctx->humidity = 0.0f;
  ctx->tinymlScore = 0.0f;
  ctx->mnistConfidence = 0.0f;
  ctx->remoteTemperature = 0.0f;
  ctx->remoteHumidity = 0.0f;
  ctx->mnistDigit = -1;
  ctx->rgbRed = 0;
  ctx->rgbGreen = 0;
  ctx->rgbBlue = 0;
  ctx->fanSpeed = 0;
  ctx->remoteFanSpeed = 0;
  ctx->remoteRgbRed = 0;
  ctx->remoteRgbGreen = 0;
  ctx->remoteRgbBlue = 0;
  ctx->remoteLastSeenMs = 0;
  ctx->espNowPacketsRx = 0;
  ctx->espNowPacketsTx = 0;
  ctx->coreIotToken = "y8m225l6zv297aarday2";
  ctx->coreIotServer = "app.coreiot.io";
  ctx->coreIotPort = "1883";
  ctx->cameraHost = "";
  ctx->mnistStatus = "Camera host not configured.";
  ctx->rgbHexText = "#000000";
  ctx->peerMac = "";
  ctx->localMac = "";
  ctx->espNowStatus = "ESP-NOW not initialized.";
  ctx->remoteBoardName = "Remote Board";
  ctx->remoteRgbHexText = "#000000";
  ctx->apSsid = "ESP32 yolo kit cua bao!!!";
  ctx->apPassword = "12345678";
  ctx->wifiSsid = "ACLAB";
  ctx->wifiPass = "ACLAB2023";

  check_info_File(ctx, false);

  xTaskCreate(temp_humi_monitor, "Task TEMP HUMI Monitor", 4096, ctx, 2, NULL);
  xTaskCreate(lcd_display_task, "Task LCD Display", 4096, ctx, 2, NULL);
  xTaskCreate(main_server_task, "Task Main Server", 8192, ctx, 2, NULL);
  xTaskCreate(espnow_link_task, "ESP-NOW Link", 8192, ctx, 2, NULL);
  xTaskCreate(coreiot_thingsboard_task, "CoreIOT Task", 8192, ctx, 2, NULL);
  xTaskCreate(led_blinky, "Task LED Blink", 2048, ctx, 2, NULL);
}

void loop()
{
}
