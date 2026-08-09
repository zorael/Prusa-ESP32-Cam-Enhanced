/*
   ESP32 PrusaConnect Camera — enhanced fork.

   Built with PlatformIO, not the Arduino IDE. Every dependency, the board
   definition and the partition table are declared in platformio.ini and fetched
   automatically:

       pio run -e ai_thinker              build
       pio run -e ai_thinker -t upload    build and flash
       pio device monitor -b 115200       serial console

   Per-board pin maps and feature flags live in src/module_<BOARD>.h, selected by
   the build_flags of the environment you build.

   Flashing a blank device needs the full factory image at 0x0, which also writes
   the partition table. Upgrading an existing one should write only the app at
   0x10000 — the factory image spans NVS and would take your WiFi credentials and
   Prusa Connect token with it. tools/devflash.py wraps both paths. OTA is always
   safe: it writes the inactive app partition only.

   Original project: ESP32 PrusaConnect Camera
   Developed for: Prusa Research, prusa3d.com
   Author: Miroslav Pivovarsky
   e-mail: miroslav.pivovarsky@gmail.com
   Licence: GPL-3.0
*/

/* includes */
#include "Arduino.h"
#include <esp_task_wdt.h>
#include <ESPmDNS.h>
#include "esp32-hal-cpu.h"
#include "mbedtls/platform.h"
#include "mbedtls/bignum.h"
#include "esp_heap_caps.h"

#include "WebServer.h"
#include "cfg.h"
#include "var.h"
#include "mcu_cfg.h"
#include "system.h"
#include "micro_sd.h"
#include "log.h"
#include "connect.h"
#include "wifi_mngt.h"
#include "serial_cfg.h"
#include "prusa_link.h"

