#ifndef GLOBAL_H
#define GLOBAL_H

#include "serial_compat.h"
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

enum RemoteCommandType
{
    REMOTE_CMD_NONE = 0,
    REMOTE_CMD_RELAY1,
    REMOTE_CMD_RELAY2,
    REMOTE_CMD_DOOR,
    REMOTE_CMD_FAN_POWER,
    REMOTE_CMD_FAN_SPEED,
    REMOTE_CMD_RGB
};

typedef struct
{
    QueueHandle_t sensorQueue;
    SemaphoreHandle_t ledTempSemaphore;
    SemaphoreHandle_t neoHumiSemaphore;
    SemaphoreHandle_t rgbSemaphore;
    SemaphoreHandle_t semLCDNormal;
    SemaphoreHandle_t semLCDWarning;
    SemaphoreHandle_t semLCDCritical;
    SemaphoreHandle_t internetSemaphore;
    SemaphoreHandle_t stateMutex;
    SemaphoreHandle_t configMutex;
    SemaphoreHandle_t serialMutex;
    SemaphoreHandle_t i2cMutex;

    TempLevel tempLevel;
    HumiLevel humiLevel;
    LCDState lcdState;
    TinyMLState tinymlState;

    bool wifiConnected;
    bool tinymlReady;
    bool mnistReady;
    bool relay1On;
    bool relay2On;
    bool doorOpen;
    bool fanOn;
    bool rgbLedOn;
    bool remoteOnline;
    bool espNowReady;
    bool espNowPeerConfigured;
    bool remoteDoorOpen;
    bool remoteFanOn;
    bool remoteRgbOn;

    float temperature;
    float humidity;
    float tinymlScore;
    float mnistConfidence;
    float remoteTemperature;
    float remoteHumidity;
    int mnistDigit;
    uint8_t rgbRed;
    uint8_t rgbGreen;
    uint8_t rgbBlue;
    uint8_t fanSpeed;
    uint8_t remoteFanSpeed;
    uint8_t remoteRgbRed;
    uint8_t remoteRgbGreen;
    uint8_t remoteRgbBlue;
    uint32_t remoteLastSeenMs;
    uint32_t espNowPacketsRx;
    uint32_t espNowPacketsTx;

    String wifiSsid;
    String wifiPass;
    String coreIotToken;
    String coreIotServer;
    String coreIotPort;
    String cameraHost;
    String mnistStatus;
    String rgbHexText;
    String peerMac;
    String localMac;
    String espNowStatus;
    String remoteBoardName;
    String remoteRgbHexText;

    String apSsid;
    String apPassword;
} AppContext;

#endif
