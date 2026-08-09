/**
   @file timelapse.cpp
   @brief MJPEG-in-AVI timelapse video builder implementation.

   AVI file layout written by writeAviHeader():
     offset   0: 'RIFF'
     offset   4: <riff_size>        ← patched in finalise()
     offset   8: 'AVI '
     offset  12: 'LIST'
     offset  16: 192                (hdrl chunk size, fixed)
     offset  20: 'hdrl'
     offset  24: 'avih' + 4 + MainAVIHeader[56]   (total 64 bytes)
     offset  88: 'LIST' + 4 + 'strl'
                   'strh' + 4 + AVIStreamHeader[56]
                   'strf' + 4 + BITMAPINFOHEADER[40]
     offset 212: 'LIST'
     offset 216: <movi_size>        ← patched in finalise()
     offset 220: 'movi'
     offset 224: <frame chunks begin>

   Each frame chunk:
     '00dc' [4]  chunk fourcc (compressed video)
     <len>  [4]  JPEG payload length LE
     <data> [len] raw JPEG bytes
     <pad>  [0 or 1]  word-align padding if len is odd

   idx1 appended after all frames on finalise().
*/

#include "timelapse.h"
#include "prusa_link.h"
#include "camera.h"   // SystemCamera — dimensions read in begin()
#include "log.h"      // SystemLog
#include "cfg.h"      // SystemConfig — persist recording state across reboots
#include <esp_task_wdt.h>
#include <esp_system.h>

extern Camera        SystemCamera;
extern Logs          SystemLog;
extern Configuration SystemConfig;

TimelapseBuilder SystemTimelapse;

/* ── flash feedback ───────────────────────────────────────────────────────── */

static void flashBlink(int count, int onMs = 150, int offMs = 100) {
  bool wasOn = SystemCamera.GetFlashStatus();
  if (wasOn) SystemCamera.SetFlashStatus(false);
  for (int i = 0; i < count; i++) {
    SystemCamera.SetFlashStatus(true);
    delay(onMs);
    SystemCamera.SetFlashStatus(false);
    if (i < count - 1) delay(offMs);
  }
  if (wasOn) SystemCamera.SetFlashStatus(true);
}

/* ── helpers ──────────────────────────────────────────────────────────────── */

bool TimelapseBuilder::writeU32LE(uint32_t v) {
  uint8_t b[4] = {
    (uint8_t)(v),
    (uint8_t)(v >> 8),
    (uint8_t)(v >> 16),
    (uint8_t)(v >> 24)
  };
  return _file.write(b, 4) == 4;  /* return success so callers can detect SD-full */
}

bool TimelapseBuilder::patchU32LE(uint32_t fileOffset, uint32_t val) {
  uint32_t cur = _file.position();
  if (!_file.seek(fileOffset)) return false;
  bool ok = writeU32LE(val);
  _file.seek(cur);
  return ok;
}

/* ── AVI header ───────────────────────────────────────────────────────────── */

/*
  MainAVIHeader (56 bytes, AVIMAINHEADER):
    dwMicroSecPerFrame  [4]
    dwMaxBytesPerSec    [4]
    dwPaddingGranularity[4]
    dwFlags             [4]
    dwTotalFrames       [4]  ← patched in finalise()
    dwInitialFrames     [4]
    dwStreams            [4]
    dwSuggestedBufferSize[4]
    dwWidth             [4]
    dwHeight            [4]
    dwReserved[4]       [16]

  AVIStreamHeader (56 bytes, AVISTREAMHEADER):
    fccType             [4]  'vids'
    fccHandler          [4]  'MJPG'
    dwFlags             [4]
    wPriority           [2]
    wLanguage           [2]
    dwInitialFrames     [4]
    dwScale             [4]  1
    dwRate              [4]  fps
    dwStart             [4]
    dwLength            [4]  ← patched in finalise() (= frame count)
    dwSuggestedBufferSize[4]
    dwQuality           [4]
    dwSampleSize        [4]
    rcFrame             [8]  (left,top,right,bottom) shorts

  BITMAPINFOHEADER (40 bytes):
    biSize              [4]  40
    biWidth             [4]
    biHeight            [4]
    biPlanes            [2]  1
    biBitCount          [2]  24
    biCompression       [4]  'MJPG'
    biSizeImage         [4]
    biXPelsPerMeter     [4]
    biYPelsPerMeter     [4]
    biClrUsed           [4]
    biClrImportant      [4]
*/

