/**
   @file prusa_link.cpp

   @brief PrusaLink local API poller — auto-starts/stops AVI timelapse
          based on print state reported by the printer's local HTTP API.

   Endpoint polled: GET http://<printer-ip>/api/v1/status
   Auth header:     X-Api-Key: <key>
   Relevant field:  response["printer"]["state"] — "PRINTING" triggers begin(),
                    any other state after PRINTING triggers finalise().
*/

#include "prusa_link.h"
#include <WiFi.h>
#include <math.h>

PrusaLink SystemPrusaLink(&SystemConfig, &SystemLog);

PrusaLink::PrusaLink(Configuration* cfg, Logs* log)
  : _config(cfg), _log(log) {}

void PrusaLink::LoadCfgFromEeprom() {
  _enable             = _config->LoadPrusaLinkEnable();
  _ip                 = _config->LoadPrusaLinkIp();
  _apiKey             = _config->LoadPrusaLinkApiKey();
  _triggerMode        = _config->LoadTlTriggerMode();
  _stopUploadOnDone   = _config->LoadPlStopUploadOnDone();
  _frameMode          = _config->LoadTlFrameMode();
  _log->AddEvent(LogLevel_Info, F("PrusaLink loaded. enable="), String(_enable) + " ip=" + _ip);
}

