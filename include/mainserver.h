#ifndef ___MAIN_SERVER__
#define ___MAIN_SERVER__
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "global.h"

#define LED1_PIN 2
#define LED2_PIN 4
#define BOOT_PIN 0
//extern WebServer server;

//extern bool isAPMode;




String mainPage();
String settingsPage();

void startAP();
void setupServer();
void connectToWiFi();
void local_set_relay(AppContext *ctx, uint8_t relayIndex, bool on);
void local_set_door(AppContext *ctx, bool open);
void local_set_fan(AppContext *ctx, bool on, uint8_t speed);
void local_set_rgb(AppContext *ctx, uint8_t red, uint8_t green, uint8_t blue);

void main_server_task(void *pvParameters);

#endif