static void writeChunkTag(File &f, const char tag[4]) {
  f.write((const uint8_t*)tag, 4);
}

void TimelapseBuilder::writeAviHeader() {
  const uint32_t fps          = TIMELAPS_AVI_FPS;
  const uint32_t usecPerFrame = 1000000UL / fps;
  const uint32_t w            = _width;
  const uint32_t h            = _height;

  /* RIFF header */
  writeChunkTag(_file, "RIFF");
  writeU32LE(0);            /* riff_size — patched later, offset 4 */
  writeChunkTag(_file, "AVI ");

  /* LIST hdrl — size = 4('hdrl') + 64(avih) + 8+4+64+48(strl) = 192 */
  writeChunkTag(_file, "LIST");
  writeU32LE(192);
  writeChunkTag(_file, "hdrl");

  /* avih (MainAVIHeader, 56 bytes) */
  writeChunkTag(_file, "avih");
  writeU32LE(56);
  writeU32LE(usecPerFrame);      /* dwMicroSecPerFrame */
  writeU32LE(w * h * 3);         /* dwMaxBytesPerSec   */
  writeU32LE(0);                 /* dwPaddingGranularity */
  writeU32LE(0x00000910);        /* dwFlags: AVIF_HASINDEX | AVIF_MUSTUSEINDEX */
  writeU32LE(0);                 /* dwTotalFrames — patched at offset 48 */
  writeU32LE(0);                 /* dwInitialFrames */
  writeU32LE(1);                 /* dwStreams */
  writeU32LE(w * h * 3);         /* dwSuggestedBufferSize */
  writeU32LE(w);                 /* dwWidth */
  writeU32LE(h);                 /* dwHeight */
  writeU32LE(0); writeU32LE(0); writeU32LE(0); writeU32LE(0); /* dwReserved[4] */

  /* LIST strl */
  writeChunkTag(_file, "LIST");
  writeU32LE(116);               /* strl_size = 4+'strh'(64)+'strf'(48) */
  writeChunkTag(_file, "strl");

  /* strh (AVIStreamHeader, 56 bytes) */
  writeChunkTag(_file, "strh");
  writeU32LE(56);
  writeChunkTag(_file, "vids");  /* fccType  */
  writeChunkTag(_file, "MJPG");  /* fccHandler */
  writeU32LE(0);                 /* dwFlags */
  writeU32LE(0);                 /* wPriority + wLanguage */
  writeU32LE(0);                 /* dwInitialFrames */
  writeU32LE(1);                 /* dwScale */
  writeU32LE(fps);               /* dwRate  */
  writeU32LE(0);                 /* dwStart */
  writeU32LE(0);                 /* dwLength — patched in finalise() at offset 140+4+4=? */
  writeU32LE(w * h * 3);         /* dwSuggestedBufferSize */
  writeU32LE(0xFFFFFFFF);        /* dwQuality */
  writeU32LE(0);                 /* dwSampleSize */
  writeU32LE(0); writeU32LE((h << 16) | w); /* rcFrame (left=0,top=0,right=w,bottom=h packed) */

  /* strf (BITMAPINFOHEADER, 40 bytes) */
  writeChunkTag(_file, "strf");
  writeU32LE(40);
  writeU32LE(40);                /* biSize */
  writeU32LE(w);                               /* biWidth */
  writeU32LE(h);                               /* biHeight: positive = standard MJPEG-in-AVI convention */
  writeU32LE(0x00180001);        /* biPlanes=1, biBitCount=24 */
  writeChunkTag(_file, "MJPG"); /* biCompression */
  writeU32LE(w * h * 3);         /* biSizeImage */
  writeU32LE(0);                 /* biXPelsPerMeter */
  writeU32LE(0);                 /* biYPelsPerMeter */
  writeU32LE(0);                 /* biClrUsed */
  writeU32LE(0);                 /* biClrImportant */

  /* movi LIST header — size patched in finalise() */
  _moviSizeOffset = _file.position() + 4;  /* +4 skips 'LIST' tag */
  writeChunkTag(_file, "LIST");
  writeU32LE(0);                 /* movi_size — patched later */
  writeChunkTag(_file, "movi");
  _moviDataStart = _file.position();       /* offset 224 */
}

