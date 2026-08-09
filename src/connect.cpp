/**
   @file connnect.cpp

   @brief library for communication with prusa connect backend

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   @bug: no know bug
*/

#include "connect.h"
#include "timelapse.h"

PrusaConnect Connect(&SystemConfig, &SystemLog, &SystemCamera, &SystemWifiMngt);

/**
 * @brief Size the body-write budget from the payload.
 *
 * Clamped so it always exceeds CONNECT_SOCKET_TIMEOUT_MS — the socket layer must
 * be what gives up first, otherwise we abort healthy-but-slow transfers mid-JPEG.
 */
static unsigned long UploadBodyBudgetMs(size_t i_len) {
  unsigned long budget = ((unsigned long)i_len * 1000UL) / UPLOAD_MIN_THROUGHPUT_BPS;
  if (budget < UPLOAD_BODY_BUDGET_MIN_MS) budget = UPLOAD_BODY_BUDGET_MIN_MS;
  if (budget > UPLOAD_BODY_BUDGET_MAX_MS) budget = UPLOAD_BODY_BUDGET_MAX_MS;
  return budget;
}

/**
 * @brief HTTP response codes the backend is documented to return.
 *
 * Replaces two parallel switch statements (~90 lines) that had to be kept in
 * sync by hand; `ok` and the description now cannot disagree.
 */
struct HttpCodeInfo {
  uint16_t code;
  bool     ok;
  const char *text;
};

static const HttpCodeInfo kHttpCodes[] = {
  { 200, true,  "200 - OK" },
  { 201, true,  "201 - OK entry created" },
  { 204, true,  "204 - Upload OK" },
  { 304, false, "304 - Response has not been modified" },
  { 400, false, "400 - Some data received is not valid" },
  { 401, false, "401 - Missing security token or it is not valid" },
  { 403, false, "403 - Security token is not valid or is outdated" },
  { 404, false, "404 - Entity not found or invalid auth token" },
  { 409, false, "409 - Conflict with the state of target resource (user error)" },
  { 503, false, "503 - Service is unavailable at this moment. Try again later" },
};

static const HttpCodeInfo *FindHttpCode(int i_code) {
  for (const HttpCodeInfo &e : kHttpCodes) {
    if (e.code == (uint16_t)i_code) return &e;
  }
  return nullptr;
}

/**
 * @brief Constructor for PrusaConnect class
 *
 * @param Configuration*  - pointer to Configuration class
 * @param Logs*           - pointer to Logs class
 * @param Camera*         - pointer to Camera class
 */
PrusaConnect::PrusaConnect(Configuration *i_conf, Logs *i_log, Camera *i_camera, WiFiMngt *i_wifi) {
  config = i_conf;
  log = i_log;
  camera = i_camera;
  wifi = i_wifi;
  BackendAvailability = WaitForFirstConnection;
  SendDeviceInformationToBackend = true;
  UploadEnabled = true;
  _printerGateOpen = true;
}

/**
 * @brief init library PrusaConnect
 *
 * @param none
 * @return none
 */
void PrusaConnect::Init() {
  log->AddEvent(LogLevel_Info, F("Init PrusaConnect lib"));
  BackendReceivedStatus = F("Wait for first connection");
}

/**
 * @brief Load configuration from EEPROM
 *
 * @param none
 * @return none
 */
void PrusaConnect::LoadCfgFromEeprom() {
  log->AddEvent(LogLevel_Info, F("Load PrusaConnect CFG from EEPROM"));
  Token = config->LoadToken();
  Fingerprint = config->LoadFingerprint();
  RefreshInterval = config->LoadRefreshInterval();
  PrusaConnectHostname = config->LoadPrusaConnectHostname();
  EnableTimelapsPhotoSave = config->LoadTimeLapseFunctionStatus();
  UploadEnabled = config->LoadUploadEnable();
}

/**
 * @brief take picture
 *
 * @param none
 * @return none
 */
void PrusaConnect::TakePicture() {
  camera->CapturePhoto();
}

/**
 * @brief Sending data to prusa connect backend
 * 
 * @param i_data  - data to send
 * @param i_content_type - data content type
 * @param i_type - type of data for log message
 * @param i_url_path - url path for backend
 * @param i_fragmentation - flag for enable/disable data fragmentation
 * @return true - if data was sent successfully
 * @return false - if data was not sent successfully
 */
