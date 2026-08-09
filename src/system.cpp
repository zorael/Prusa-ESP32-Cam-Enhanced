/**
   @file system.cpp

   @brief system library

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   @bug: no know bug
*/

#include "system.h"

/**
   @brief Function for init system library
   @param none
   @return none
*/
void System_Init() {
  SystemLog.AddEvent(LogLevel_Info, F("Init system lib"));
  SystemLog.AddEvent(LogLevel_Info, "SW Version: " + String(SW_VERSION) + " Build: " + String(SW_BUILD));
  SystemLog.AddEvent(LogLevel_Info, "Board name: " + String(BOARD_NAME));

  /* show last reset status */
  String reason_simple = System_printMcuResetReasonSimple();
  SystemLog.AddEvent(LogLevel_Warning, "CPU reset reason: " + reason_simple);

  String reason_core0 = System_PrintMcuResetReason(rtc_get_reset_reason(0));
  String reason_core1 = System_PrintMcuResetReason(rtc_get_reset_reason(1));
  SystemLog.AddEvent(LogLevel_Warning, "CPU0 reset reason: " + reason_core0);
  SystemLog.AddEvent(LogLevel_Warning, "CPU1 reset reason: " + reason_core1);

  SystemLog.AddEvent(LogLevel_Info, "MCU Temperature: " + String(temperatureRead()) + " *C");
  SystemLog.AddEvent(LogLevel_Info, "Internal Total heap: " + String(ESP.getHeapSize()) + " B, Internal Free Heap: " + String(ESP.getFreeHeap()));
  SystemLog.AddEvent(LogLevel_Info, "PSRAM Total heap: " + String(ESP.getPsramSize()) + " B, PSRAM Free Heap: " + String(ESP.getFreePsram()));
  SystemLog.AddEvent(LogLevel_Info, "Chip model: " + String(ESP.getChipModel()) + ", ChipRevision: " + String(ESP.getChipRevision()) + ", Cpu Freq: " + String(ESP.getCpuFreqMHz()));
  SystemLog.AddEvent(LogLevel_Info, "SDK Version: " + String(ESP.getSdkVersion()));
  SystemLog.AddEvent(LogLevel_Info, "Flash Size: " + String(ESP.getFlashChipSize()) + ", Flash Speed " + String(ESP.getFlashChipSpeed()) + ", Flash Mode: " + String(ESP.getFlashChipMode()));
  
  System_CheckIfPsramIsUsed();
}

/**
   @brief Function for check if PSRAM is used
   @param none
   @return none
*/
bool System_CheckIfPsramIsUsed() {
  bool ret = false;
  if (psramFound()) {
    SystemLog.AddEvent(LogLevel_Info, F("PSRAM is used."));
    ret = true;
    void *ptr = malloc(100);

    if (ptr != NULL) {
      if (esp_ptr_external_ram(ptr)) {
        SystemLog.AddEvent(LogLevel_Info, F("malloc/new is using SPIRAM"));
      } else {
        SystemLog.AddEvent(LogLevel_Info, F("malloc/new is not using SPIRAM"));
      }
      free(ptr);
    } else {
      SystemLog.AddEvent(LogLevel_Info, F("Failed to allocate memory"));
    }
  } else {
    SystemLog.AddEvent(LogLevel_Info, F("PSRAM is not used."));
  }

  return ret;
}