/* ── filename ─────────────────────────────────────────────────────────────── */

/**
 * @brief Reduce a gcode filename to something safe and short for FAT.
 *
 * "cone_mount_0.4n_0.25mm_PETG_COREONE_3h27m[1].bgcode"
 *   -> "cone_mount_0.4n_0.25mm_PETG_CORE"
 *
 * Dots and dashes are kept deliberately — they carry the meaning in slicer names
 * (0.4n, 0.25mm) and FAT long filenames permit both. Everything else outside
 * [A-Za-z0-9] becomes '_', with runs collapsed so "[1]" does not become "___".
 */
static String SanitiseJobName(const String &raw) {
  /* drop any directory part and the gcode extension */
  int slash = raw.lastIndexOf('/');
  String s = (slash >= 0) ? raw.substring(slash + 1) : raw;
  int dot = s.lastIndexOf('.');
  if (dot > 0) {
    String ext = s.substring(dot);
    ext.toLowerCase();
    if (ext == ".bgcode" || ext == ".gcode" || ext == ".gco" || ext == ".g") {
      s = s.substring(0, dot);
    }
  }

  String out;
  out.reserve(TL_NAME_JOB_MAX_CHARS + 1);
  bool lastWasUnderscore = false;
  for (size_t i = 0; i < s.length() && out.length() < TL_NAME_JOB_MAX_CHARS; i++) {
    char c = s.charAt(i);
    bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '.' || c == '-';
    if (keep) {
      out += c;
      lastWasUnderscore = false;
    } else if (!lastWasUnderscore && out.length() > 0) {
      out += '_';
      lastWasUnderscore = true;
    }
  }
  while (out.length() > 0 && out.charAt(out.length() - 1) == '_') {
    out.remove(out.length() - 1);
  }
  return out;
}

String TimelapseBuilder::buildFileName() const {
  /* Timestamp first so the name sorts chronologically on its own, then the job name
     so a file is identifiable in the browser without opening it. */
  String stamp;
  struct tm t;
  if (getLocalTime(&t, 0)) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
    stamp = String(buf);
  } else {
    /* NTP not yet synced — uptime ms keeps names unique within a session */
    char buf[24];
    snprintf(buf, sizeof(buf), "ms%010lu", (unsigned long)millis());
    stamp = String(buf);
  }

  String name = String(TIMELAPS_AVI_FOLDER) + "/" + TIMELAPS_AVI_PREFIX + stamp;
  String job  = SanitiseJobName(_jobName);
  if (job.length() > 0) {
    name += "_";
    name += job;
  }

  /* Part number. The timestamp alone has one-second resolution, so with
     segmentation enabled two segments closing inside the same second would collide —
     and SD_MMC.open(FILE_WRITE) on an existing name truncates it, silently
     destroying the previous part. _segmentCount always increments, so this
     guarantees uniqueness. (It was declared as "drives filename suffix" but was
     never actually used in the name.) */
  char part[8];
  snprintf(part, sizeof(part), "_p%03u", (unsigned)(_segmentCount % 1000));
  name += part;

  name += TIMELAPS_AVI_SUFFIX;
  return name;
}

/* ── download-active flag ─────────────────────────────────────────────────── */

void TimelapseBuilder::setDownloadActive(bool active, size_t fileSizeBytes) {
  (void)fileSizeBytes;   /* no longer used — see below */
  _downloadActive = active;
  _downloadLastActivity = millis();
}

