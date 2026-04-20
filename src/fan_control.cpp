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

void fan_init()
{
    ledcSetup(FAN_PWM_CHANNEL, FAN_PWM_FREQ, FAN_PWM_RESOLUTION);
    ledcAttachPin(FAN_SIG_PIN, FAN_PWM_CHANNEL);
    ledcWrite(FAN_PWM_CHANNEL, 0);
}

void fan_off()
{
    ledcWrite(FAN_PWM_CHANNEL, 0);
}

void fan_on()
{
    ledcWrite(FAN_PWM_CHANNEL, 255);
}

void fan_set_speed(uint8_t percent)
{
    if (percent > 100) percent = 100;

    int duty = map(percent, 0, 100, 0, 255);
    ledcWrite(FAN_PWM_CHANNEL, duty);
}

uint8_t fan_get_speed()
{
    int duty = ledcRead(FAN_PWM_CHANNEL);
    return static_cast<uint8_t>(map(duty, 0, 255, 0, 100));
}