bool PrusaConnect::SendDataToBackend(String *i_data, size_t i_data_length, String i_content_type, String i_type, String i_url_path, SendDataToBackendType i_data_type) {  /* i_data_length is size_t to avoid signed/unsigned mismatch on large frames */
  WiFiClientSecure client;
  BackendReceivedStatus = "";
  bool ret = false;
  log->AddEvent(LogLevel_Info, "Sending " + i_type + " to PrusaConnect, " + String(i_data_length) + " bytes");

  /* check fingerprint and token length */
  if ((Fingerprint.length() > 0) && (Token.length() > 0)) {
    client.setCACert(root_CAs);
    /* Timeout wiring — verified against arduino-esp32 3.x sources.
       NetworkClientSecure declares neither setTimeout() nor _timeout; it inherits
       both from NetworkClient. Consequences:
         - setTimeout(x)           -> binds to Stream::setTimeout, i.e. ONLY the
                                      readStringUntil()/readBytes() parse timeout.
                                      It does NOT touch the TLS socket. The previous
                                      call here was a no-op for the socket.
         - setConnectionTimeout(x) -> NetworkClient::_timeout [ms], which becomes
                                      SO_SNDTIMEO/SO_RCVTIMEO and the send_ssl_data()
                                      retry budget. Defaults to 30000 ms.
         - setHandshakeTimeout(x)  -> ssl_client->handshake_timeout, argument in
                                      SECONDS. Defaults to 120 s, which on its own
                                      exceeds the 60 s task watchdog and panics the MCU.
       Budget: 8 s socket + 12 s handshake = 20 s worst case per attempt, leaving
       ample margin under WDG_TIMEOUT (60 s). */
    client.setConnectionTimeout(CONNECT_SOCKET_TIMEOUT_MS);
    client.setHandshakeTimeout(CONNECT_HANDSHAKE_TIMEOUT_S);
    client.setTimeout(CONNECT_STREAM_PARSE_TIMEOUT_MS);
    client.setNoDelay(true);

    log->AddEvent(LogLevel_Verbose, F("Connecting to server..."));

    /* DNS resolution + TCP connect + TLS handshake can collectively exceed 60s
       (DNS TTL expires after long idle; lwIP may block up to ~90s on retry).
       Reset TWDT here so the capture task's subscription doesn't expire mid-connect. */
    esp_task_wdt_reset();

    /* connecting to server */
    if (!client.connect(PrusaConnectHostname.c_str(), 443)) {
      char err_buf[200];
      int last_error = client.lastError(err_buf, sizeof(err_buf));
      int error = client.getWriteError();
      if (BackendAvailability != WaitForFirstConnection) {
        BackendAvailability = BackendUnavailable;
      }

      BackendReceivedStatus = "Connetion failed to domain! Error: " + String(last_error) + " - " + String(err_buf) + " : " + String(error);
      log->AddEvent(LogLevel_Info, BackendReceivedStatus + " ,BA:" + CovertBackendAvailabilitStatusToString(BackendAvailability));
      return false;

    } else {
      /* send data to server */
      log->AddEvent(LogLevel_Verbose, F("Connected to server!"));
      client.println("PUT https://" + PrusaConnectHostname + i_url_path + " HTTP/1.1");
      client.println("Host: " + PrusaConnectHostname);
      client.println("User-Agent: ESP32-CAM");
      client.println("Connection: close");

      client.println("Content-Type: " + i_content_type);
      client.println("fingerprint: " + Fingerprint);
      client.println("token: " + Token);
      client.println("Content-Length: " + String(i_data_length));
      client.println();

      esp_task_wdt_reset();
      size_t sendet_data = 0;
      /* sending photo */
      if (SendPhoto == i_data_type) {
        log->AddEvent(LogLevel_Verbose, F("Sendig photo"));

        /* Stream from the PSRAM snapshot staged by BuildUploadSnapshot(), never from
           the live camera frame buffer — the browser stream can recycle that DMA
           buffer mid-transfer. The snapshot already contains EXIF header + JPEG body
           concatenated, so there is no header write or offset arithmetic here. */
        const uint8_t *fbBuf = camera->GetUploadSnapshot();
        size_t fbLen = camera->GetUploadSnapshotLen();
        _dbgLastContentLen = i_data_length;
        _dbgLastFbLen = fbLen;

        if ((fbBuf == NULL) || (fbLen == 0)) {
          log->AddEvent(LogLevel_Error, F("No upload snapshot staged"));
          BackendReceivedStatus = F("No photo snapshot");
          esp_task_wdt_reset();
          client.stop();
          esp_task_wdt_reset();
          return false;
        }

        /* Body write budget.
           The previous hard 10 s cap was the direct cause of
           "INCOMPLETE DATA SEND TO SERVER!": it sat *below* the TLS socket timeout,
           so a single stalled client.write() could not be interrupted by it, and
           when that write finally returned the deadline had already passed — the
           loop broke mid-JPEG with remaining > 0 and the Content-Length check failed.

           The budget is now derived from the payload and floored well above the
           socket timeout, so the socket layer is what gives up first and reports a
           real error, rather than us aborting a healthy-but-slow transfer.
           ~24 kB/s is a conservative floor for ESP32 TLS throughput on a weak link. */
        const unsigned long bodyBudget = UploadBodyBudgetMs(fbLen);
        unsigned long uploadStart = millis();
        size_t remaining = fbLen;
        bool writeStalled = false;
        while (remaining > 0) {
          if (millis() - uploadStart >= bodyBudget) {
            log->AddEvent(LogLevel_Warning, "Upload body budget exceeded (" + String(bodyBudget) + "ms), " + String(remaining) + "B left");
            writeStalled = true;
            break;
          }
          esp_task_wdt_reset();
          size_t chunk = (remaining > PHOTO_FRAGMENT_SIZE) ? PHOTO_FRAGMENT_SIZE : remaining;
          size_t written = client.write(fbBuf, chunk);
          if (written == 0) {  /* socket timeout or peer reset — client.write() stops the client */
            log->AddEvent(LogLevel_Warning, "Upload write failed, " + String(remaining) + "B left");
            writeStalled = true;
            break;
          }
          sendet_data += written;
          fbBuf += written;
          remaining -= written;
        }
        /* A short write leaves the request body incomplete; there is no point
           reading a response the server will never send. Bail out so the caller's
           retry can open a clean connection. */
        if (writeStalled) {
          BackendReceivedStatus = F("Upload stalled - incomplete body");
          esp_task_wdt_reset();
          client.stop();
          esp_task_wdt_reset();
          return false;
        }

        log->AddEvent(LogLevel_Verbose, F("Photo snapshot sent"));

        /* sending device information */
      } else if (SendInfo == i_data_type) {
        log->AddEvent(LogLevel_Verbose, F("Sending info"));
        sendet_data = client.print(*i_data);
      }

      client.flush();
      _dbgLastSentBytes = sendet_data;
      log->AddEvent(LogLevel_Info, "Send done: " + String(i_data_length) + "/" + String(sendet_data) + " bytes");

      /* check if all data was sent */
      if (i_data_length != sendet_data) {
        BackendReceivedStatus = F("INCOMPLETE DATA SEND TO SERVER!");
        log->AddEvent(LogLevel_Error, F("ERROR SEND DATA TO SERVER! INCORRECT DATA LENGTH!"));
        /* client.stop() on a stalled connection blocks up to the socket timeout;
           the destructor on 'return' may also block — reset WDT around both */
        esp_task_wdt_reset();
        client.stop();
        esp_task_wdt_reset();  /* covers destructor of WiFiClientSecure on return */
        return false;
      }
      //esp_task_wdt_reset();

      /* read response from server */
      String response = "";
      String fullResponse = "";
      vTaskDelay(pdMS_TO_TICKS(10));  /* vTaskDelay yields to scheduler; delay() busy-waits */
      log->AddEvent(LogLevel_Verbose, F("Response:"));
      /* We send "Connection: close", so the server replies and immediately closes.
         The old condition was `client.connected() && ...`: once the peer closed,
         connected() went false and the loop exited with the status line still
         sitting unread in the receive buffer — scoring a stored photo as a failure.
         Checking available() first keeps draining buffered bytes after close. */
      unsigned long responseDeadline = millis() + CONNECT_RESPONSE_TIMEOUT_MS;
      unsigned long lastWdtReset = millis();
      while ((client.available() || client.connected()) && millis() < responseDeadline) {
        if (millis() - lastWdtReset >= 500) {  /* was reset every spin — millions of calls */
          esp_task_wdt_reset();
          lastWdtReset = millis();
        }
        if (!client.available()) {
          vTaskDelay(pdMS_TO_TICKS(5));  /* yield: this loop used to busy-spin Core 1 */
          continue;
        }
        response = client.readStringUntil('\n');
        if (fullResponse.length() < 2048) {
          fullResponse += response;
        }
        log->AddEvent(LogLevel_Verbose, response.c_str());

        if (response.startsWith("HTTP/1.1")) {
          int httpCode = response.substring(9, 12).toInt();
          BackendReceivedStatus = i_type;
          BackendReceivedStatus += ": ";
          BackendReceivedStatus += ProcessHttpResponseCode(httpCode);
          if (true == ProcessHttpResponseCodeBool(httpCode)) {
            ret = true;
          }
        }
      }
      esp_task_wdt_reset();
      log->AddEvent(LogLevel_Verbose, "Full response: " + fullResponse);

      /* Reaching this point means TCP+TLS succeeded, so the backend is reachable.
         Whether it *accepted* the payload is a separate axis — previously this was
         set unconditionally, so a 401/403 still reported "Backend available" and
         the UI was useless for diagnosing exactly this class of failure. */
      BackendAvailability = ret ? BackendAvailable : BackendUnavailable;
      _dbgLastUploadOk = ret;
      esp_task_wdt_reset();  /* client.stop() on TLS can block up to socket timeout */
      client.stop();
    }
  } else {
    /* err message */
    log->AddEvent(LogLevel_Verbose, F("ERROR SEND DATA TO SERVER! INVALID DATA!"));
    log->AddEvent(LogLevel_Verbose, "Fingerprint length: " + String(Fingerprint.length()));
    /* never log credentials in plaintext */
    log->AddEvent(LogLevel_Verbose, "Token present: " + String(Token.length() > 0 ? "yes" : "no"));

    if (Fingerprint.length() == 0) {
      BackendReceivedStatus = F("Missing fingerprint");
    } else if (Token.length() == 0) {
      BackendReceivedStatus = F("Missing token");
    }
  }

  log->AddEvent(LogLevel_Info, "Upload done. Response code: " + BackendReceivedStatus + " ,BA:" + CovertBackendAvailabilitStatusToString(BackendAvailability));
  return ret;
}

