#include "global.h"

#include "led_blinky.h"
#include "neo_blinky.h"
#include "temp_humi_monitor.h"
#include "mainserver.h"
#include "tinyml.h"
#include "coreiot.h"
#include "lcd_display_task.h"
// include task
#include "task_check_info.h"
#include "task_toogle_boot.h"
// #include "task_wifi.h"
// #include "task_webserver.h"
#include "task_core_iot.h"

void setup()
{
  Serial.begin(115200);
  check_info_File(0);

  xSensorQueue = xQueueCreate(1, sizeof(SensorData));

  xSemLCDNormal = xSemaphoreCreateBinary();
  xSemLCDWarning = xSemaphoreCreateBinary();
  xSemLCDCritical = xSemaphoreCreateBinary();
  xBinarySemaphoreInternet = xSemaphoreCreateBinary();

  xLedTempSemaphore = xSemaphoreCreateBinary();
  xNeoHumiSemaphore = xSemaphoreCreateBinary();

  xTaskCreate(temp_humi_monitor, "Task TEMP HUMI Monitor", 4096, NULL, 2, NULL);
  xTaskCreate(lcd_display_task, "Task LCD Display", 4096, NULL, 2, NULL);
  xTaskCreate(main_server_task, "Task Main Server", 8192, NULL, 2, NULL);
  xTaskCreate(coreiot_task, "CoreIOT Task", 4096, NULL, 2, NULL);
  xTaskCreate(led_blinky, "Task LED Blink", 2048, NULL, 2, NULL);
  xTaskCreate(neo_blinky, "Task NEO Blink", 2048, NULL, 2, NULL);
}

void loop()
{
  // if (check_info_File(1))
  // {
  //   if (!Wifi_reconnect())
  //   {
  //     Webserver_stop();
  //   }
  //   else
  //   {
  //     //CORE_IOT_reconnect();
  //   }
  // }
  // Webserver_reconnect();
}