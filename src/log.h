/**
   @file log.h

   @brief log library

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   @bug: no know bug
*/

#pragma once

#include <Arduino.h>

#include "micro_sd.h"
#include "log_level.h"

class Logs : public MicroSd {
private:
  LogLevel_enum LogLevel;     ///< LogLevel
  String FileName;            ///< log File name
  String FilePath;            ///< log file patch
  uint16_t FileMaxSize;       ///< log file max size
  bool NtpTimeSynced;         ///< status NTP time sync
  String LogMsg;              ///< log message
  File LogFile;               ///< log file object
  bool LogFileOpened;         ///< log file opened status
  SemaphoreHandle_t LogMutex; ///< log mutex

  static const uint8_t  LOG_RING_SIZE  = 30;
  static const uint8_t  LOG_ENTRY_MAX  = 160;
  char     _ring[LOG_RING_SIZE][LOG_ENTRY_MAX];  ///< fixed BSS allocation, no heap
  uint8_t  _ringHead  = 0;
  uint8_t  _ringCount = 0;

public:
  Logs();
  Logs(String, String);
  Logs(LogLevel_enum, String, String);
  Logs(String, String, uint16_t);
  Logs(LogLevel_enum, String, String, uint16_t);
  ~Logs(){};

  void Init();
  void LogOpenFile();
  void LogCloseFile();
  void LogCheckOpenedFile();
  void AddEvent(LogLevel_enum, String, bool = true, bool = true);
  void AddEvent(LogLevel_enum, const __FlashStringHelper*, String, bool = true, bool = true);
  void SetLogLevel(LogLevel_enum);
  void SetFileName(String);
  void SetFilePath(String);
  void SetFileMaxSize(uint16_t);
  void SetNtpTimeSynced(bool);

  String GetFileName();
  String GetFilePath();
  LogLevel_enum GetLogLevel();
  bool GetNtpTimeSynced();
  void CheckMaxLogFileSize();
  void CheckCardSpace();
  bool GetLogFileOpened();

  String GetSystemTime();
  String GetRecentLogs();
};

extern Logs SystemLog;  ///< log object

/* EOF */