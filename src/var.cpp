/**
   @file variable.cpp

   @brief Library with global variables

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   @bug: no know bug
*/

#include "var.h"

WebBasicAuth_struct WebBasicAuth = { false, "", "" };
struct McuTemperature_struct McuTemperature = {0.0};

TaskHandle_t Task_CapturePhotoAndSend;
TaskHandle_t Task_SystemMain;
TaskHandle_t Task_Housekeeping;

QueueHandle_t g_captureQueue = NULL;

/* EOF */