void setup() {
  /* Serial port for debugging purposes */
  Serial.begin(SERIAL_PORT_SPEED);
  Serial.println(F("----------------------------------------------------------------"));
  Serial.println(F("Start MCU!"));
  Serial.println(F("Prusa ESP32-cam https://prusa3d.cz"));
  Serial.print(F("SW Version: "));
  Serial.println(SW_VERSION);
  Serial.print(F("Build: "));
  Serial.println(SW_BUILD);
#if (CONSOLE_VERBOSE_DEBUG == true)
  Serial.setDebugOutput(true);
#endif

  /* mbedTLS allocator policy — PSRAM FIRST for large blocks.

     The previous policy tried internal RAM first and only fell back to PSRAM.
     That inverted priority caused a hard crash: mbedTLS greedily consumed internal
     RAM for its 16 KB TLS record buffers and the X509 chain, drove internal free
     toward zero, and then the ~90-byte FreeRTOS mutex that
     esp_crypto_mpi_lock_acquire() creates lazily on first RSA operation failed to
     allocate. newlib's lock_init_generic() responds to a NULL mutex by calling
     abort() — a panic reboot in the middle of the TLS handshake, every time.

     Large mbedTLS buffers are byte-addressed and never touched from an ISR or by
     DMA, so PSRAM is the correct home for them. Internal RAM is then left for the
     allocations that genuinely require it. Small allocations stay internal because
     PSRAM carries per-block overhead and small internal blocks are plentiful. */
  mbedtls_platform_set_calloc_free(
    [](size_t n, size_t sz) -> void* {
      const size_t total = n * sz;
      if ((total >= MBEDTLS_PSRAM_THRESHOLD) && psramFound()) {
        void* p = heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM);
        if (p) return p;
      }
      void* p = heap_caps_calloc(n, sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      if (!p) p = heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM);
      return p;
    },
    heap_caps_free
  );

  /* Force the lazy crypto-lock allocations to happen now, while the heap is fresh.
     esp_crypto_mpi_lock_acquire() creates its mutex on first use; if that first use
     lands under memory pressure the failure is an abort(), not an error return, so
     there is nothing to recover from at that point. One throwaway modexp at boot
     makes the lock permanent and removes the failure mode entirely. */
  {
    mbedtls_mpi base, exp, mod, res;
    mbedtls_mpi_init(&base); mbedtls_mpi_init(&exp);
    mbedtls_mpi_init(&mod);  mbedtls_mpi_init(&res);
    mbedtls_mpi_lset(&base, 12345);
    mbedtls_mpi_lset(&exp, 17);
    mbedtls_mpi_lset(&mod, 9973);
    int warm = mbedtls_mpi_exp_mod(&res, &base, &exp, &mod, NULL);
    mbedtls_mpi_free(&base); mbedtls_mpi_free(&exp);
    mbedtls_mpi_free(&mod);  mbedtls_mpi_free(&res);
    Serial.printf("Crypto lock warm-up: %d\n", warm);
  }

  /* Init EEPROM */
  EEPROM.begin(EEPROM_SIZE);

  /* init system led */
  system_led.init();

  /* init micro SD card and logs */
  SystemLog.SetLogLevel((LogLevel_enum)EEPROM.read(EEPROM_ADDR_LOG_LEVEL));
  SystemLog.Init();

  /* init System lib */
  System_Init();

  /* read cfg from EEPROM */
  SystemConfig.Init();
  SystemConfig.CheckResetCfg();
  Server_LoadCfg();
  SystemCamera.LoadCameraCfgFromEeprom();
  Connect.LoadCfgFromEeprom();
  SystemWifiMngt.LoadCfgFromEeprom();
  SystemPrusaLink.LoadCfgFromEeprom();
  SystemTimelapse.setMaxFrames(SystemConfig.LoadTlMaxFrames());

  /* init WiFi mngt */
  SystemWifiMngt.Init();

  /* init camera interface */
  SystemCamera.Init();
  SystemCamera.CapturePhoto();
  SystemCamera.CaptureReturnFrameBuffer();

  /* init WEB server */
  Server_InitWebServer();

  /* init class for communication with PrusaConnect */
  Connect.Init();

  /* Camera is up (or permanently failed) — from here on an init failure must not
     reboot a running device. */
  SystemCamera.SetBootCompleted();

  /* init wdg */
  SystemLog.AddEvent(LogLevel_Info, F("Init WDG"));
  esp_task_wdt_config_t twdt_config = {};
  twdt_config.timeout_ms = WDG_TIMEOUT;
  /* Watch Core 0's idle task only. The previous all-cores mask also subscribed
     Core 1's idle task, which stock IDF config deliberately leaves unwatched —
     and Core 1 is exactly where the upload task blocks for seconds inside mbedTLS. */
  twdt_config.idle_core_mask = (1 << 0);
  twdt_config.trigger_panic = true;

  esp_task_wdt_init(&twdt_config);          /* already inited by IDF; reconfigure is what applies */
  esp_task_wdt_reconfigure(&twdt_config);
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));  /* add current thread to WDT watch */
  esp_task_wdt_reset();                     /* reset wdg */

  /* create inter-task command queue before starting tasks */
  g_captureQueue = xQueueCreate(8, sizeof(CaptureCommand_t));

  /* Three tasks, down from eight. The five housekeeping loops now share one stack
     (see System_TaskHousekeeping) — ~14 kB of DRAM and five WDT subscribers saved. */
  SystemLog.AddEvent(LogLevel_Info, F("Start tasks"));
  /* 5200/6000 B were too small for mbedTLS 3.x + ArduinoJson stack frames under ESP-IDF 5.x */
  /* Now only polls the button and drives the 100 ms tick — PrusaLink.Poll() moved to
     Housekeeping, and the stack budget moved with it. */
  xTaskCreatePinnedToCore(System_TaskMain, "SystemMain", 3072, NULL, 3, &Task_SystemMain, 0);
  ESP_ERROR_CHECK(esp_task_wdt_add(Task_SystemMain));
  /* Core 1 keeps TLS off the WiFi core */
  xTaskCreatePinnedToCore(System_TaskCaptureAndSendPhoto, "CaptureAndSendPhoto", 10240, NULL, 2, &Task_CapturePhotoAndSend, 1);
  ESP_ERROR_CHECK(esp_task_wdt_add(Task_CapturePhotoAndSend));
  /* Absorbs WiFiManagement + SdCardCheck + SerialCfg + Telemetry + SysLed + WiFiWatchdog,
     and now PrusaLink.Poll(). 8192 because of that last one: HTTPClient + ArduinoJson
     frames are what overflowed the old 5200/6000 B stacks, so this must not be sized
     from the housekeeping work alone. WiFi scan JSON lives here too. */
  xTaskCreatePinnedToCore(System_TaskHousekeeping, "Housekeeping", 8192, NULL, 1, &Task_Housekeeping, 0);
  ESP_ERROR_CHECK(esp_task_wdt_add(Task_Housekeeping));

  SystemLog.AddEvent(LogLevel_Info, F("MCU configuration done"));
}

void loop() {
  /* reset wdg */
  esp_task_wdt_reset();
  delay(1000);
}

/* EOF */