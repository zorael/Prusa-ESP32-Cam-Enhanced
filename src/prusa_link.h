/**
   @file prusa_link.h

   @brief PrusaLink local API poller — auto-starts/stops AVI timelapse
          based on print state reported by the printer's local HTTP API.
*/

#pragma once

#include <Arduino.h>
#include <math.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "cfg.h"
#include "log.h"
#include "timelapse.h"
#include "var.h"

enum TlTriggerMode_e : uint8_t {
  TL_TRIGGER_MANUAL     = 0,  ///< timelapse started/stopped by physical button only
  TL_TRIGGER_PRUSA_LINK = 1,  ///< timelapse auto-starts/stops with print job
};

enum TlFrameMode_e : uint8_t {
  TL_FRAME_TIME  = 0,  ///< one frame per photo cycle (time-based) — original behaviour
  TL_FRAME_LAYER = 1,  ///< one frame per detected layer change, from PrusaLink axis_z
};

class PrusaLink {
public:
  PrusaLink(Configuration*, Logs*);

  void LoadCfgFromEeprom();
  void Poll();

  void SetEnable(bool);
  void SetIp(String);
  void SetApiKey(String);
  void SetTriggerMode(uint8_t v);
  void SetStopUploadOnDone(bool v);
  void SetFrameMode(uint8_t v);

  /* true when layer-triggered frames are in charge, so the time-based capture
     cycle must not also append to the AVI */
  bool LayerFramesActive() const { return _frameMode == TL_FRAME_LAYER && _enable; }
  /* poll faster while a print is running so layers are not missed */
  uint32_t PollIntervalMs() const {
    return (_lastState == "PRINTING" && _frameMode == TL_FRAME_LAYER) ? TL_LAYER_POLL_MS : TL_IDLE_POLL_MS;
  }

  bool    GetEnable()      const { return _enable; }
  String  GetIp()          const { return _ip; }
  String  GetLastState()   const { return _lastState; }
  uint8_t GetTriggerMode() const { return _triggerMode; }
  bool    GetReachable()          const { return _reachable; }
  int     GetProgress()           const { return _progress; }
  float   GetNozzleTemp()         const { return _nozzleTemp; }
  float   GetBedTemp()            const { return _bedTemp; }
  bool    GetStopUploadOnDone()   const { return _stopUploadOnDone; }
  uint8_t GetFrameMode()          const { return _frameMode; }
  float   GetAxisZ()              const { return _axisZ; }
  uint32_t GetLayerFrames()       const { return _layerFrames; }
  float   GetLayerHeight()        const { return _layerHeight; }
  String  GetJobName()            const { return _jobName; }

private:
  bool    _enable      = false;
  String  _ip;
  String  _apiKey;
  String  _lastState;           ///< "" = unknown (skip auto-start on first boot)
  uint8_t _triggerMode        = TL_TRIGGER_MANUAL;
  bool    _stopUploadOnDone   = false;  ///< disable uploads when print finishes
  bool    _reachable          = false;
  int     _progress    = -1;    ///< 0–100; -1 = not available
  float   _nozzleTemp  = -1.0f;
  float   _bedTemp     = -1.0f;

  /* layer-change detection */
  uint8_t  _frameMode    = TL_FRAME_TIME;
  float    _axisZ        = NAN;     ///< Z from the most recent poll; NAN = no valid reading
  float    _prevPollZ    = NAN;     ///< Z from the poll before that (confirmation sample)
  float    _lastLayerZ   = NAN;     ///< Z at which the last frame was emitted
  uint32_t _layerFrames  = 0;       ///< frames emitted by the layer trigger this session
  float    _maxLayerZ    = NAN;     ///< highest Z a frame was taken at, for the end-of-print ratio
  float    _layerHeight  = NAN;     ///< from file.meta.layer_height; NAN = not published by the printer
  String   _jobName;                ///< file.display_name

  void CheckLayerChange(const String &state);
  void FetchJobInfo();              ///< one GET /api/v1/job at print start — layer height + file name
  void ReportCaptureRatio();        ///< end-of-print: layers expected vs frames captured

  /* Detection threshold. Derived from the real layer height when the printer
     publishes it, so the same code suits a 0.05 mm and a 0.3 mm print; the fixed
     fallback is only used when meta.layer_height is absent (it is optional). */
  float LayerDeltaMm() const {
    if (isnan(_layerHeight) || _layerHeight <= 0.0f) return TL_LAYER_MIN_DELTA_MM;
    float d = _layerHeight * 0.5f;
    return (d < 0.02f) ? 0.02f : d;
  }

  Configuration *_config;
  Logs          *_log;
};

extern PrusaLink SystemPrusaLink;

/* EOF */
