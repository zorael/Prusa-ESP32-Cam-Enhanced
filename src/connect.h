/**
   @file connnect.h

   @brief library for communication with prusa connect backend

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   @bug: no know bug
*/
#pragma once

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>
#include <Arduino.h>
#include <ArduinoJson.h>

#include "wifi_mngt.h"
#include "mcu_cfg.h"
#include "var.h"
#include "log.h"
#include "camera.h"
#include "cfg.h"
#include "Certificate.h"
#include "WebServer.h"
#include "connect_types.h"

class WiFiMngt;
class Configuration;
class Camera;

class PrusaConnect {
private:
  uint8_t RefreshInterval;                        ///< interval for sending photo to backend
  String BackendReceivedStatus;                   ///< status of backend response
  BackendAvailabilitStatus BackendAvailability;   ///< status of backend availability
  bool SendDeviceInformationToBackend;            ///< flag for sending device information to backend
  uint8_t SendingIntervalCounter;                 ///< counter for sending interval, represents seconds
  bool EnableTimelapsPhotoSave;                   ///< flag for saving photo to SD card
  bool UploadEnabled;                             ///< user toggle — persisted to EEPROM, set via UI only
  bool _printerGateOpen;                          ///< runtime gate — false while printer is idle after a completed print

  String Token;                                   ///< token for backend communication
  String Fingerprint;                             ///< fingerprint for backend communication
  String PrusaConnectHostname;                    ///< hostname of prusa connect backend

  /* last upload diagnostics — populated by SendDataToBackend() */
  size_t _dbgLastFbLen        = 0;
  size_t _dbgLastContentLen   = 0;
  size_t _dbgLastSentBytes    = 0;
  bool   _dbgLastUploadOk     = false;
  volatile bool _isUploading  = false;

  /* cumulative upload health — without these "more reliable" is unmeasurable */
  uint32_t _statUploadAttempts  = 0;   ///< individual TLS attempts, including retries
  uint32_t _statUploadOk        = 0;   ///< cycles that ended in an accepted upload
  uint32_t _statUploadFail      = 0;   ///< cycles that exhausted their attempts
  uint32_t _statConsecutiveFail = 0;   ///< reset on success; the number that matters

  Configuration *config;                          ///< pointer to configuration object
  Logs *log;                                      ///< pointer to logs object
  Camera *camera;                                 ///< pointer to camera object
  WiFiMngt *wifi;                                 ///< pointer to wifi object

  bool SendDataToBackend(String *, size_t, String, String, String, SendDataToBackendType);

public:
  PrusaConnect(Configuration*, Logs*, Camera*, WiFiMngt*);
  ~PrusaConnect(){};

  void Init();
  void LoadCfgFromEeprom();

  void TakePicture();
  bool SendPhotoToBackend();
  void SendInfoToBackend();
  void TakePictureAndSendToBackend();
  String ProcessHttpResponseCode(int);
  bool ProcessHttpResponseCodeBool(int);
  void UpdateDeviceInformation();

  void SetRefreshInterval(uint8_t);
  void SetToken(String);
  void SetBackendAvailabilitStatus(BackendAvailabilitStatus);
  void SetPrusaConnectHostname(String);
  void SetTimeLapsPhotoSaveStatus(bool);
  void SetUploadEnabled(bool);
  bool GetUploadEnabled() const;
  void SetPrinterGate(bool);

  void SavePhotoToSdCard();

  uint8_t GetRefreshInterval();
  String GetBackendReceivedStatus();
  size_t  GetDbgLastFbLen()       const { return _dbgLastFbLen; }
  size_t  GetDbgLastContentLen()  const { return _dbgLastContentLen; }
  size_t  GetDbgLastSentBytes()   const { return _dbgLastSentBytes; }
  bool    GetDbgLastUploadOk()    const { return _dbgLastUploadOk; }
  bool    IsUploading()           const { return _isUploading; }
  uint32_t GetStatAttempts()      const { return _statUploadAttempts; }
  uint32_t GetStatOk()            const { return _statUploadOk; }
  uint32_t GetStatFail()          const { return _statUploadFail; }
  uint32_t GetStatConsecutiveFail() const { return _statConsecutiveFail; }
  String GetToken();
  String GetFingerprint();
  String GetPrusaConnectHostname();
  BackendAvailabilitStatus GetBackendAvailabilitStatus();
  String CovertBackendAvailabilitStatusToString(BackendAvailabilitStatus);
  bool GetTimeLapsPhotoSaveStatus();

  void IncreaseSendingIntervalCounter();
  void SetSendingIntervalCounter(uint8_t);
  void SetSendingIntervalExpired();
  uint8_t GetSendingIntervalCounter();
  bool CheckSendingIntervalExpired();
};

extern PrusaConnect Connect;  ///< PrusaConnect object

/* EOF */