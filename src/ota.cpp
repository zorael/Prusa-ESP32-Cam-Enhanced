/**
   @file ota.cpp

   @brief On-demand firmware update from GitHub releases. See ota.h for why this is
          not a poller.
*/

#include "ota.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_task_wdt.h>

#include "mcu_cfg.h"
#include "Certificate_ota.h"
#include "timelapse.h"

OtaUpdate SystemOta(&SystemLog);

/**
 * @brief Numeric version comparison, e.g. "1.1.10" > "1.1.9".
 *
 * A string compare gets that pair backwards, which is exactly the case where a
 * user most needs the update to be offered.
 */
bool OtaUpdate::IsNewer(const String &candidate, const String &current) {
  auto part = [](const String &v, int idx) -> long {
    int start = 0;
    for (int i = 0; i < idx; i++) {
      int dot = v.indexOf('.', start);
      if (dot < 0) return 0;
      start = dot + 1;
    }
    int dot = v.indexOf('.', start);
    return (dot < 0 ? v.substring(start) : v.substring(start, dot)).toInt();
  };
  String a = candidate; if (a.startsWith("v") || a.startsWith("V")) a = a.substring(1);
  String b = current;   if (b.startsWith("v") || b.startsWith("V")) b = b.substring(1);
  for (int i = 0; i < 3; i++) {
    long x = part(a, i), y = part(b, i);
    if (x != y) return x > y;
  }
  return false;
}

void OtaUpdate::RequestCheck() {
  if (Busy()) return;
  _checkRequested = true;
  _state = OtaChecking;
  _message = F("Checking...");
}

bool OtaUpdate::RequestInstall() {
  if (Busy()) return false;
  if (_state != OtaAvailable || _assetUrl.length() == 0) {
    _message = F("Nothing to install — run a check first");
    return false;
  }
  /* An update reboots the MCU, and an AVI is only playable once finalised, so
     installing mid-recording would truncate the file. Same reasoning as the WiFi
     recovery ladder deferring its reboot. */
  if (SystemTimelapse.isRecording()) {
    _message = F("Refusing: timelapse is recording. Stop it first.");
    _state = OtaFailed;
    return false;
  }
  _installRequested = true;
  _state = OtaInstalling;
  _progress = 0;
  _message = F("Starting download...");
  return true;
}

void OtaUpdate::Service() {
  if (_checkRequested) {
    _checkRequested = false;
    DoCheck();
  } else if (_installRequested) {
    _installRequested = false;
    DoInstall();
  }
}

void OtaUpdate::DoCheck() {
  if (WiFi.status() != WL_CONNECTED) {
    _state = OtaFailed;
    _message = F("No WiFi connection");
    return;
  }

  WiFiClientSecure client;
  client.setCACert(root_CAs_ota);
  client.setConnectionTimeout(CONNECT_SOCKET_TIMEOUT_MS);
  client.setHandshakeTimeout(CONNECT_HANDSHAKE_TIMEOUT_S);

  HTTPClient https;
  String url = String("https://") + OTA_API_HOST + OTA_API_PATH;
  _log->AddEvent(LogLevel_Info, "OTA: checking " + url);

  esp_task_wdt_reset();
  if (!https.begin(client, url)) {
    _state = OtaFailed; _message = F("Connection setup failed");
    return;
  }
  /* Renaming the repository makes api.github.com answer the old path with a 301 to
     an id-based URL. Without this the check fails with a bare "HTTP 301" and OTA
     quietly stops working for every device already in the field. */
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  /* GitHub rejects requests without a User-Agent with 403. */
  https.addHeader("User-Agent", "ESP32-PrusaCam");
  https.addHeader("Accept", "application/vnd.github+json");
  https.setTimeout(OTA_HTTP_TIMEOUT_MS);

  int code = https.GET();
  esp_task_wdt_reset();

  if (code != 200) {
    https.end();
    _state = OtaFailed;
    /* 404 on a private repo is the common case: release assets need auth there,
       which is why this expects a public repository. */
    if (code == 404)      _message = F("404 — no releases, or the repository is private");
    else if (code == 403) _message = F("403 — GitHub rate limit (60/hour per IP)");
    else                  _message = "HTTP " + String(code);
    _log->AddEvent(LogLevel_Warning, "OTA: check failed, " + _message);
    return;
  }

  String body = https.getString();
  https.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    _state = OtaFailed; _message = F("Could not parse the release JSON");
    return;
  }

  const char *tag = doc["tag_name"];
  if (!tag) {
    _state = OtaFailed; _message = F("Release has no tag_name");
    return;
  }
  _latest = String(tag);

  /* Pick the asset for this board. A release carries one binary per board, so
     matching on the exact filename avoids installing another board's image. */
  _assetUrl = "";
  for (JsonObject a : doc["assets"].as<JsonArray>()) {
    const char *name = a["name"];
    if (name && String(name) == OTA_ASSET_NAME) {
      const char *u = a["browser_download_url"];
      if (u) _assetUrl = String(u);
      break;
    }
  }

  if (!IsNewer(_latest, SW_VERSION)) {
    _state = OtaUpToDate;
    _message = "Running the latest version (" + String(SW_VERSION) + ")";
  } else if (_assetUrl.length() == 0) {
    _state = OtaFailed;
    _message = _latest + " exists but has no " + String(OTA_ASSET_NAME);
  } else {
    _state = OtaAvailable;
    _message = "Version " + _latest + " is available";
  }
  _log->AddEvent(LogLevel_Info, "OTA: " + _message);
}