/**
 * @brief Mark forward progress on the active download.
 *
 * Replaces a predicted total duration with observed liveness. The old scheme set a
 * deadline of 120 s + fileSize/50, which for a 43 MB file is ~17 MINUTES — and if the
 * browser aborted, nothing cleared it, so the flag stayed set for that whole time.
 * Now the download is considered dead after TL_DOWNLOAD_IDLE_MS without a chunk,
 * regardless of file size, and onDisconnect() clears it immediately in the normal case.
 */
void TimelapseBuilder::noteDownloadProgress() {
  _downloadLastActivity = millis();
}

bool TimelapseBuilder::isDownloadActive() const {
  if (!_downloadActive) return false;
  /* stale flag from a connection that died without onDisconnect firing */
  return (millis() - _downloadLastActivity) < TL_DOWNLOAD_IDLE_MS;
}

/* ── open a new segment file ──────────────────────────────────────────────── */

bool TimelapseBuilder::openSegment() {
  _fileName = buildFileName();

  /* ensure directory exists */
  if (!SD_MMC.exists(TIMELAPS_AVI_FOLDER)) {
    if (!SD_MMC.mkdir(TIMELAPS_AVI_FOLDER)) {
      SystemLog.AddEvent(LogLevel_Error, F("Timelapse: cannot create folder"));
      return false;
    }
  }

  _file = SD_MMC.open(_fileName.c_str(), FILE_WRITE);
  if (!_file) {
    SystemLog.AddEvent(LogLevel_Error, "Timelapse: cannot open " + _fileName);
    return false;
  }

  _segFrames = 0;
  _index.clear();
  _index.reserve(_maxSegFrames > 0 ? _maxSegFrames : TIMELAPS_AVI_SEGMENT_FRAMES);  /* pre-allocate to avoid realloc under PSRAM pressure while _mutex is held; cap unlimited to compile-time default */
  writeAviHeader();
  SystemLog.AddEvent(LogLevel_Info, "Timelapse: opened " + _fileName);
  return true;
}

/* ── public API ───────────────────────────────────────────────────────────── */

TimelapseBuilder::TimelapseBuilder() {
  _mutex = xSemaphoreCreateMutex();
}


/**
 * @brief Is the device far enough through start-up to open a recording?
 *
 * The filename is fixed at the moment the file is created, so anything not yet known
 * is lost for the whole recording. Starting too early produced names like
 * "tl_ms0000020748_p000.avi" — no date because NTP had not synced, no job name
 * because PrusaLink had not fetched it yet.
 *
 * Checked here:
 *   - NTP synced, so the name carries a real timestamp rather than uptime millis
 *   - if PrusaLink is enabled and a print is running, its job name has arrived
 *
 * WiFi itself is deliberately NOT required beyond those: a manual recording on an
 * offline camera is legitimate, and the timeout below covers it.
 */
bool TimelapseBuilder::startConditionsMet(String *whyNot) {
  if (!SystemLog.GetNtpTimeSynced()) {
    if (whyNot) *whyNot = F("waiting for NTP time sync");
    return false;
  }
  if (SystemPrusaLink.GetEnable() && SystemPrusaLink.GetLastState() == "PRINTING"
      && SystemPrusaLink.GetJobName().length() == 0) {
    if (whyNot) *whyNot = F("waiting for PrusaLink job name");
    return false;
  }
  return true;
}

/**
 * @brief Open a deferred recording once the device is ready, or give up waiting.
 *
 * The timeout matters: with no internet NTP never syncs, and blocking recording
 * forever would break offline use entirely. After TL_START_READY_TIMEOUT_MS it
 * starts anyway and accepts the uptime-based filename.
 */
void TimelapseBuilder::servicePendingStart() {
  if (!_startPending || _recording) return;

  String why;
  bool ready = startConditionsMet(&why);
  bool timedOut = (millis() - _startPendingSince) >= TL_START_READY_TIMEOUT_MS;
  if (!ready && !timedOut) return;

  if (!ready) {
    SystemLog.AddEvent(LogLevel_Warning,
      "Timelapse: starting anyway after " + String(TL_START_READY_TIMEOUT_MS / 1000)
      + "s (" + why + ")");
  } else {
    SystemLog.AddEvent(LogLevel_Info, F("Timelapse: start conditions met, opening file"));
  }
  _startPending = false;
  if (!begin(/*force=*/true)) {
    SystemLog.AddEvent(LogLevel_Error, F("Timelapse: deferred start failed to open a file"));
  }
}

