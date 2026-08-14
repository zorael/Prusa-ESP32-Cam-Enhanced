/**
   @file module_ESP32-WROVER-DEV.h

   @brief Definition of the ESP32-WROVER-DEV

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   https://github.com/Freenove/Freenove_ESP32_WROVER_Board

   Board configuration in the arduino IDE 2.3.2
   Tools -> Board -> ESP32 Arduino -> ESP32 Wrover Module
   Tools -> CPU Frequency -> 240MHz (WiFi/BT)
   Tools -> Core debug level -> None
   Tools -> Flash frequency -> 80MHz
   Tools -> Flash Mode -> DIO
   Tools -> Partition scheme -> Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)

   @bug: no know bug

*/

#pragma once

#include "mcu_cfg.h"

#ifdef ESP32_WROVER_DEV

/* --------------- CAMERA CFG -------------------*/
#define PWDN_GPIO_NUM               -1      ///< Power down control pin
#define RESET_GPIO_NUM              -1      ///< Reset control pin
#define XCLK_GPIO_NUM               21      ///< External clock pin
#define SIOD_GPIO_NUM               26      ///< SCCB: SI/O data pin
#define SIOC_GPIO_NUM               27      ///< SCCB: SI/O control pin
#define Y9_GPIO_NUM                 35      ///< SCCB: Y9 pin
#define Y8_GPIO_NUM                 34      ///< SCCB: Y8 pin
#define Y7_GPIO_NUM                 39      ///< SCCB: Y7 pin
#define Y6_GPIO_NUM                 36      ///< SCCB: Y6 pin
#define Y5_GPIO_NUM                 19      ///< SCCB: Y5 pin
#define Y4_GPIO_NUM                 18      ///< SCCB: Y4 pin
#define Y3_GPIO_NUM                 5       ///< SCCB: Y3 pin
#define Y2_GPIO_NUM                 4       ///< SCCB: Y2 pin
#define VSYNC_GPIO_NUM              25      ///< Vertical sync pin
#define HREF_GPIO_NUM               23      ///< Line sync pin
#define PCLK_GPIO_NUM               22      ///< Pixel clock pin

/* ------------------ MCU CFG  ------------------*/
#define BOARD_NAME                  F("ESP32-WROVER-DEV") ///< Board name
#define ENABLE_BROWN_OUT_DETECTION  true   ///< Enable brown out detection
#define ENABLE_PSRAM                true   ///< Enable PSRAM   

/* --------------- OTA UPDATE CFG  --------------*/
#define OTA_ASSET_NAME              "firmware-wrover.app.bin" ///< release asset for env:wrover
#define FW_STATUS_LED_PIN           13     ///< GPIO pin for status FW update LED (unused by the code; 2 is the SD data line)
#define FW_STATUS_LED_LEVEL_ON      LOW    ///< GPIO pin level for status LED ON

/* --------------- FLASH LED CFG  ---------------*/
/* This board has no flash LED of its own — its only LEDs are IO2, RX, TX and a
   power indicator. GPIO 14 is what upstream's guide told people to wire an
   external LED to, and on a v3 board that pin is the SD card clock, so it moves
   here to GPIO 13. Nothing is harmed if no LED is wired: the pin just toggles. */
#define ENABLE_CAMERA_FLASH         true    ///< Enable camera flash function
#define CAMERA_FLASH_DIGITAL_CTRL   true    ///< Enable camera flash digital control
#define CAMERA_FLASH_PWM_CTRL       false   ///< Enable camera flash PWM control
#define CAMERA_FLASH_NEOPIXEL       false   ///< Enable camera flash NeoPixel control
#define FLASH_GPIO_NUM              13      ///< Flash control pin (was 14 — SD CLK on v3 boards)
#define FLASH_NEOPIXEL_LED_PIN      -1      ///< External flash control pin. RGB LED NeoPixel
#define FLASH_OFF_STATUS            LOW     ///< value for flash OFF
#define FLASH_ON_STATUS             HIGH    ///< value for flash ON
//#define FLASH_PWM_FREQ              2000    ///< frequency of pwm [240MHz / (100 prescale * pwm cycles)] = frequency
//#define FLASH_PWM_CHANNEL           0       ///< channel 0
//#define FLASH_PWM_RESOLUTION        8       ///< range 1-20bit. 8bit = 0-255 range

/* --------------- SD CARD CFG  ---------------*/
/* v3.0 boards carry a microSD slot on the back, wired to a fixed 1-bit SDMMC bus.
   Freenove's own example marks all three pins "Please do not modify it", and
   4-bit is impossible anyway: D1 would be GPIO 4, which is the camera's Y2.

   Earlier revisions of this board had no slot at all, which is why upstream ships
   this as false. Enabling it costs nothing on those: SD_MMC.begin() fails, the
   firmware reports "No card detected", and everything else runs — the same path
   as an AI Thinker with an empty slot. */
#define ENABLE_SD_CARD              true    ///< Enable SD card function (v3.0 boards; older ones report no card)
#define SD_PIN_CLK                  14      ///< GPIO pin for SD card clock
#define SD_PIN_CMD                  15      ///< GPIO pin for SD card command
#define SD_PIN_DATA0                2       ///< GPIO pin for SD card data 0

/* ---------- RESET CFG CONFIGURATION  ----------*/
#define CFG_RESET_PIN               12      ///< GPIO 12 is for reset CFG to default
#define CFG_RESET_LED_PIN           13      ///< GPIO for indication of reset CFG (was 2 — see below)
#define CFG_RESET_LED_LEVEL_ON      LOW     ///< GPIO pin level for status LED ON

/* -------------- STATUS LED CFG ----------------*/
/* The onboard LED is GPIO 2 — the same pin as SD D0. Freenove's own Blink sketch
   uses `LED_BUILTIN 2` and their SDMMC sketch uses `SD_MMC_D0 2`; the board
   really does put both on one pin, so the card and the status blink cannot
   coexist. Blinking it would drive a data line push-pull mid-transfer, and the
   housekeeping task toggles it every 400 ms to 4 s, so this is not a rare race.
   The card wins: timelapse and SD logging are the point of this firmware, and
   WiFi state is visible in the web UI. CFG_RESET_LED_PIN moved off 2 for the
   same reason — factory-reset blinking runs after the card is mounted. */
#define STATUS_LED_ENABLE           false   ///< disabled: GPIO 2 is SD D0 on this board
#define STATUS_LED_GPIO_NUM         2       ///< GPIO pin for status LED (unused while STATUS_LED_ENABLE is false)
#define STATUS_LED_OFF_PIN_LEVEL    HIGH    ///< GPIO pin level for status LED ON


#endif  // ESP32_WROVER_DEV
/* EOF */