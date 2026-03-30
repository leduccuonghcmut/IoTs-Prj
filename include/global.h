#ifndef GLOBAL_H
#define GLOBAL_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// =========================
// Shared sensor packet
// =========================
typedef struct
{
    float temperature;
    float humidity;
} SensorData;

// =========================
// Task 1 - Temperature LED
// =========================
enum TempLevel
{
    TEMP_NORMAL = 0,
    TEMP_WARNING,
    TEMP_CRITICAL
};

// =========================
// Task 2 - Humidity NeoPixel
// =========================
enum HumiLevel
{
    HUMI_DRY = 0,
    HUMI_NORMAL,
    HUMI_WET
};

// =========================
// Task 3 - LCD state
// =========================
enum LCDState
{
    LCD_NORMAL = 0,
    LCD_WARNING,
    LCD_CRITICAL
};

// =========================
// RTOS objects
// =========================

// Queue truyền dữ liệu cảm biến cho các task
extern QueueHandle_t xSensorQueue;

// Semaphore cho Task 1
extern SemaphoreHandle_t xLedTempSemaphore;

// Semaphore cho Task 2
extern SemaphoreHandle_t xNeoHumiSemaphore;

// Semaphore cho Task 3
extern SemaphoreHandle_t xSemLCDNormal;
extern SemaphoreHandle_t xSemLCDWarning;
extern SemaphoreHandle_t xSemLCDCritical;

// Semaphore Internet/CoreIOT
extern SemaphoreHandle_t xBinarySemaphoreInternet;

// =========================
// Current logical states
// =========================
extern TempLevel g_tempLevel;
extern HumiLevel g_humiLevel;
extern LCDState g_lcdState;

// =========================
// WiFi / CoreIOT config
// =========================
extern String WIFI_SSID;
extern String WIFI_PASS;
extern String CORE_IOT_TOKEN;
extern String CORE_IOT_SERVER;
extern String CORE_IOT_PORT;

extern String ssid;
extern String password;

extern String wifi_ssid;
extern String wifi_password;

extern bool isWifiConnected;

extern float glob_temperature;
extern float glob_humidity;

#endif