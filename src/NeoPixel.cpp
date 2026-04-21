#include "NeoPixel.h"

#include <Adafruit_NeoPixel.h>

#include "global.h"

#ifndef RGB_PIN
#define RGB_PIN 6
#endif

#ifndef RGB_LED_COUNT
#define RGB_LED_COUNT 1
#endif

#ifndef RGB_BRIGHTNESS
#define RGB_BRIGHTNESS 80
#endif

namespace
{
Adafruit_NeoPixel &rgbStrip()
{
    static Adafruit_NeoPixel strip(RGB_LED_COUNT, RGB_PIN, NEO_GRB + NEO_KHZ800);
    return strip;
}

bool &rgbReadyFlag()
{
    static bool ready = false;
    return ready;
}

void updateRgbState(AppContext *ctx, uint8_t red, uint8_t green, uint8_t blue)
{
    if (ctx == NULL || ctx->stateMutex == NULL || xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) != pdTRUE)
        return;

    ctx->rgbRed = red;
    ctx->rgbGreen = green;
    ctx->rgbBlue = blue;
    ctx->rgbLedOn = !(red == 0 && green == 0 && blue == 0);

    char hexBuffer[8];
    snprintf(hexBuffer, sizeof(hexBuffer), "#%02X%02X%02X", red, green, blue);
    ctx->rgbHexText = hexBuffer;

    xSemaphoreGive(ctx->stateMutex);
}

void logExternalRgb(AppContext *ctx, uint8_t red, uint8_t green, uint8_t blue)
{
    if (ctx != NULL && ctx->serialMutex != NULL && xSemaphoreTake(ctx->serialMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        Serial.printf("[EXT-RGB] pin=%d color=(%u,%u,%u)\n", RGB_PIN, red, green, blue);
        xSemaphoreGive(ctx->serialMutex);
        return;
    }

    Serial.printf("[EXT-RGB] pin=%d color=(%u,%u,%u)\n", RGB_PIN, red, green, blue);
}
}

void setNeoPixelColor(uint8_t r, uint8_t g, uint8_t b)
{
    if (!rgbReadyFlag())
        return;

    Adafruit_NeoPixel &strip = rgbStrip();

    for (int index = 0; index < RGB_LED_COUNT; ++index)
    {
        strip.setPixelColor(index, strip.Color(r, g, b));
    }
    strip.show();
}

void NeoPixel(void *pvParameters)
{
    AppContext *ctx = static_cast<AppContext *>(pvParameters);
    Adafruit_NeoPixel &strip = rgbStrip();

    strip.begin();
    strip.setBrightness(RGB_BRIGHTNESS);
    strip.clear();
    strip.show();
    rgbReadyFlag() = true;

    updateRgbState(ctx, 0, 0, 0);

    while (1)
    {
        if (ctx != NULL && ctx->rgbSemaphore != NULL && xSemaphoreTake(ctx->rgbSemaphore, portMAX_DELAY) == pdTRUE)
        {
            uint8_t red = 0;
            uint8_t green = 0;
            uint8_t blue = 0;

            if (ctx->stateMutex != NULL && xSemaphoreTake(ctx->stateMutex, portMAX_DELAY) == pdTRUE)
            {
                red = ctx->rgbRed;
                green = ctx->rgbGreen;
                blue = ctx->rgbBlue;
                xSemaphoreGive(ctx->stateMutex);
            }

            setNeoPixelColor(red, green, blue);
            updateRgbState(ctx, red, green, blue);
            logExternalRgb(ctx, red, green, blue);
        }
    }
}