/**
   @brief Function for print reset reason
   @param int - reset reason
   @return String - reset reason
*/
String System_PrintMcuResetReason(int reason) {
  String ret = "";
  switch (reason) {
    case 1: /**<1,  Vbat power on reset*/
      ret = F("POWERON_RESET");
      break;
    case 3: /**<3,  Software reset digital core*/
      ret = F("SW_RESET");
      break;
    case 4: /**<4,  Legacy watch dog reset digital core*/
      ret = F("OWDT_RESET");
      break;
    case 5: /**<5,  Deep Sleep reset digital core*/
      ret = F("DEEPSLEEP_RESET");
      break;
    case 6: /**<6,  Reset by SLC module, reset digital core*/
      ret = F("SDIO_RESET");
      break;
    case 7: /**<7,  Timer Group0 Watch dog reset digital core*/
      ret = F("TG0WDT_SYS_RESET");
      break;
    case 8: /**<8,  Timer Group1 Watch dog reset digital core*/
      ret = F("TG1WDT_SYS_RESET");
      break;
    case 9: /**<9,  RTC Watch dog Reset digital core*/
      ret = F("RTCWDT_SYS_RESET");
      break;
    case 10: /**<10, Instrusion tested to reset CPU*/
      ret = F("INTRUSION_RESET");
      break;
    case 11: /**<11, Time Group reset CPU*/
      ret = F("TGWDT_CPU_RESET");
      break;
    case 12: /**<12, Software reset CPU*/
      ret = F("SW_CPU_RESET");
      break;
    case 13: /**<13, RTC Watch dog Reset CPU*/
      ret = F("RTCWDT_CPU_RESET");
      break;
    case 14: /**<14, for APP CPU, reseted by PRO CPU*/
      ret = F("EXT_CPU_RESET");
      break;
    case 15: /**<15, Reset when the vdd voltage is not stable*/
      ret = F("RTCWDT_BROWN_OUT_RESET");
      break;
    case 16: /**<16, RTC Watch dog reset digital core and rtc module*/
      ret = F("RTCWDT_RTC_RESET");
      break;
    default:
      ret = F("NO_MEAN");
  }

  return ret;
}

/**
   @brief Function for print reset reason
   @param none
   @return String - reset reason
*/
String System_printMcuResetReasonSimple() {
  String ret = "";
  esp_reset_reason_t reason = esp_reset_reason();

  switch (reason) {
    case ESP_RST_UNKNOWN:
      ret = F("Reset reason can not be determined");
      break;
    case ESP_RST_POWERON:
      ret = F("Reset due to power-on event");
      break;
    case ESP_RST_EXT:
      ret = F("Reset by external pin (not applicable for ESP32)");
      break;
    case ESP_RST_SW:
      ret = F("Software reset via esp_restart");
      break;
    case ESP_RST_PANIC:
      ret = F("Software reset due to exception/panic");
      break;
    case ESP_RST_INT_WDT:
      ret = F("Reset (software or hardware) due to interrupt watchdog");
      break;
    case ESP_RST_TASK_WDT:
      ret = F("Reset due to task watchdog");
      break;
    case ESP_RST_WDT:
      ret = F("Reset due to other watchdogs");
      break;
    case ESP_RST_DEEPSLEEP:
      ret = F("Reset after exiting deep sleep mode");
      break;
    case ESP_RST_BROWNOUT:
      ret = F("Brownout reset (software or hardware)");
      break;
    case ESP_RST_SDIO:
      ret = F("Reset over SDIO");
      break;
    default:
      ret = F("N/A");
      break;
  }

  return ret;
}

/**
 * @brief Function for main system task
 * 
 * @param void *pvParameters
 * @return none
 */
