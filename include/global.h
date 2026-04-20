#ifndef GLOBAL_H
#define GLOBAL_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

typedef struct
{
    float temperature;
    float humidity;
} SensorData;

enum TempLevel
{
    TEMP_NORMAL = 0,
    TEMP_WARNING,
    TEMP_CRITICAL
};

enum HumiLevel
{
    HUMI_DRY = 0,
    HUMI_NORMAL,
    HUMI_WET
};

enum LCDState
{
    LCD_NORMAL = 0,
    LCD_WARNING,
    LCD_CRITICAL
};

enum TinyMLState
{
    TINYML_IDLE = 0,
    TINYML_NORMAL,
    TINYML_WARNING,
    TINYML_ANOMALY
};

typedef struct
{
    QueueHandle_t sensorQueue;
    SemaphoreHandle_t ledTempSemaphore;
    SemaphoreHandle_t neoHumiSemaphore;
    SemaphoreHandle_t semLCDNormal;
    SemaphoreHandle_t semLCDWarning;
    SemaphoreHandle_t semLCDCritical;
    SemaphoreHandle_t internetSemaphore;
    SemaphoreHandle_t stateMutex;
    SemaphoreHandle_t configMutex;
    SemaphoreHandle_t serialMutex;

    TempLevel tempLevel;
    HumiLevel humiLevel;
    LCDState lcdState;
    TinyMLState tinymlState;

    bool wifiConnected;
    bool tinymlReady;
    bool mnistReady;

    float temperature;
    float humidity;
    float tinymlScore;
    float mnistConfidence;
    int mnistDigit;

    String wifiSsid;
    String wifiPass;
    String coreIotToken;
    String coreIotServer;
    String coreIotPort;
    String cameraHost;
    String mnistStatus;

    String apSsid;
    String apPassword;
} AppContext;

#endif
