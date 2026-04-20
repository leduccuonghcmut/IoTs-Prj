// #include "led_blinky.h"

// void led_blinky(void *pvParameters){
//   pinMode(LED_GPIO, OUTPUT);
  
//   while(1) {                        
//     digitalWrite(LED_GPIO, HIGH);  // turn the LED ON
//     vTaskDelay(1000);
//     digitalWrite(LED_GPIO, LOW);  // turn the LED OFF
//     vTaskDelay(1000);
//   }
// }

#include "led_blinky.h"
#include "global.h"

#ifndef LED_GPIO
#define LED_GPIO 48
#endif

void led_blinky(void *pvParameters)
{
  AppContext *ctx = static_cast<AppContext *>(pvParameters);
  pinMode(LED_GPIO, OUTPUT);

  uint32_t onTime = 1000;
  uint32_t offTime = 1000;

  while (1)
  {
    if (ctx != NULL && ctx->ledTempSemaphore != NULL && xSemaphoreTake(ctx->ledTempSemaphore, 0) == pdTRUE)
    {
      TempLevel currentLevel = TEMP_NORMAL;
      if (ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
      {
        currentLevel = ctx->tempLevel;
        xSemaphoreGive(ctx->stateMutex);
      }

      switch (currentLevel)
      {
        case TEMP_NORMAL:
          onTime = 1000;
          offTime = 1000;
          Serial.println("LED mode: NORMAL - blink slow");
          break;

        case TEMP_WARNING:
          onTime = 300;
          offTime = 300;
          Serial.println("LED mode: WARNING - blink medium");
          break;

        case TEMP_CRITICAL:
          onTime = 100;
          offTime = 100;
          Serial.println("LED mode: CRITICAL - blink fast");
          break;
      }
    }

    digitalWrite(LED_GPIO, HIGH);
    vTaskDelay(pdMS_TO_TICKS(onTime));

    digitalWrite(LED_GPIO, LOW);
    vTaskDelay(pdMS_TO_TICKS(offTime));
  }
}
