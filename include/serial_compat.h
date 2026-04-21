#ifndef SERIAL_COMPAT_H
#define SERIAL_COMPAT_H

#include <Arduino.h>

#if ARDUINO_USB_CDC_ON_BOOT
#ifndef Serial
#define Serial Serial0
#endif
#endif

#endif
