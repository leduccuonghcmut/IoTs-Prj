#include "lcd_display_task.h"
#include "global.h"
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x21, 16, 2);

void lcd_display_task(void *pvParameters)
{
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

    // nhận data từ sensor
    if (xSensorQueue != NULL)
    {
      if (xQueueReceive(xSensorQueue, &newData, pdMS_TO_TICKS(200)) == pdTRUE)
      {
        latestData = newData;
      }
    }

    // nhận trạng thái
    if (xSemLCDCritical && xSemaphoreTake(xSemLCDCritical, 0) == pdTRUE)
      currentState = LCD_CRITICAL;
    else if (xSemLCDWarning && xSemaphoreTake(xSemLCDWarning, 0) == pdTRUE)
      currentState = LCD_WARNING;
    else if (xSemLCDNormal && xSemaphoreTake(xSemLCDNormal, 0) == pdTRUE)
      currentState = LCD_NORMAL;

    lcd.clear();

    // ===== DÒNG 1 =====
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

    // ===== DÒNG 2 =====
    lcd.setCursor(0, 1);
    lcd.print("T:");
    lcd.print(latestData.temperature, 1);
    lcd.print(" H:");
    lcd.print(latestData.humidity, 0);
    lcd.print("%   ");  // padding tránh rác

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}