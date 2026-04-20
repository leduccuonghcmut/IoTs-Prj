#include "task_rs485.h"

#define DELAY_CONNECT_MS 100
#define TXD_RS485 9
#define RXD_RS485 10

static void sendRS485Command(HardwareSerial &rs485Serial, byte *command, int commandSize, byte *response, int responseSize)
{
    rs485Serial.write(command, commandSize);
    rs485Serial.flush();
    delay(100);
    if (rs485Serial.available() >= responseSize)
    {
        rs485Serial.readBytes(response, responseSize);
    }
    else
    {
        Serial.println("Failed to read response");
    }
}

static void sendModbusCommand(const uint8_t command[], size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        Serial2.write(command[i]);
    }
    delay(DELAY_CONNECT_MS);
}

static void sensorReadTask(void *pvParameters)
{
    HardwareSerial *rs485Serial = static_cast<HardwareSerial *>(pvParameters);
    while (true)
    {
        float sound = 0.0f;
        float pressure = 0.0f;
        byte response[7] = {};
        byte soundRequest[] = {0x06, 0x03, 0x01, 0xF6, 0x00, 0x01, 0x64, 0x73};
        byte pressureRequest[] = {0x06, 0x03, 0x01, 0xF9, 0x00, 0x01, 0x54, 0x70};

        sendRS485Command(*rs485Serial, soundRequest, sizeof(soundRequest), response, sizeof(response));
        if (response[1] == 0x03)
            sound = ((response[3] << 8) | response[4]) / 10.0f;

        memset(response, 0, sizeof(response));
        sendRS485Command(*rs485Serial, pressureRequest, sizeof(pressureRequest), response, sizeof(response));
        if (response[1] == 0x03)
            pressure = ((response[3] << 8) | response[4]) / 10.0f;

        Serial.println("sound : " + String(sound));
        Serial.println("pressure: " + String(pressure));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void relayTask(void *pvParameters)
{
    (void)pvParameters;
    const uint8_t relay_ON[][8] = {
        {1, 5, 0, 0, 255, 0, 140, 58},
        {1, 5, 0, 1, 255, 0, 221, 250},
        {1, 5, 0, 2, 255, 0, 45, 250},
        {1, 5, 0, 3, 255, 0, 124, 58}};
    const uint8_t relay_OFF[][8] = {
        {1, 5, 0, 0, 0, 0, 205, 202},
        {1, 5, 0, 1, 0, 0, 156, 10},
        {1, 5, 0, 2, 0, 0, 108, 10},
        {1, 5, 0, 3, 0, 0, 61, 202}};
    bool state = false;

    while (true)
    {
        const auto &commands = state ? relay_OFF : relay_ON;
        for (int i = 0; i < 4; i++)
        {
            sendModbusCommand(commands[i], sizeof(commands[i]));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        state = !state;
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void tasksensor_init()
{
    HardwareSerial *rs485Serial = new HardwareSerial(1);
    rs485Serial->begin(9600, SERIAL_8N1, TXD_RS485, RXD_RS485);
    xTaskCreate(sensorReadTask, "Task_Read_Sensor", 4096, rs485Serial, 1, NULL);
    xTaskCreate(relayTask, "Task_Send_data", 4096, NULL, 1, NULL);
}