void System_TaskMain(void *pvParameters) {
  SystemLog.AddEvent(LogLevel_Info, F("System task. core: "), String(xPortGetCoreID()));
  TickType_t xLastWakeTime = xTaskGetTickCount();

  static uint16_t btnTicks     = 0;
  static bool     btnWasDown   = false;

  while (1) {
    esp_task_wdt_reset();

    /* --- button: short press (1–8 ticks) toggles timelapse; long press ignored at runtime --- */
    bool btnNow = (digitalRead(CFG_RESET_PIN) == LOW);
    if (btnNow) {
      btnTicks++;
    } else if (btnWasDown) {
      /* btnTicks * 100ms: valid press is 100ms–8s (1–80 ticks at 100ms/tick) */
      if (btnTicks >= 1 && btnTicks < 80) {
        CaptureCommand_t btnCmd;
        if (SystemTimelapse.isRecording()) {
          SystemLog.AddEvent(LogLevel_Info, F("Button: queuing TL_STOP"));
          btnCmd = CMD_TL_STOP;
          /* TL_STOP must not be dropped — send to front with short timeout */
          xQueueSendToFront(g_captureQueue, &btnCmd, pdMS_TO_TICKS(200));
        } else {
          SystemLog.AddEvent(LogLevel_Info, F("Button: queuing TL_START"));
          btnCmd = CMD_TL_START;
          xQueueSend(g_captureQueue, &btnCmd, 0);
        }
        /* no immediate flash here — SetFlashStatus drives LEDC from Core 1
           (capture task); calling it from Core 0 without a mutex races the PWM
           register. The 2/3-flash feedback from begin()/stop() is the signal. */
      }
      btnTicks = 0;
    }
    btnWasDown = btnNow;

    /* PrusaLink polling used to live here. It moved to System_TaskHousekeeping:
       Poll() blocks on HTTP for up to 1.5 s, and layer-change detection needs a
       5 s poll interval while printing, which would repeatedly stall the 100 ms
       button tick this task exists to serve. */

    SystemLog.AddEvent(LogLevel_Verbose, F("System task. Stack free size: "), String(uxTaskGetStackHighWaterMark(NULL)) + "B");

    /* reset wdg */
    esp_task_wdt_reset();

    /* next start task */
    vTaskDelayUntil(&xLastWakeTime, TASK_SYSTEM / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Function for capture and send photo task
 * 
 * @param void *pvParameters
 * @return none
 */
void System_TaskCaptureAndSendPhoto(void *pvParameters) {
  SystemLog.AddEvent(LogLevel_Info, F("Task photo processing. core: "), String(xPortGetCoreID()));
  TickType_t xLastWakeTime = xTaskGetTickCount();

  bool prevWifiConnected = false;
  bool tlRestored        = false;

  while (1) {
    /* drain all queued commands posted by web handlers, PrusaLink, or button */
    CaptureCommand_t cmd;
    bool doSend          = false;
    bool doCapture       = false;
    bool doTlStart       = false;
    bool doTlStop        = false;
    bool doUploadDisable = false;
    bool doUploadEnable  = false;
    uint8_t tlFrames     = 0;   ///< counted, not flagged: two layers can land in one drain
    while (xQueueReceive(g_captureQueue, &cmd, 0) == pdTRUE) {
      switch (cmd) {
        case CMD_CAPTURE_ONLY:     doCapture       = true; break;
        case CMD_CAPTURE_AND_SEND: doSend          = true; break;
        case CMD_TL_START:         doTlStart       = true; break;
        case CMD_TL_STOP:          doTlStop        = true; break;
        case CMD_UPLOAD_DISABLE:   doUploadDisable = true; break;
        case CMD_UPLOAD_ENABLE:    doUploadEnable  = true; break;
        case CMD_TL_FRAME:         if (tlFrames < 4) tlFrames++; break;
        default: break;
      }
    }

    /* printer gate — set by PrusaLink print state, independent of the user upload_enable toggle */
    if (doUploadDisable) { Connect.SetPrinterGate(false); SystemLog.AddEvent(LogLevel_Info, F("Printer gate closed — print ended")); }
    if (doUploadEnable)  { Connect.SetPrinterGate(true);  SystemLog.AddEvent(LogLevel_Info, F("Printer gate open — print started")); }

    /* interval ticks regardless of WiFi — capture and timelapse must not stall when offline */
    bool wifiNow = (WL_CONNECTED == WiFi.status());
    if (wifiNow && !prevWifiConnected) {
      if (!tlRestored) {
        tlRestored = true;
        SystemTimelapse.restoreFromEeprom();
      }
    }
    prevWifiConnected = wifiNow;

    if (Connect.CheckSendingIntervalExpired()) {
      Connect.SetSendingIntervalCounter(0);
      doSend = true;
    } else {
      Connect.IncreaseSendingIntervalCounter();
    }

    /* open any recording that was deferred until NTP/job-name were available */
    SystemTimelapse.servicePendingStart();

    /* timelapse control — no WiFi dependency.
       Stop before start so a same-tick stop+start correctly sequences. */
    if (doTlStop) {
      esp_task_wdt_reset();
      SystemTimelapse.stop();
    }
    if (doTlStart) {
      esp_task_wdt_reset();
      SystemTimelapse.begin();
    }

    /* capture runs regardless of WiFi; skip only when stream or AVI download is active */
    if (doSend) {
      /* Downloads no longer blanket-suppress capture — that cost a live print its
         frames. But letting the full cycle run during a download is measurably
         expensive: capture + TLS upload competes for WiFi, CPU and PSRAM and drops
         transfer throughput from ~218 KB/s to ~64 KB/s.
         So the rule is priority, not exclusion: while recording (a print is live)
         capture always wins and the download takes the hit; while idle there is
         nothing worth interrupting the transfer for, so the cycle stands down. */
      if (SystemCamera.GetStreamStatus()) {
        SystemLog.AddEvent(LogLevel_Info, F("Stream active — skipping capture"));
      } else if (SystemTimelapse.isDownloadActive() && !SystemTimelapse.isRecording()) {
        SystemLog.AddEvent(LogLevel_Info, F("Download active and printer idle — deferring capture"));
      } else {
        /* device-info PUT is a pure network call — only attempt when online */
        if (wifiNow) {
          SystemLog.AddEvent(LogLevel_Verbose, F("Task photo processing. Start sending info"));
          esp_task_wdt_reset();
          Connect.SendInfoToBackend();
        }
        SystemLog.AddEvent(LogLevel_Verbose, F("Task photo processing. Capture + send"));
        esp_task_wdt_reset();
        Connect.TakePictureAndSendToBackend();
      }
    } else if (doCapture) {
      SystemLog.AddEvent(LogLevel_Verbose, F("Task photo processing. Capture only"));
      esp_task_wdt_reset();
      SystemCamera.CapturePhoto();
    }

    /* --- layer-triggered timelapse frame (PrusaLink axis_z) ---
       Deliberately independent of the Prusa Connect cycle: a layer change must
       produce a frame whether or not an upload is due, and must not cause one.
       Skipped while streaming or downloading for the same reason capture is. */
    if (tlFrames > 0) {
      if (!SystemTimelapse.isRecording()) {
        SystemLog.AddEvent(LogLevel_Verbose, F("Layer frame ignored — not recording"));
      } else if (SystemCamera.GetStreamStatus()) {
        /* Only the stream still blocks a layer frame — it owns the camera. A download
           does not, and dropping a layer to finish a file transfer is the wrong trade. */
        SystemLog.AddEvent(LogLevel_Info, F("Layer frame skipped — stream active"));
      } else {
        /* Coalesce: if several layers were detected inside one task period the
           camera can only show the newest one, so capture once. */
        if (tlFrames > 1) {
          SystemLog.AddEvent(LogLevel_Info, "Coalescing " + String(tlFrames) + " layer frames into 1");
        }
        esp_task_wdt_reset();
        SystemCamera.CapturePhoto();
        if (SystemCamera.GetCameraCaptureSuccess()) {
          /* hold the frame buffer for the append — CaptureStream() on the AsyncTCP
             task would otherwise recycle it mid-write, same race as the upload path */
          if (xSemaphoreTake(SystemCamera.GetFrameBufferSemaphore(),
                             pdMS_TO_TICKS(CAMERA_SNAPSHOT_WAIT_MS)) == pdTRUE) {
            esp_task_wdt_reset();
            SystemTimelapse.appendFrame(SystemCamera.GetPhotoFb()->buf, SystemCamera.GetPhotoFb()->len);
            xSemaphoreGive(SystemCamera.GetFrameBufferSemaphore());
            SystemLog.AddEvent(LogLevel_Info, "Layer frame appended, total " + String(SystemTimelapse.frameCount()));
          } else {
            SystemLog.AddEvent(LogLevel_Error, F("Layer frame: frame buffer busy"));
          }
        } else {
          SystemLog.AddEvent(LogLevel_Error, F("Layer frame capture failed"));
        }
      }
    }

    SystemLog.AddEvent(LogLevel_Verbose, F("Photo processing task. Stack free size: "), String(uxTaskGetStackHighWaterMark(NULL)) + "B");

    /* reset wdg */
    esp_task_wdt_reset();

    /* next start task */
    vTaskDelayUntil(&xLastWakeTime, TASK_PHOTO_SEND / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Single periodic housekeeping task.
 *
 * Replaces six near-identical tasks (WiFi management, SD card check, serial cfg,
 * telemetry, LED, WiFi watchdog). Each was a `while(1) { work(); vTaskDelayUntil(); }`
 * shell costing its own stack, TCB and task-watchdog subscription — about 14 kB of
 * DRAM and six WDT subscribers to perform a few seconds of work per minute.
 *
 * Everything runs off one 100 ms tick at its original period. Ordering is fixed and
 * only one job runs per tick where possible, so the slow jobs (SD, WiFi scan) cannot
 * stack up behind each other within a single watchdog window.
 *
 * @param void *pvParameters
 * @return none
 */
void System_TaskHousekeeping(void *pvParameters) {
  SystemLog.AddEvent(LogLevel_Info, F("Housekeeping task. core: "), String(xPortGetCoreID()));
  TickType_t xLastWakeTime = xTaskGetTickCount();

  /* elapsed-ms accumulators, one per former task */
  uint32_t tLed = 0, tSerial = 0, tWifi = 0, tWdg = 0, tSd = 0, tTelemetry = 0, tPrusa = 0;

  while (1) {
    esp_task_wdt_reset();

    /* NTP first-sync, on its own cadence rather than the 28 s WiFi tick. Cheap:
       a single bool test once synced. */
    SystemWifiMngt.ServiceNtpSync(TASK_HOUSEKEEPING);

    /* --- OTA: run any user-requested check/install here, never on AsyncTCP.
           Idle unless the user pressed a button, so this costs one flag test. --- */
    if (SystemOta.Busy()) {
      esp_task_wdt_reset();
      SystemOta.Service();
      esp_task_wdt_reset();
    }

    /* --- PrusaLink poll. Interval is adaptive: 5 s while printing in layer-frame
           mode so layers are not missed, 30 s otherwise. --- */
    tPrusa += TASK_HOUSEKEEPING;
    if (tPrusa >= SystemPrusaLink.PollIntervalMs()) {
      tPrusa = 0;
      esp_task_wdt_reset();  /* Poll() blocks on HTTP for up to 1.5 s */
      SystemPrusaLink.Poll();
      esp_task_wdt_reset();
    }

    /* --- system LED (period is dynamic: reflects WiFi/AP state) --- */
    tLed += TASK_HOUSEKEEPING;
    if (tLed >= system_led.getTimer()) {
      tLed = 0;
      system_led.toggle();
    }

    /* --- serial configuration --- */
    tSerial += TASK_HOUSEKEEPING;
    if (tSerial >= TASK_SERIAL_CFG) {
      tSerial = 0;
      SystemSerialCfg.ProcessIncommingData();
    }

    /* --- WiFi management + reconnect --- */
    tWifi += TASK_HOUSEKEEPING;
    if (tWifi >= TASK_WIFI) {
      tWifi = 0;
      SystemWifiMngt.WifiManagement();
      McuTemperature.TemperatureCelsius = temperatureRead();
      SystemWifiMngt.WiFiReconnect();
      esp_task_wdt_reset();
    }

    /* --- WiFi watchdog (may perform a blocking scan) --- */
    tWdg += TASK_HOUSEKEEPING;
    if (tWdg >= TASK_WIFI_WATCHDOG) {
      tWdg = 0;
      SystemWifiMngt.WiFiWatchdog();
      esp_task_wdt_reset();
    }

    /* --- micro SD card health --- */
    tSd += TASK_HOUSEKEEPING;
    if (tSd >= TASK_SDCARD) {
      tSd = 0;
#if (true == ENABLE_SD_CARD)
      if ((true == SystemLog.GetCardDetectAfterBoot()) && (false == SystemLog.GetCardDetectedStatus())) {
        SystemLog.LogCloseFile();
        SystemLog.ReinitCard();
        SystemLog.LogOpenFile();
        SystemLog.AddEvent(LogLevel_Warning, F("Reinit micro SD card done!"));
      }

      if (true == SystemLog.GetCardDetectedStatus()) {
        SystemLog.CheckCardSpace();
        SystemLog.CheckMaxLogFileSize();
        SystemLog.LogCheckOpenedFile();
        if (false == SystemLog.GetLogFileOpened()) {
          SystemLog.LogOpenFile();
          SystemLog.AddEvent(LogLevel_Warning, F("Log file is not opened!"));
        }
      }
      SystemLog.AddEvent(LogLevel_Info, "CardStatus: " + String(SystemLog.GetCardDetectedStatus()) + " FileStatus: " + String(SystemLog.GetLogFileOpened()));
#endif
      esp_task_wdt_reset();
    }

    /* --- telemetry --- */
    tTelemetry += TASK_HOUSEKEEPING;
    if (tTelemetry >= TASK_SYSTEM_TELEMETRY) {
      tTelemetry = 0;
      if (SystemCamera.GetStreamStatus()) {
        char buf[80] = { 0 };
        snprintf(buf, sizeof(buf), "Stream, average data in %dsec. FPS: %.1f, Size: %uKB",
                 (TASK_SYSTEM_TELEMETRY / SECOND_TO_MILISECOND),
                 SystemCamera.StreamGetFrameAverageFps(), SystemCamera.StreamGetFrameAverageSize());
        SystemLog.AddEvent(LogLevel_Info, buf);
        SystemCamera.StreamClearFrameData();
      }

      /* getMaxAllocHeap is the number that predicts TLS failures — free heap alone
         hides fragmentation, which is what actually breaks the mbedTLS handshake. */
      char buf[160] = { 0 };
      snprintf(buf, sizeof(buf),
               "Heap free:%u min:%u maxblk:%u | PSRAM free:%u | %.1fC | upload ok:%lu fail:%lu consec:%lu",
               (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
               (unsigned)ESP.getFreePsram(), McuTemperature.TemperatureCelsius,
               (unsigned long)Connect.GetStatOk(), (unsigned long)Connect.GetStatFail(),
               (unsigned long)Connect.GetStatConsecutiveFail());
      SystemLog.AddEvent(LogLevel_Info, buf);

      /* Stack headroom at Info, not Verbose. These are the numbers that catch a task
         being given work it wasn't sized for — the failure mode is a silent overflow,
         so they are worth the one log line per 30 s. */
      char sbuf[120] = { 0 };
      snprintf(sbuf, sizeof(sbuf), "Stack free B - main:%u capture:%u housekeeping:%u",
               (unsigned)(Task_SystemMain ? uxTaskGetStackHighWaterMark(Task_SystemMain) : 0),
               (unsigned)(Task_CapturePhotoAndSend ? uxTaskGetStackHighWaterMark(Task_CapturePhotoAndSend) : 0),
               (unsigned)uxTaskGetStackHighWaterMark(NULL));
      SystemLog.AddEvent(LogLevel_Info, sbuf);
      esp_task_wdt_reset();
    }

    esp_task_wdt_reset();
    vTaskDelayUntil(&xLastWakeTime, TASK_HOUSEKEEPING / portTICK_PERIOD_MS);
  }
}

/* EOF */