#ifndef ESPNOW_LINK_H
#define ESPNOW_LINK_H

#include "global.h"

void espnow_link_task(void *pvParameters);
bool espnow_send_remote_command(AppContext *ctx, RemoteCommandType commandType, uint8_t value);
bool espnow_send_remote_rgb(AppContext *ctx, uint8_t red, uint8_t green, uint8_t blue);

#endif
