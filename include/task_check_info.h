#ifndef __TASK_CHECK_INFO_H__
#define __TASK_CHECK_INFO_H__

#include <ArduinoJson.h>
#include "LittleFS.h"
#include "global.h"

bool check_info_File(AppContext *ctx, bool check);
void Load_info_File(AppContext *ctx);
void Delete_info_File();
void Save_info_File(String wifiSsid, String wifiPass, String coreIotToken, String coreIotServer, String coreIotPort);

#endif
