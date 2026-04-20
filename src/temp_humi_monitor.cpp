#include "temp_humi_monitor.h"
#include "global.h"

static LCDState classifyState(float t, float h)
{
  if (t >= 35.0f || h < 25.0f || h > 85.0f)
    return LCD_CRITICAL;

  if ((t >= 30.0f && t < 35.0f) || h < 40.0f || h > 70.0f)
    return LCD_WARNING;

  return LCD_NORMAL;
}

static HumiLevel classifyHumidity(float h)
{
  if (h < 40.0f)
    return HUMI_DRY;
  if (h <= 70.0f)
    return HUMI_NORMAL;
  return HUMI_WET;
}

void temp_humi_monitor(void *pvParameters)
{
  AppContext *ctx = static_cast<AppContext *>(pvParameters);
  DHT20 dht20;
  Wire.begin(11, 12);
  dht20.begin();

  LCDState lastLCDState = (LCDState)(-1);
  HumiLevel lastHumiLevel = (HumiLevel)(-1);

  while (1)
  {
    dht20.read();

    float temperature = dht20.getTemperature();
    float humidity = dht20.getHumidity();

    if (isnan(temperature) || isnan(humidity))
    {
      Serial.println("Failed to read from DHT20 sensor!");
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    if (ctx != NULL && ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
    {
      ctx->temperature = temperature;
      ctx->humidity = humidity;
      xSemaphoreGive(ctx->stateMutex);
    }

    SensorData data;
    data.temperature = temperature;
    data.humidity = humidity;

    if (ctx != NULL && ctx->sensorQueue != NULL)
    {
      xQueueOverwrite(ctx->sensorQueue, &data);
    }

    LCDState newLCDState = classifyState(temperature, humidity);
    HumiLevel newHumiLevel = classifyHumidity(humidity);

    // cập nhật NeoPixel ngay cả lần đầu
    if (newHumiLevel != lastHumiLevel)
    {
      if (ctx != NULL && ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
      {
        ctx->humiLevel = newHumiLevel;
        xSemaphoreGive(ctx->stateMutex);
      }

      if (ctx != NULL && ctx->neoHumiSemaphore != NULL)
      {
        xSemaphoreGive(ctx->neoHumiSemaphore);
      }

      lastHumiLevel = newHumiLevel;
    }

    // cập nhật LCD theo trạng thái mới
    if (newLCDState != lastLCDState)
    {
      switch (newLCDState)
      {
        case LCD_NORMAL:
          if (ctx != NULL && ctx->semLCDNormal != NULL) xSemaphoreGive(ctx->semLCDNormal);
          break;

        case LCD_WARNING:
          if (ctx != NULL && ctx->semLCDWarning != NULL) xSemaphoreGive(ctx->semLCDWarning);
          break;

        case LCD_CRITICAL:
          if (ctx != NULL && ctx->semLCDCritical != NULL) xSemaphoreGive(ctx->semLCDCritical);
          break;
      }

      if (ctx != NULL && ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
      {
        ctx->lcdState = newLCDState;
        xSemaphoreGive(ctx->stateMutex);
      }

      lastLCDState = newLCDState;
    }

    Serial.print("Temp: ");
    Serial.print(temperature, 1);
    Serial.print(" C | Humi: ");
    Serial.print(humidity, 1);
    Serial.print(" % | Level: ");

    switch (newHumiLevel)
    {
      case HUMI_DRY:
        Serial.println("DRY");
        break;
      case HUMI_NORMAL:
        Serial.println("NORMAL");
        break;
      case HUMI_WET:
        Serial.println("WET");
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
