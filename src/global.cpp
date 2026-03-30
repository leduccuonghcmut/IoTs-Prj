#include "global.h"

// =========================
// RTOS objects
// =========================
QueueHandle_t xSensorQueue = NULL;

SemaphoreHandle_t xLedTempSemaphore = NULL;
SemaphoreHandle_t xNeoHumiSemaphore = NULL;

SemaphoreHandle_t xSemLCDNormal = NULL;
SemaphoreHandle_t xSemLCDWarning = NULL;
SemaphoreHandle_t xSemLCDCritical = NULL;

SemaphoreHandle_t xBinarySemaphoreInternet = NULL;

// =========================
// Current logical states
// =========================
TempLevel g_tempLevel = TEMP_NORMAL;
HumiLevel g_humiLevel = HUMI_NORMAL;
LCDState g_lcdState = LCD_NORMAL;

// =========================
// WiFi / CoreIOT config
// =========================
String WIFI_SSID = "";
String WIFI_PASS = "";
String CORE_IOT_TOKEN = "";
String CORE_IOT_SERVER = "";
String CORE_IOT_PORT = "";

// AP mặc định của ESP32
String ssid = "ESP32-YOUR NETWORK HERE!!!";
String password = "12345678";

// WiFi STA mặc định
String wifi_ssid = "ACLAB";
String wifi_password = "ACLAB2023";

float glob_temperature = 0;
float glob_humidity = 0;

bool isWifiConnected = false;