/**
 * @brief Send photo to prusa connect backend
 *
 * @param none
 * @return none
 */
bool PrusaConnect::SendPhotoToBackend() {
  log->AddEvent(LogLevel_Info, F("Start sending photo to prusaconnect"));
  camera->SetPhotoSending(true);
  String Photo = "";

  /* Stage the frame into PSRAM before opening the connection. Content-Length is
     then taken from the snapshot itself, so the declared length and the bytes
     actually written cannot diverge — the three-way branch this replaced computed
     the length from live camera state that could change during the transfer. */
  bool result = false;
  if (camera->BuildUploadSnapshot()) {
    result = SendDataToBackend(&Photo, camera->GetUploadSnapshotLen(),
                               F("image/jpg"), F("Photo"), HOST_URL_CAM_PATH, SendPhoto);
  } else {
    log->AddEvent(LogLevel_Error, F("Failed to stage photo for upload"));
    BackendReceivedStatus = F("Snapshot failed");
  }

  camera->SetPhotoSending(false);
  return result;
}

/**
 * @brief seding device info to prusaconnect backend
 * 
 */
void PrusaConnect::SendInfoToBackend() {
  if (!UploadEnabled || !_printerGateOpen) return;
  if (false == SendDeviceInformationToBackend) {
    return;

  } else {
    log->AddEvent(LogLevel_Info, F("Start sending device information to prusaconnect"));

    JsonDocument json_data;
    String json_string = "";

    JsonObject config = json_data["config"].to<JsonObject>();
    config["name"] = wifi->GetMdns();
    config["firmware"] = SW_VERSION;
    config["manufacturer"] = F("Prusa");
    config["model"] = BOARD_NAME;

    JsonObject resolution = config["resolution"].to<JsonObject>();
    resolution["width"] = SystemCamera.GetFrameSizeWidth();
    resolution["height"] = SystemCamera.GetFrameSizeHeight();

    JsonObject network_info = config["network_info"].to<JsonObject>();
    network_info["wifi_mac"] = SystemWifiMngt.GetWifiMac();
    network_info["wifi_ipv4"] = SystemWifiMngt.GetStaIp();
    network_info["wifi_ssid"] = SystemWifiMngt.GetStaSsid();

    serializeJson(json_data, json_string);
    log->AddEvent(LogLevel_Info, "Data: " + json_string);
    bool response = SendDataToBackend(&json_string, json_string.length(), F("application/json"), F("Info"), HOST_URL_INFO_PATH, SendInfo);

    if (true == response) {
      SendDeviceInformationToBackend = false;
    }
  }
}