bool TimelapseBuilder::begin(bool force) {
  /* Defer if the device is not ready yet. Gating here rather than at the call sites
     covers both the TL_START queue command and restoreFromEeprom()'s resume. */
  if (!force) {
    String why;
    if (!startConditionsMet(&why)) {
      _startPending = true;
      _startPendingSince = millis();
      SystemLog.AddEvent(LogLevel_Info, "Timelapse: start deferred - " + why);
      return true;   /* queued, not failed */
    }
  }

  xSemaphoreTake(_mutex, portMAX_DELAY);
  if (_recording) { xSemaphoreGive(_mutex); return false; }

  _width        = SystemCamera.GetFrameSizeWidth();
  _height       = SystemCamera.GetFrameSizeHeight();
  _totalFrames  = 0;
  _maxSegFrames = SystemConfig.LoadTlMaxFrames();
  /* _segmentCount keeps incrementing across begin()/finalise() pairs */

  if (_width == 0 || _height == 0) {
    SystemLog.AddEvent(LogLevel_Error, F("Timelapse: unknown frame dimensions"));
    xSemaphoreGive(_mutex);
    return false;
  }

  if (!openSegment()) { xSemaphoreGive(_mutex); return false; }

  _recording = true;
  SystemConfig.SaveTlWasRecording(true);
  SystemConfig.SaveTlSegmentCount((uint16_t)_segmentCount);

  SystemLog.AddEvent(LogLevel_Info, F("Timelapse: recording started"));
  xSemaphoreGive(_mutex);
  flashBlink(2);
  return true;
}

bool TimelapseBuilder::appendFrame(const uint8_t *jpegBuf, size_t jpegLen) {
  if (!_recording || !_file) return false;
  if (jpegBuf == nullptr || jpegLen == 0) return false;

  xSemaphoreTake(_mutex, portMAX_DELAY);
  if (!_recording || !_file) { xSemaphoreGive(_mutex); return false; }

  uint32_t framePos = _file.position();

  _file.write((const uint8_t*)"00dc", 4);
  writeU32LE((uint32_t)jpegLen);
  esp_task_wdt_reset();  /* large JPEG write may block on slow SD cards */
  _file.write(jpegBuf, jpegLen);
  if (jpegLen & 1) {
    uint8_t pad = 0;
    _file.write(&pad, 1);
  }

  _index.push_back({ framePos - _moviDataStart, (uint32_t)jpegLen });
  _segFrames++;
  _totalFrames++;

  if (_maxSegFrames > 0 && _segFrames >= _maxSegFrames) {
    esp_task_wdt_reset();  /* segment finalize + reopen involves flush/close/open */
    _finalise_unlocked();
    if (openSegment()) _recording = true;
  }

  xSemaphoreGive(_mutex);
  return true;
}

bool TimelapseBuilder::_finalise_unlocked() {
  if (!_file) return false;

  bool ok = true;

  esp_task_wdt_reset();  /* patch seeks + writes can block on slow SD */
  /* patch movi LIST size: bytes from _moviDataStart to current pos, plus 4 for 'movi' tag */
  uint32_t moviDataLen = _file.position() - _moviDataStart;
  ok &= patchU32LE(_moviSizeOffset, moviDataLen + 4);

  /* patch avih.dwTotalFrames (at fixed offset 48) and strh.dwLength (at offset 140) */
  ok &= patchU32LE(48, _segFrames);
  ok &= patchU32LE(140, _segFrames);

  /* write idx1 chunk */
  _file.seek(0, SeekEnd);
  ok &= (_file.write((const uint8_t*)"idx1", 4) == 4);
  ok &= writeU32LE((uint32_t)(_index.size() * 16));
  for (auto &e : _index) {
    esp_task_wdt_reset();  /* idx1 loop can be hundreds of iterations on large recordings */
    ok &= (_file.write((const uint8_t*)"00dc", 4) == 4);
    ok &= writeU32LE(0x00000010);        /* AVIIF_KEYFRAME */
    ok &= writeU32LE(e.offset);
    ok &= writeU32LE(e.size);
  }

  /* flush before size() so RIFF size reflects all committed idx1 bytes */
  esp_task_wdt_reset();  /* flush + close may block on slow SD cards */
  _file.flush();

  /* patch RIFF size (must be last — idx1 just added bytes) */
  esp_task_wdt_reset();
  ok &= patchU32LE(4, (uint32_t)(_file.size() - 8));

  _file.close();
  _index.clear();
  _segmentCount++;
  SystemConfig.SaveTlSegmentCount((uint16_t)_segmentCount);
  _recording = false;

  if (!ok) {
    SystemLog.AddEvent(LogLevel_Error, "Timelapse: write error during finalise (SD full?) " + _fileName);
  }
  SystemLog.AddEvent(LogLevel_Info, "Timelapse: finalised " + _fileName + " (" + String(_segFrames) + " frames)");
  return ok;
}

