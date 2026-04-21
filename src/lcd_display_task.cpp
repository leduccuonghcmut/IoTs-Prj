#include "lcd_display_task.h"
#include "global.h"
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

void lcd_display_task(void *pvParameters)
{
  AppContext *ctx = static_cast<AppContext *>(pvParameters);
  LiquidCrystal_I2C lcd(0x21, 16, 2);

  delay(500);
  Wire.begin(11, 12);
  delay(100);

  lcd.begin();
  lcd.backlight();
  lcd.clear();

  SensorData latestData = {0.0f, 0.0f};
  LCDState currentState = LCD_NORMAL;

  while (1)
  {
    SensorData newData;

    if (ctx != NULL && ctx->sensorQueue != NULL)
    {
      if (xQueueReceive(ctx->sensorQueue, &newData, pdMS_TO_TICKS(200)) == pdTRUE)
      {
        latestData = newData;
      }
    }

    if (ctx != NULL)
    {
      if (ctx->semLCDCritical && xSemaphoreTake(ctx->semLCDCritical, 0) == pdTRUE)
        currentState = LCD_CRITICAL;
      else if (ctx->semLCDWarning && xSemaphoreTake(ctx->semLCDWarning, 0) == pdTRUE)
        currentState = LCD_WARNING;
      else if (ctx->semLCDNormal && xSemaphoreTake(ctx->semLCDNormal, 0) == pdTRUE)
        currentState = LCD_NORMAL;
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    switch (currentState)
    {
      case LCD_NORMAL:
        lcd.print("STATUS:NORMAL ");
        break;
      case LCD_WARNING:
        lcd.print("STATUS:WARNING");
        break;
      case LCD_CRITICAL:
        lcd.print("STATUS:CRITICAL");
        break;
    }

    lcd.setCursor(0, 1);
    lcd.print("T:");
    lcd.print(latestData.temperature, 1);
    lcd.print(" H:");
    lcd.print(latestData.humidity, 1);
    lcd.print("%   ");

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