/**
 * @brief Take picture and send to backend
 *
 * @param none
 * @return none
 */
void PrusaConnect::TakePictureAndSendToBackend() {
  bool wifiUp = (WiFi.status() == WL_CONNECTED);
  _isUploading = UploadEnabled && _printerGateOpen && wifiUp;
  camera->CapturePhoto();

  if (camera->GetCameraCaptureSuccess() != true) {
    log->AddEvent(LogLevel_Error, F("Error capturing photo. Stop sending to backend!"));
    if (camera->GetStreamStatus() == true) {
      camera->StreamSetSendingPhoto(false);
    }
    _isUploading = false;
    return;
  }

  if (UploadEnabled && _printerGateOpen && wifiUp) {
    /* Retry within the cycle's own time budget rather than gating on a fixed
       interval threshold. The old rule was `maxAttempts = (RefreshInterval >= 120) ? 3 : 1`,
       and the factory default interval is 30 s — so in the shipped configuration
       retries never ran at all and every transient fault dropped a frame.
       Now: keep retrying while enough of the interval remains to finish an attempt,
       capped at 3, leaving a guard band so we never run into the next cycle. */
    bool uploaded = false;
    const unsigned long cycleStart  = millis();
    const unsigned long cycleBudget = (RefreshInterval > UPLOAD_CYCLE_GUARD_S)
                                        ? (RefreshInterval - UPLOAD_CYCLE_GUARD_S) * 1000UL
                                        : 0UL;
    int attempt = 0;
    while (!uploaded && attempt < UPLOAD_MAX_ATTEMPTS) {
      if (attempt > 0) {
        uint32_t backoff = 1000UL << attempt;  /* 2 s, 4 s */
        if (millis() - cycleStart + backoff >= cycleBudget) {
          log->AddEvent(LogLevel_Warning, F("Upload retry skipped - cycle budget spent"));
          break;
        }
        log->AddEvent(LogLevel_Warning, "Upload retry " + String(attempt) + ", backoff " + String(backoff) + "ms");
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(backoff));
      }
      esp_task_wdt_reset();
      uploaded = SendPhotoToBackend();
      attempt++;
    }
    _statUploadAttempts += attempt;
    if (uploaded) {
      _statUploadOk++;
      _statConsecutiveFail = 0;
    } else {
      _statUploadFail++;
      _statConsecutiveFail++;
      log->AddEvent(LogLevel_Error, "Upload failed after " + String(attempt) + " attempt(s), "
                                    + String(_statConsecutiveFail) + " consecutive");
    }
    /* reset WDT: client.stop() after upload can block up to socket timeout */
    esp_task_wdt_reset();
  } else {
    log->AddEvent(LogLevel_Verbose, F("Upload disabled — skipping Prusa Connect send"));
  }

  /* always save to SD / append timelapse frame regardless of upload state */
  if (false == camera->GetStreamStatus()) {
    SavePhotoToSdCard();
  }

  /* return frame buffer */
  if (camera->GetStreamStatus() == true) {
    camera->StreamSetSendingPhoto(false);
  }
  _isUploading = false;
}