bool TimelapseBuilder::finalise() {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  bool ok = _finalise_unlocked();
  xSemaphoreGive(_mutex);
  return ok;
}

bool TimelapseBuilder::stop() {
  _startPending = false;   /* a stop cancels a start that never opened */
  xSemaphoreTake(_mutex, portMAX_DELAY);
  bool ok = _finalise_unlocked();
  xSemaphoreGive(_mutex);
  SystemConfig.SaveTlWasRecording(false);
  if (ok) flashBlink(3, 100, 80);  /* 3 quick flashes = stopped */
  return ok;
}

void TimelapseBuilder::abort() {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  if (_file) {
    _file.close();
    SD_MMC.remove(_fileName.c_str());
  }
  _index.clear();
  _recording   = false;
  _segFrames   = 0;
  _totalFrames = 0;
  xSemaphoreGive(_mutex);
  SystemConfig.SaveTlWasRecording(false);
  SystemLog.AddEvent(LogLevel_Warning, F("Timelapse: recording aborted"));
}

uint32_t TimelapseBuilder::fileSizeBytes() const {
  /* position(), not size(): for a file still open for writing, FATFS reports the
     size known at open time — 0 for a file we created — and only updates it on
     close. That is why /timelapse/status showed 0 MB throughout a recording and the
     real figure appeared only after finalise(). The write offset is exact and free. */
  if (_file) return (uint32_t)_file.position();
  return 0;
}

String TimelapseBuilder::currentFileName() {
  xSemaphoreTake(_mutex, portMAX_DELAY);
  String n = _fileName;
  xSemaphoreGive(_mutex);
  return n;
}

bool TimelapseBuilder::lockForBulkOp(TickType_t timeout) {
  return xSemaphoreTake(_mutex, timeout) == pdTRUE;
}

void TimelapseBuilder::unlockForBulkOp() {
  xSemaphoreGive(_mutex);
}

void TimelapseBuilder::restoreFromEeprom() {
  if (!SystemConfig.LoadTlWasRecording()) return;

  /* If the previous session ended with a crash or WDT reboot, do not resume —
     it would recreate the same crash on every subsequent boot.
     Only resume after a clean power cycle or software restart. */
  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT ||
      reason == ESP_RST_PANIC    || reason == ESP_RST_WDT      ||
      reason == ESP_RST_BROWNOUT || reason == ESP_RST_UNKNOWN) {
    SystemLog.AddEvent(LogLevel_Warning, F("Timelapse: crash/WDT reboot — not resuming, clearing flag"));
    SystemConfig.SaveTlWasRecording(false);
    return;
  }

  _segmentCount = SystemConfig.LoadTlSegmentCount();
  SystemLog.AddEvent(LogLevel_Info, F("Timelapse: resuming after restart, segment="), String(_segmentCount));
  if (!begin()) {
    SystemLog.AddEvent(LogLevel_Warning, F("Timelapse: resume failed (no SD or camera not ready)"));
    SystemConfig.SaveTlWasRecording(false);
  }
}

/* EOF */
