/**
   @file system.h

   @brief system library

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   @bug: no know bug
*/

#pragma once

#include <WiFi.h>
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>

#if defined(CONFIG_IDF_TARGET_ESP32)
    #include "esp32/rom/rtc.h"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    #include "esp32s3/rom/rtc.h"
#else
    #error "Unsupported chip target"
#endif

#include "mcu_cfg.h"
#include "var.h"
#include "cfg.h"
#include "log.h"
#include "wifi_mngt.h"
#include "connect.h"
#include "serial_cfg.h"
#include "sys_led.h"
#include "prusa_link.h"
#include "ota.h"
#include "timelapse.h"

void System_Init();
bool System_CheckIfPsramIsUsed();

String System_PrintMcuResetReason(int);
String System_printMcuResetReasonSimple();

/* Three tasks, down from eight.
   The five former housekeeping tasks (WiFi management, SD card check, serial cfg,
   telemetry, WiFi watchdog, LED) were each an idle loop around a periodic call,
   costing a stack + TCB + a task-watchdog subscription apiece — roughly 14 kB of
   DRAM to do a few seconds of work per minute. System_TaskHousekeeping runs all of
   them off one 100 ms tick at their original periods. Capture/send stays separate
   because it blocks for seconds on TLS and is pinned to the non-WiFi core. */
void System_TaskMain(void *);
void System_TaskCaptureAndSendPhoto(void *);
void System_TaskHousekeeping(void *);

/* EOF */