void OtaUpdate::DoInstall() {
  if (WiFi.status() != WL_CONNECTED) {
    _state = OtaFailed; _message = F("No WiFi connection");
    return;
  }

  WiFiClientSecure client;
  client.setCACert(root_CAs_ota);
  client.setConnectionTimeout(CONNECT_SOCKET_TIMEOUT_MS);
  client.setHandshakeTimeout(CONNECT_HANDSHAKE_TIMEOUT_S);

  HTTPClient https;
  _log->AddEvent(LogLevel_Info, "OTA: downloading " + _assetUrl);
  if (!https.begin(client, _assetUrl)) {
    _state = OtaFailed; _message = F("Download setup failed");
    return;
  }
  https.addHeader("User-Agent", "ESP32-PrusaCam");
  https.setTimeout(OTA_HTTP_TIMEOUT_MS);
  /* Release downloads 302 to objects.githubusercontent.com — a different host, which
     is why the pinned bundle carries both chains. */
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  esp_task_wdt_reset();
  int code = https.GET();
  if (code != 200) {
    https.end();
    _state = OtaFailed; _message = "Download failed, HTTP " + String(code);
    return;
  }

  int total = https.getSize();
  if (total <= 0) {
    https.end();
    _state = OtaFailed; _message = F("Server did not report a size");
    return;
  }

  /* Update::begin picks the inactive OTA partition. It writes only there, so NVS —
     WiFi credentials, Prusa Connect token — is never touched. That is the important
     difference from flashing firmware.factory.bin over serial, which does clear it. */
  if (!Update.begin(total)) {
    https.end();
    _state = OtaFailed;
    _message = "No room for a " + String(total) + " byte image";
    return;
  }

  WiFiClient *stream = https.getStreamPtr();
  uint8_t buf[OTA_CHUNK_BYTES];
  int written = 0;
  uint32_t lastData = millis();

  while (written < total) {
    esp_task_wdt_reset();
    size_t avail = stream->available();
    if (avail == 0) {
      if (millis() - lastData > OTA_STALL_TIMEOUT_MS) {
        Update.abort(); https.end();
        _state = OtaFailed; _message = F("Download stalled");
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    lastData = millis();

    size_t n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
    if (n == 0) continue;
    if (Update.write(buf, n) != n) {
      Update.abort(); https.end();
      _state = OtaFailed; _message = F("Flash write failed");
      return;
    }
    written += n;
    _progress = (uint8_t)((uint64_t)written * 100 / total);
  }
  https.end();

  if (!Update.end(true)) {
    _state = OtaFailed;
    _message = "Image rejected: " + String(Update.errorString());
    _log->AddEvent(LogLevel_Error, "OTA: " + _message);
    return;
  }

  _state = OtaSuccess;
  _progress = 100;
  _message = "Installed " + _latest + " — reboot to run it";
  _log->AddEvent(LogLevel_Info, "OTA: " + _message);
}

/* EOF */