/**
   @brief Function for processing http response code from prusa backend
   @param int - http response code
   @return none
*/
String PrusaConnect::ProcessHttpResponseCode(int code) {
  const HttpCodeInfo *e = FindHttpCode(code);
  if (e != nullptr) {
    return String(e->text);
  }
  return String(code) + F(" - unknown error code");
}
/**
 * @brief Translate http response code to boolean
 * 
 * @param code - http response code
 * @return true - if response code is OK
 * @return false - if response code is not OK
 */
bool PrusaConnect::ProcessHttpResponseCodeBool(int code) {
  const HttpCodeInfo *e = FindHttpCode(code);
  return (e != nullptr) && e->ok;
}

/**
 * @brief Update device information
 *
 * @param none
 * @return none
 */
void PrusaConnect::UpdateDeviceInformation() {
  SendDeviceInformationToBackend = true;
}

/**
 * @brief Set refresh interval
 *
 * @param uint8_t i_data - refresh interval
 * @return none
 */
void PrusaConnect::SetRefreshInterval(uint8_t i_data) {
  RefreshInterval = i_data;
  config->SaveRefreshInterval(RefreshInterval);
}

/**
 * @brief Set token
 *
 * @param String i_data - token
 * @return none
 */
void PrusaConnect::SetToken(String i_data) {
  Token = i_data;
  config->SaveToken(Token);
}

