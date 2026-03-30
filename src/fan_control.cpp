#include "fan_control.h"
#include <Arduino.h>

#ifndef FAN_SIG_PIN
#define FAN_SIG_PIN 18
#endif

#ifndef FAN_PWM_CHANNEL
#define FAN_PWM_CHANNEL 0
#endif

#ifndef FAN_PWM_FREQ
#define FAN_PWM_FREQ 25000
#endif

#ifndef FAN_PWM_RESOLUTION
#define FAN_PWM_RESOLUTION 8
#endif

static uint8_t g_fan_speed_percent = 0;

void fan_init()
{
    ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQ, FAN_PWM_RESOLUTION);
    ledcAttachPin(FAN_SIG_PIN, FAN_PWM_CHANNEL);
    ledcWrite(FAN_PWM_CHANNEL, 0);
    g_fan_speed_percent = 0;
}

void fan_off()
{
    g_fan_speed_percent = 0;
    ledcWrite(FAN_PWM_CHANNEL, 0);
}

void fan_on()
{
    g_fan_speed_percent = 100;
    ledcWrite(FAN_PWM_CHANNEL, 255);
}

void fan_set_speed(uint8_t percent)
{
    if (percent > 100) percent = 100;

    g_fan_speed_percent = percent;

    int duty = map(percent, 0, 100, 0, 255);
    ledcWrite(FAN_PWM_CHANNEL, duty);
}

uint8_t fan_get_speed()
{
    return g_fan_speed_percent;
}