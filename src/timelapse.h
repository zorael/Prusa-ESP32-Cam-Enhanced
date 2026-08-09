/**
   @file timelapse.h
   @brief MJPEG-in-AVI timelapse video builder for ESP32-CAM firmware.

   Frames are appended as raw JPEG chunks inside an AVI RIFF container.
   No transcoding is performed — the camera's native JPEG output is used directly.
   On finalise() the header size fields are patched via seek() and an idx1 index
   chunk is appended, producing a fully playable AVI file.

   Segmentation: after TIMELAPS_AVI_SEGMENT_FRAMES frames the current segment is
   automatically finalised and a new one opened, giving power-loss protection.
*/

#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <vector>

#include "mcu_cfg.h"

class TimelapseBuilder {
public:
  TimelapseBuilder();

  /* force=true bypasses the readiness gate. Only servicePendingStart() uses it, and
     only after the timeout — without it the timeout cannot work, because begin()
     would just re-evaluate the same unmet conditions and defer again. */
  bool  begin(bool force = false);

  /* A start requested before the device is ready is DEFERRED, not refused — losing a
     recording would be worse than a late one. servicePendingStart() must be called
     regularly (the capture task does) to actually open the file once ready. */
  bool  isStartPending() const { return _startPending; }
  void  servicePendingStart();
  static bool startConditionsMet(String *whyNot = nullptr);
  bool  appendFrame(const uint8_t *jpegBuf, size_t jpegLen);
  bool  finalise();
  bool  stop();    ///< user-triggered stop: flashes 3× then calls finalise()
  void  abort();
  void  restoreFromEeprom();  ///< call once after camera init; resumes recording if it was active at shutdown

  /* Job name for the output filename. Pushed in by the PrusaLink poller at print
     start rather than read from it, because prusa_link.h already includes this
     header — reaching back the other way would be a circular include. */
  void   setJobName(const String &n) { _jobName = n; }
  String getJobName() const { return _jobName; }

  bool     isRecording()    const { return _recording; }
  uint32_t frameCount()     const { return _totalFrames; }
  uint32_t fileSizeBytes()  const;
  String   currentFileName();  ///< thread-safe: takes _mutex internally

  void     setDownloadActive(bool active, size_t fileSizeBytes = 0);
  void     noteDownloadProgress();   ///< call on each chunk so the idle timeout tracks liveness
  bool     isDownloadActive()  const;
  File&    getDownloadFile()         { return _downloadFile; }

  void     setMaxFrames(uint32_t n)  { _maxSegFrames = n; }
  uint32_t maxFrames()         const { return _maxSegFrames; }

  /* C3: hold _mutex for the duration of a bulk destructive operation (delete_all).
     Returns false if the lock cannot be taken within the timeout. */
  bool     lockForBulkOp(TickType_t timeout = pdMS_TO_TICKS(500));
  void     unlockForBulkOp();

private:
  File     _file;
  volatile bool _startPending = false;  ///< begin() was asked for before the device was ready
  uint32_t _startPendingSince = 0;      ///< millis() when it was deferred, for the timeout
  String   _jobName;                ///< gcode file name for this recording, "" if unknown
  volatile bool _recording = false;  ///< written by capture task, read from multiple tasks
  uint32_t _segFrames    = 0;       ///< frames in current segment
  uint32_t _totalFrames  = 0;       ///< frames across all segments since begin()
  uint32_t _segmentCount = 0;       ///< segment index, drives filename suffix
  uint16_t _width        = 0;
  uint16_t _height       = 0;

  uint32_t _maxSegFrames   = TIMELAPS_AVI_SEGMENT_FRAMES; ///< 0 = unlimited; set from EEPROM in begin()
  uint32_t _moviSizeOffset = 0;     ///< file byte position of movi LIST size field
  uint32_t _moviDataStart  = 0;     ///< byte address immediately after 'movi' fourcc

  String _fileName;                 ///< path of the currently open file

  File     _downloadFile;           ///< file held open during an active download
  volatile bool _downloadActive   = false;
  /* volatile: written by the AsyncTCP task (Core 0) on every chunk, read by the
     capture task (Core 1). Without it the reader can cache a stale value and
     conclude the download is dead while it is actively transferring. */
  volatile uint32_t _downloadLastActivity = 0; ///< millis() of the last chunk read; liveness, not a predicted duration

  SemaphoreHandle_t _mutex = nullptr;

  struct FrameEntry {
    uint32_t offset;  ///< byte offset from _moviDataStart to the '00dc' tag
    uint32_t size;    ///< JPEG payload length (excluding chunk header)
  };
  std::vector<FrameEntry> _index;

  bool     _finalise_unlocked();    ///< finalise body, caller must hold _mutex
  bool     writeU32LE(uint32_t v);
  bool     patchU32LE(uint32_t fileOffset, uint32_t val);
  void     writeAviHeader();
  String   buildFileName() const;
  bool     openSegment();
};

extern TimelapseBuilder SystemTimelapse;

/* EOF */
