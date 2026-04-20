#include "neo_blinky.h"
#include "global.h"

void neo_blinky(void *pvParameters)
{
    AppContext *ctx = static_cast<AppContext *>(pvParameters);
    Adafruit_NeoPixel strip(LED_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

    strip.begin();
    strip.clear();
    strip.show();

    uint8_t r = 255, g = 180, b = 0; 
    int delayMs = 500;

    while (1)
    {
        if (ctx != NULL && ctx->neoHumiSemaphore != NULL)
        {
            if (xSemaphoreTake(ctx->neoHumiSemaphore, 0) == pdTRUE)
            {
                HumiLevel currentLevel = HUMI_NORMAL;
                if (ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
                {
                    currentLevel = ctx->humiLevel;
                    xSemaphoreGive(ctx->stateMutex);
                }

                switch (currentLevel)
                {
                    case HUMI_DRY:
                        r = 255; g = 0; b = 0;
                        delayMs = 200; 
                        Serial.println("[NEO] DRY -> RED FAST");
                        break;

                    case HUMI_NORMAL:
                        r = 255; g = 180; b = 0;
                        delayMs = 500; 
                        Serial.println("[NEO] NORMAL -> YELLOW");
                        break;

                    case HUMI_WET:
                        r = 0; g = 255; b = 0;
                        delayMs = 1000; 
                        Serial.println("[NEO] WET -> GREEN SLOW");
                        break;
                }
            }
        }

        // ===== BLINK ON =====
        strip.setPixelColor(0, strip.Color(r, g, b));
        strip.show();
        vTaskDelay(pdMS_TO_TICKS(delayMs));

        // ===== BLINK OFF =====
        strip.setPixelColor(0, strip.Color(0, 0, 0));
        strip.show();
        vTaskDelay(pdMS_TO_TICKS(delayMs));
    }
}
