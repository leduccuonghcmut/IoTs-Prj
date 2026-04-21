#include "coreiot.h"

void coreiot_task(void *pvParameters)
{
  AppContext *ctx = static_cast<AppContext *>(pvParameters);
  WiFiClient espClient;
  PubSubClient client(espClient);

  while (1)
  {
    if (ctx != NULL && ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
    {
      float temperature = ctx->temperature;
      float humidity = ctx->humidity;
      xSemaphoreGive(ctx->stateMutex);

      if (client.connected())
      {
        String payload = "{\"temperature\":" + String(temperature) + ",\"humidity\":" + String(humidity) + "}";
        client.publish("v1/devices/me/telemetry", payload.c_str());
      }
    }

    client.loop();
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}