/**
 * @brief Set backend availability status
 *
 * @param BackendAvailabilitStatus - backend status
 * @return none
 */
void PrusaConnect::SetBackendAvailabilitStatus(BackendAvailabilitStatus i_data) {
  BackendAvailability = i_data;
}

/**
 * @brief set prusa connect hostname
 * 
 * @param String i_data - hostname
 */
void PrusaConnect::SetPrusaConnectHostname(String i_data) {
  PrusaConnectHostname = i_data;
  config->SavePrusaConnectHostname(PrusaConnectHostname);
}

/**
 * @brief Set time laps photo save status
 * 
 * @param bool - status
 */
void PrusaConnect::SetTimeLapsPhotoSaveStatus(bool i_data) {
  EnableTimelapsPhotoSave = i_data;
  config->SaveTimeLapseFunctionStatus(EnableTimelapsPhotoSave);
}

/**
   @brief Function for saving photo to SD card
   @param none
   @return none
*/
void PrusaConnect::SavePhotoToSdCard() {
#if (ENABLE_SD_CARD == true)
  /* Nothing to do — take the lock only when there is real work, so the stream is
     never blocked for a no-op. */
  const bool wantAviFrame = SystemTimelapse.isRecording() && !SystemPrusaLink.LayerFramesActive();
  if ((EnableTimelapsPhotoSave == false) && (false == wantAviFrame)) {
    return;
  }

  /* Same race as the upload path: these functions read the live camera frame buffer,
     which CaptureStream() can recycle from the AsyncTCP task. The SD write is short
     (tens of ms) so holding the semaphore across it is cheap, unlike the upload. */
  if (xSemaphoreTake(camera->GetFrameBufferSemaphore(), pdMS_TO_TICKS(CAMERA_SNAPSHOT_WAIT_MS)) != pdTRUE) {
    log->AddEvent(LogLevel_Error, F("SD save: frame buffer busy, skipping"));
    return;
  }

  /* check if time laps photo save is enabled */
  if ((EnableTimelapsPhotoSave == true) && (log->GetCardDetectedStatus() == true)) {
    log->AddEvent(LogLevel_Info, F("Save TimeLaps photo to SD card"));

    /* check if folder for time laps photos exists */
    if (false == log->CheckDir(SD_MMC, TIMELAPS_PHOTO_FOLDER)) {
      log->AddEvent(LogLevel_Info, F("Create folder for TimeLaps photos"));
      log->CreateDir(SD_MMC, TIMELAPS_PHOTO_FOLDER);
    }

    /* create file name */
    String FileName = String(TIMELAPS_PHOTO_FOLDER) + "/" + String(TIMELAPS_PHOTO_PREFIX) + "_";
    FileName += log->GetSystemTime();
    FileName += TIMELAPS_PHOTO_SUFFIX;
    log->AddEvent(LogLevel_Verbose, F("Saving file: "), FileName);

    /* save photo to SD card */
    if (camera->GetPhotoExifData()->header != NULL) {
      if (log->WritePicture(FileName, camera->GetPhotoFb()->buf + camera->GetPhotoExifData()->offset, camera->GetPhotoFb()->len - camera->GetPhotoExifData()->offset, camera->GetPhotoExifData()->header, camera->GetPhotoExifData()->len) == true) {
        log->AddEvent(LogLevel_Info, F("Photo saved to SD card. EXIF"));
      } else {
        log->AddEvent(LogLevel_Error, F("Error saving photo to SD card. EXIF"));
      }

    } else {
      if (log->WritePicture(FileName, camera->GetPhotoFb()->buf, camera->GetPhotoFb()->len) == true) {
        log->AddEvent(LogLevel_Info, F("Photo saved to SD card"));
      } else {
        log->AddEvent(LogLevel_Error, F("Error saving photo to SD card"));
      }
    }
  } else if (EnableTimelapsPhotoSave == true) {
    log->AddEvent(LogLevel_Error, F("SD card not detected!"));
  }

  /* Append frame to active AVI timelapse recording.
     Write the raw JPEG directly — orientation is handled at capture time
     via sensor vflip/hmirror settings, not EXIF metadata.

     Skipped in layer-frame mode: there the PrusaLink Z trigger owns the AVI, and
     appending here as well would interleave timer frames with layer frames and
     make the playback cadence uneven. */
  if (SystemTimelapse.isRecording() && !SystemPrusaLink.LayerFramesActive()) {
    esp_task_wdt_reset();  /* SD write happens after TLS upload; refresh WDT before it */
    SystemTimelapse.appendFrame(camera->GetPhotoFb()->buf, camera->GetPhotoFb()->len);
  }

  xSemaphoreGive(camera->GetFrameBufferSemaphore());
#endif
}

