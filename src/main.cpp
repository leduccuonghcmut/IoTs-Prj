#include "global.h"

#include "led_blinky.h"
#include "neo_blinky.h"
#include "temp_humi_monitor.h"
#include "mainserver.h"
#include "tinyml.h"
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
  ctx->stateMutex = xSemaphoreCreateMutex();
  ctx->configMutex = xSemaphoreCreateMutex();

  ctx->tempLevel = TEMP_NORMAL;
  ctx->humiLevel = HUMI_NORMAL;
  ctx->lcdState = LCD_NORMAL;
  ctx->tinymlState = TINYML_IDLE;
  ctx->wifiConnected = false;
  ctx->tinymlReady = false;
  ctx->temperature = 0.0f;
  ctx->humidity = 0.0f;
  ctx->tinymlScore = 0.0f;
  ctx->coreIotToken = "y8m225l6zv297aarday2";
  ctx->coreIotServer = "app.coreiot.io";
  ctx->coreIotPort = "1883";
  ctx->apSsid = "ESP32-YOUR NETWORK HERE!!!";
  ctx->apPassword = "12345678";
  ctx->wifiSsid = "ACLAB";
  ctx->wifiPass = "ACLAB2023";

  check_info_File(ctx, false);

  xTaskCreate(temp_humi_monitor, "Task TEMP HUMI Monitor", 4096, ctx, 2, NULL);
  xTaskCreate(lcd_display_task, "Task LCD Display", 4096, ctx, 2, NULL);
  xTaskCreate(main_server_task, "Task Main Server", 8192, ctx, 2, NULL);
  xTaskCreate(tiny_ml_task, "Tiny ML Task", 8192, ctx, 2, NULL);
  xTaskCreate(coreiot_thingsboard_task, "CoreIOT Task", 8192, ctx, 2, NULL);
  xTaskCreate(led_blinky, "Task LED Blink", 2048, ctx, 2, NULL);
  xTaskCreate(neo_blinky, "Task NEO Blink", 2048, ctx, 2, NULL);
}

void loop()
{
}
