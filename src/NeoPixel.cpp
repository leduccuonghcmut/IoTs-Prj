#include "NeoPixel.h"

#include <Adafruit_NeoPixel.h>

#include "global.h"

#ifndef RGB_PIN
#define RGB_PIN 6
#endif

#ifndef RGB_LED_COUNT
#define RGB_LED_COUNT 1
#endif

namespace
{
Adafruit_NeoPixel &rgbStrip()
{
    static Adafruit_NeoPixel strip(RGB_LED_COUNT, RGB_PIN, NEO_GRB + NEO_KHZ800);
    return strip;
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
}

void setNeoPixelColor(uint8_t r, uint8_t g, uint8_t b)
{
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
    strip.setBrightness(80);
    strip.clear();
    strip.show();

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
        }
    }
}
