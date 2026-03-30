#ifndef FAN_CONTROL_H
#define FAN_CONTROL_H

#include <Arduino.h>

#define FAN_SIG_PIN 18
#define FAN_PWM_CHANNEL 0
#define FAN_PWM_FREQ 25000
#define FAN_PWM_RESOLUTION 8

void fan_init();
void fan_on();
void fan_off();
void fan_set_speed(uint8_t percent);
uint8_t fan_get_speed();

#endif