/**
 * @brief Get refresh interval
 *
 * @param none
 * @return uint8_t - refresh interval
 */
uint8_t PrusaConnect::GetRefreshInterval() {
  return RefreshInterval;
}

/**
 * @brief get backend received status
 *
 * @param none
 * @return String - backend received status
 */
String PrusaConnect::GetBackendReceivedStatus() {
  return BackendReceivedStatus;
}

/**
 * @brief get token
 *
 * @param none
 * @return String - token
 */
String PrusaConnect::GetToken() {
  return Token;
}

/**
 * @brief get fingerprint
 *
 * @param none
 * @return String - fingerprint
 */
String PrusaConnect::GetFingerprint() {
  return Fingerprint;
}

/**
 * @brief get prusa connect hostname
 * 
 * @return String - hostanme
 */
String PrusaConnect::GetPrusaConnectHostname() {
  return PrusaConnectHostname;
}

/**
 * @brief Get backend availability status
 *
 * @param none
 * @return BackendAvailabilitStatus - backend status
 */
BackendAvailabilitStatus PrusaConnect::GetBackendAvailabilitStatus() {
  return BackendAvailability;
}

/** 
 * @brief Convert backend availability status to string
 * @param BackendAvailabilitStatus - backend status
 * @return String - backend status as string
*/
String PrusaConnect::CovertBackendAvailabilitStatusToString(BackendAvailabilitStatus i_data) {
  String ret = "";
  switch (i_data) {
    case WaitForFirstConnection:
      ret = F("Wait for first connection");
      break;
    case BackendAvailable:
      ret = F("Backend available");
      break;
    case BackendUnavailable:
      ret = F("Backend unavailable");
      break;
    default:
      ret = F("Unknown");
      break;
  }

  return ret;
}

bool PrusaConnect::GetTimeLapsPhotoSaveStatus() {
  return EnableTimelapsPhotoSave;
}

/**
 * @brief Increase sending interval counter
 *
 * @param none
 * @return none
 */
void PrusaConnect::IncreaseSendingIntervalCounter() {
  SendingIntervalCounter++;
}

/**
 * @brief Set sending interval counter
 *
 * @param uint8_t i_data - counter
 * @return none
 */
void PrusaConnect::SetSendingIntervalCounter(uint8_t i_data) {
  SendingIntervalCounter = i_data;
}

void PrusaConnect::SetSendingIntervalExpired() {
  SendingIntervalCounter = RefreshInterval;
}

/**
 * @brief Get sending interval counter
 * 
 * @return uint8_t - counter
 */
uint8_t PrusaConnect::GetSendingIntervalCounter() {
  return SendingIntervalCounter;
}

/**
 * @brief Enable or disable upload to Prusa Connect
 */
void PrusaConnect::SetUploadEnabled(bool i_data) {
  UploadEnabled = i_data;
  config->SaveUploadEnable(i_data);
  if (i_data) {
    SendDeviceInformationToBackend = true;  /* re-send device info on re-enable */
  }
}

bool PrusaConnect::GetUploadEnabled() const {
  return UploadEnabled;
}

void PrusaConnect::SetPrinterGate(bool open) {
  _printerGateOpen = open;
}


/**
 * @brief Check if sending interval is expired. and can I send the data to the backend. [seconds]
 * 
 * @return true 
 * @return false 
 */
bool PrusaConnect::CheckSendingIntervalExpired() {
  bool ret = false;
  if (SendingIntervalCounter >= RefreshInterval) {
    ret = true;
  }

  return ret;
}

/* EOF */