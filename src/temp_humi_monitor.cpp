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

static const char *humiLevelToString(HumiLevel level)
{
  switch (level)
  {
    case HUMI_DRY:
      return "DRY";
    case HUMI_NORMAL:
      return "NORMAL";
    case HUMI_WET:
    default:
      return "WET";
  }
}

void temp_humi_monitor(void *pvParameters)
{
  AppContext *ctx = static_cast<AppContext *>(pvParameters);
  DHT20 dht20;
  if (ctx != NULL && ctx->i2cMutex != NULL && xSemaphoreTake(ctx->i2cMutex, pdMS_TO_TICKS(500)) == pdTRUE)
  {
    Wire.begin(21, 22);
    dht20.begin();
    xSemaphoreGive(ctx->i2cMutex);
  }

  LCDState lastLCDState = (LCDState)(-1);
  HumiLevel lastHumiLevel = (HumiLevel)(-1);

  while (1)
  {
    float temperature = NAN;
    float humidity = NAN;

    if (ctx != NULL && ctx->i2cMutex != NULL && xSemaphoreTake(ctx->i2cMutex, pdMS_TO_TICKS(500)) == pdTRUE)
    {
      dht20.read();
      temperature = dht20.getTemperature();
      humidity = dht20.getHumidity();
      xSemaphoreGive(ctx->i2cMutex);
    }

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

    if (ctx != NULL && ctx->serialMutex != NULL && xSemaphoreTake(ctx->serialMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      Serial.printf("Temp: %.1f C | Humi: %.1f %% | Level: %s\n",
                    temperature,
                    humidity,
                    humiLevelToString(newHumiLevel));
      xSemaphoreGive(ctx->serialMutex);
    }
    else
    {
      Serial.printf("Temp: %.1f C | Humi: %.1f %% | Level: %s\n",
                    temperature,
                    humidity,
                    humiLevelToString(newHumiLevel));
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
