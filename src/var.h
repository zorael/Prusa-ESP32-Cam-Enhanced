/**
   @file variable.h

   @brief Library with global variables

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   @bug: no know bug
*/

#pragma once

#include <Arduino.h>
#include "mcu_cfg.h"

struct WebBasicAuth_struct {
  bool EnableAuth;                        ///< user definition for enable/disable basic auth
  String UserName;                        ///< login name for basic auth
  String Password;                        ///< password for basic auth
};


struct McuTemperature_struct {
  float TemperatureCelsius;               ///< MCU temperature
};

extern struct WebBasicAuth_struct WebBasicAuth;      ///< structure with configuration for basic auth
extern struct McuTemperature_struct McuTemperature;  ///< MCU temperature

extern TaskHandle_t Task_CapturePhotoAndSend;        ///< task handle for capture photo and send
extern TaskHandle_t Task_SystemMain;                 ///< task handle for system main
extern TaskHandle_t Task_Housekeeping;               ///< task handle for the merged periodic housekeeping task

enum CaptureCommand_t : uint8_t {
  CMD_CAPTURE_ONLY      = 0,
  CMD_CAPTURE_AND_SEND  = 1,
  CMD_TL_START          = 2,  ///< begin a new timelapse segment
  CMD_TL_STOP           = 3,  ///< finalise and close current timelapse
  CMD_UPLOAD_DISABLE    = 4,  ///< disable Prusa Connect uploads (print ended)
  CMD_UPLOAD_ENABLE     = 5,  ///< re-enable Prusa Connect uploads (print started)
  CMD_TL_FRAME          = 6,  ///< capture one timelapse frame now (layer change); no Prusa Connect upload
};

extern QueueHandle_t g_captureQueue;

/* EOF */