void PrusaLink::Poll() {
  if (!_enable || _ip.length() == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  /* PrusaLink's local API does not support HTTPS; plain HTTP is the only option.
     The API key is therefore visible to other hosts on the same LAN segment. */
  String url = "http://" + _ip + "/api/v1/status";
  if (!http.begin(url)) {
    _log->AddEvent(LogLevel_Warning, F("PrusaLink: http.begin failed"));
    return;
  }
  http.addHeader("X-Api-Key", _apiKey);
  http.setTimeout(1500);  /* 5000 was too long — blocks System_TaskMain for 5 s, making button unresponsive */

  int code = http.GET();
  if (code != 200) {
    _log->AddEvent(LogLevel_Warning, F("PrusaLink: status code "), String(code));
    http.end();
    _reachable = false;
    return;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err) {
    _log->AddEvent(LogLevel_Warning, F("PrusaLink: JSON parse error"));
    _reachable = false;
    return;
  }

  _reachable   = true;
  _progress    = doc["job"]["progress"]          | -1;
  _nozzleTemp  = doc["printer"]["temp_nozzle"]   | -1.0f;
  _bedTemp     = doc["printer"]["temp_bed"]      | -1.0f;
  /* NAN, not -1, for "no reading". Negative Z is a *legitimate* value — the live-Z
     offset puts the nozzle below the Z=0 reference, and -0.2 was observed during a
     real first layer. A `< 0` validity test silently discards those. */
  {
    JsonVariant zv = doc["printer"]["axis_z"];
    float z = zv.isNull() ? NAN : zv.as<float>();
    /* reject only physically impossible values (unhomed axes report things like -412) */
    _axisZ = (isnan(z) || z < PL_AXIS_Z_MIN_MM || z > PL_AXIS_Z_MAX_MM) ? NAN : z;
  }

  const char* stateRaw = doc["printer"]["state"];
  if (!stateRaw) return;
  String state = String(stateRaw);
  _log->AddEvent(LogLevel_Verbose, F("PrusaLink: printer state="), state);

  CheckLayerChange(state);

  /* state transitions — only auto-trigger if printer trigger mode is active.
     Post to g_captureQueue so the worker task executes start/stop serially
     with the ongoing capture/SD cycle instead of calling timelapse directly
     from this task and racing on the timelapse mutex and SD bus.

     "Active print" = PRINTING | PAUSED | ATTENTION.
       PAUSED / ATTENTION (filament change, MMU) are temporary holds — the
       session is still live, do not stop the timelapse.
     "Print ended"  = any transition OUT of an active state (to FINISHED,
       STOPPED, IDLE, ERROR, …).  FINISHED is the normal completion path;
       waiting for IDLE would delay the stop until the user clears the screen.
     Guard _lastState non-empty so the first poll after boot never fires a
     false "just ended" against an idle printer. */
  auto isActive = [](const String& s) {
    return s == "PRINTING" || s == "PAUSED" || s == "ATTENTION";
  };
  bool printJustStarted = (!isActive(_lastState) && state == "PRINTING");
  bool printJustEnded   = (!_lastState.isEmpty() && isActive(_lastState) && !isActive(state));

  if (_triggerMode == TL_TRIGGER_PRUSA_LINK) {
    if (printJustStarted) {
      if (!SystemTimelapse.isRecording()) {
        _log->AddEvent(LogLevel_Info, F("PrusaLink: print started — queuing TL_START"));
        CaptureCommand_t cmd = CMD_TL_START;
        xQueueSend(g_captureQueue, &cmd, 0);
      }
    } else if (printJustEnded) {
      if (SystemTimelapse.isRecording()) {
        ReportCaptureRatio();
        _log->AddEvent(LogLevel_Info, F("PrusaLink: print ended ("), state + F(") — queuing TL_STOP"));
        CaptureCommand_t cmd = CMD_TL_STOP;
        xQueueSendToFront(g_captureQueue, &cmd, pdMS_TO_TICKS(200));
      }
    }
  }

  /* upload auto-control: when stopUploadOnDone is enabled, disable at print end
     and re-enable when the next print starts so the cycle is fully automatic. */
  if (_stopUploadOnDone) {
    if (printJustEnded) {
      _log->AddEvent(LogLevel_Info, F("PrusaLink: print ended — queuing UPLOAD_DISABLE"));
      CaptureCommand_t cmd = CMD_UPLOAD_DISABLE;
      xQueueSend(g_captureQueue, &cmd, 0);
    } else if (printJustStarted) {
      _log->AddEvent(LogLevel_Info, F("PrusaLink: print started — queuing UPLOAD_ENABLE"));
      CaptureCommand_t cmd = CMD_UPLOAD_ENABLE;
      xQueueSend(g_captureQueue, &cmd, 0);
    }
  }

  _lastState = state;
}

/**
 * @brief Fetch layer height and file name once, at print start.
 *
 * meta.layer_height lives in GET /api/v1/job, not in the /api/v1/status response the
 * poller uses — status carries only StatusJob (id, progress, time_remaining,
 * time_printing). Doing this on the print-start transition rather than every poll
 * keeps it to one extra request per print instead of one per 5 s.
 *
 * Every field in PrintFileMetadata is optional in the spec, so absence is normal and
 * not an error: LayerDeltaMm() falls back to the fixed threshold.
 */
/**
 * @brief Recover layer height from the slicer's filename.
 *
 * meta.layer_height is optional in the PrusaLink spec and is genuinely absent for
 * .bgcode files on this printer. PrusaSlicer does however encode it in the output
 * name: "cone_mount_0.4n_0.25mm_PETG_COREONE_3h27m[1].bgcode" -> 0.25.
 *
 * A decimal point is required, which is what keeps this from matching things like
 * "100mm_cube" — layer heights are always fractional, object dimensions usually
 * are not. Values outside a plausible range are rejected.
 *
 * @return layer height in mm, or NAN if the name yields nothing credible
 */
static float LayerHeightFromName(const String &name) {
  int idx = 0;
  while (true) {
    int mm = name.indexOf("mm", idx);
    if (mm < 0) break;

    int s = mm - 1;
    bool sawDot = false, sawDigit = false;
    while (s >= 0) {
      char c = name.charAt(s);
      if (c >= '0' && c <= '9')      { sawDigit = true;  s--; }
      else if (c == '.' && !sawDot)  { sawDot   = true;  s--; }
      else break;
    }

    if (sawDigit && sawDot) {
      float v = name.substring(s + 1, mm).toFloat();
      if (v > 0.01f && v < 2.0f) return v;
    }
    idx = mm + 2;
  }
  return NAN;
}

void PrusaLink::FetchJobInfo() {
  _layerHeight = NAN;
  _jobName     = "";
  if (!_enable || _ip.length() == 0 || WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "http://" + _ip + "/api/v1/job";
  if (!http.begin(url)) return;
  http.addHeader("X-Api-Key", _apiKey);
  http.setTimeout(1500);

  int code = http.GET();
  if (code != 200) {
    _log->AddEvent(LogLevel_Warning, F("PrusaLink: /job status "), String(code));
    http.end();
    return;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    _log->AddEvent(LogLevel_Warning, F("PrusaLink: /job JSON parse error"));
    return;
  }

  JsonVariant lh = doc["file"]["meta"]["layer_height"];
  if (!lh.isNull()) {
    float v = lh.as<float>();
    if (v > 0.0f && v < 5.0f) _layerHeight = v;   /* sanity: real layers are sub-mm */
  }
  const char* dn = doc["file"]["display_name"] | doc["file"]["name"];
  if (dn) _jobName = String(dn);

  /* hand the name to the recorder so the AVI is identifiable in the browser */
  SystemTimelapse.setJobName(_jobName);

  const char *src = "meta";
  if (isnan(_layerHeight) && _jobName.length() > 0) {
    _layerHeight = LayerHeightFromName(_jobName);
    src = "filename";
  }

  if (isnan(_layerHeight)) {
    _log->AddEvent(LogLevel_Warning, F("PrusaLink: layer height unknown (no meta, no filename hint) — fixed threshold"));
  } else {
    _log->AddEvent(LogLevel_Info, "PrusaLink: job '" + _jobName + "' layer_height="
                                  + String(_layerHeight, 3) + "mm from " + String(src)
                                  + ", delta=" + String(LayerDeltaMm(), 3) + "mm");
  }
}

/**
 * @brief Log how many layers the print had versus how many frames were captured.
 *
 * This is the number that says whether the poll interval keeps up, and it cannot be
 * derived without layer_height — which is exactly why it is worth fetching.
 */
void PrusaLink::ReportCaptureRatio() {
  if (_layerFrames == 0) return;
  if (isnan(_layerHeight) || isnan(_maxLayerZ) || _layerHeight <= 0.0f) {
    _log->AddEvent(LogLevel_Info, "PrusaLink: captured " + String(_layerFrames)
                                  + " layer frames (layer height unknown, ratio N/A)");
    return;
  }
  uint32_t expected = (uint32_t)(_maxLayerZ / _layerHeight + 0.5f);
  uint32_t pct = (expected > 0) ? (_layerFrames * 100U / expected) : 0U;
  _log->AddEvent(LogLevel_Info, "PrusaLink: capture ratio " + String(_layerFrames) + "/"
                                + String(expected) + " layers (" + String(pct) + "%) to Z="
                                + String(_maxLayerZ, 2) + "mm");
}

/**
 * @brief Emit a timelapse frame when the printer starts a new layer.
 *
 * Z-hop is the thing that makes this non-trivial: during travel moves the nozzle
 * lifts by 0.2-0.6 mm, which is larger than a layer and would otherwise fire a
 * frame on every travel. The discriminator used here is duration, not magnitude —
 * a hop is momentary, a real layer holds its Z for the whole layer. So a height is
 * only accepted once two consecutive polls agree on it (within TL_LAYER_Z_EPSILON_MM).
 * TL_LAYER_POLL_MS must stay well below a layer's print time for that to hold.
 *
 * The frame itself is not captured here — this runs on a housekeeping task and must
 * not touch the camera. A CMD_TL_FRAME goes on g_captureQueue so the capture task
 * performs it serially with everything else it owns.
 */
void PrusaLink::CheckLayerChange(const String &state) {
  if (_frameMode != TL_FRAME_LAYER) return;

  /* A new print must always restart the layer baseline.
     Observed failing without this: at the moment the printer reports PRINTING it has
     not homed yet and still reports the *previous* print's final Z (168.3 mm here).
     Two polls agreed on it, so it was confirmed as the session baseline and every
     real layer (0.2, 0.4, …) sat below it — exactly one frame for the whole print.
     CheckLayerChange runs before _lastState is updated, so this sees the edge. */
  if (state == "PRINTING" && _lastState != "PRINTING") {
    _prevPollZ   = NAN;
    _lastLayerZ  = NAN;
    _maxLayerZ   = NAN;
    _layerFrames = 0;
    _log->AddEvent(LogLevel_Info, F("PrusaLink: print started — layer baseline reset"));
    FetchJobInfo();   /* one request per print: layer height + file name */
  }

  /* Only while actually laying down material. PAUSED/ATTENTION deliberately
     excluded: Z can drift during a filament change and would emit bogus frames. */
  if (state != "PRINTING") {
    _prevPollZ = NAN;                /* drop the confirmation chain */
    if (!SystemTimelapse.isRecording()) {
      _lastLayerZ  = NAN;            /* new session next time */
      _layerFrames = 0;
    }
    return;
  }

  if (isnan(_axisZ)) {
    _log->AddEvent(LogLevel_Verbose, F("PrusaLink: axis_z unavailable, layer trigger idle"));
    return;
  }
  if (!SystemTimelapse.isRecording()) return;

  const float z = _axisZ;

  /* Confirmation first, and everything downstream keys off it.
     A Z-hop is momentary, so it cannot survive two consecutive polls at the same
     height — which means any *confirmed* reading is a real, held height. That is a
     far better discriminator than comparing magnitudes, and it lets the same small
     TL_LAYER_MIN_DELTA_MM govern moves in both directions. The earlier version used
     a 1.0 mm drop threshold to spot a bad baseline; that was necessarily coarser
     than Z-hop (0.2-0.6 mm) and so blind to the sub-millimetre start-up region,
     where it left the baseline stuck at 0.4 mm while the real first layer ran at
     0.1 mm. */
  const bool confirmed = (!isnan(_prevPollZ)) && (fabsf(z - _prevPollZ) <= TL_LAYER_Z_EPSILON_MM);
  _prevPollZ = z;
  if (!confirmed) return;

  if (!isnan(_lastLayerZ)) {
    const float delta = LayerDeltaMm();
    if (z > _lastLayerZ + TL_LAYER_MAX_JUMP_MM) {
      /* No layer is this tall. This is the nozzle parking or lifting away — observed
         live at the end of a print: Z went 3.20 -> 168.00 while the state was still
         PRINTING, and it captured a frame of nothing as the final image.
         Ignore the sample entirely: no frame, and no baseline move, so when Z comes
         back down the confirmed-drop path re-baselines normally. */
      _log->AddEvent(LogLevel_Info, "PrusaLink: Z jump " + String(_lastLayerZ, 2) + "->"
                                    + String(z, 2) + "mm ignored (not a layer)");
      return;
    }
    if (z < _lastLayerZ - delta) {
      /* A confirmed *lower* height. Not a hop — those go up and do not persist.
         Means the baseline is stale: start-up settling, a re-home, or a new object
         in a sequential print. Re-baseline here and emit a frame. */
      _log->AddEvent(LogLevel_Info, "PrusaLink: Z " + String(_lastLayerZ, 2) + "->"
                                    + String(z, 2) + "mm, re-baselining layers");
    } else if (z < _lastLayerZ + delta) {
      return;                        /* same layer */
    }
  }

  _lastLayerZ = z;
  if (isnan(_maxLayerZ) || z > _maxLayerZ) _maxLayerZ = z;
  _layerFrames++;
  CaptureCommand_t cmd = CMD_TL_FRAME;
  if (xQueueSend(g_captureQueue, &cmd, 0) != pdTRUE) {
    _log->AddEvent(LogLevel_Warning, F("PrusaLink: capture queue full, layer frame dropped"));
    return;
  }
  _log->AddEvent(LogLevel_Info, "PrusaLink: layer at Z=" + String(z, 2) + "mm -> frame " + String(_layerFrames));
}

void PrusaLink::SetEnable(bool v) {
  _enable = v;
  _config->SavePrusaLinkEnable(v);
}

void PrusaLink::SetIp(String v) {
  _ip = v;
  _config->SavePrusaLinkIp(v);
}

void PrusaLink::SetApiKey(String v) {
  _apiKey = v;
  _config->SavePrusaLinkApiKey(v);
}

void PrusaLink::SetTriggerMode(uint8_t v) {
  _triggerMode = v;
  _config->SaveTlTriggerMode(v);
}

void PrusaLink::SetStopUploadOnDone(bool v) {
  _stopUploadOnDone = v;
  _config->SavePlStopUploadOnDone(v);
}

void PrusaLink::SetFrameMode(uint8_t v) {
  _frameMode = (v == TL_FRAME_LAYER) ? TL_FRAME_LAYER : TL_FRAME_TIME;
  _config->SaveTlFrameMode(_frameMode);
  /* forget any half-built confirmation state so a mode switch starts clean */
  _prevPollZ  = NAN;
  _lastLayerZ = NAN;
}

/* EOF */
