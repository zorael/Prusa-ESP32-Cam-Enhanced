/**
   @file log.cpp

   @brief log library

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   @bug: no know bug
*/
#include "log.h"

Logs SystemLog(LOGS_FILE_PATH, LOGS_FILE_NAME, LOGS_FILE_MAX_SIZE);

/**
   @info Constructor
   @param none
   @return none
*/
Logs::Logs() {
  FileName = F("log.txt");
  FilePath = F("/");
  LogLevel = LogLevel_Verbose;
  FileMaxSize = 1024;
  NtpTimeSynced = false;
  LogMsg = "";
  LogMutex = xSemaphoreCreateMutex();
}

/**
 * @brief Construct a new Logs:: Logs object  with file path and file name
 * 
 * @param String - file path
 * @param String - file name
 */
Logs::Logs(String i_FilePath, String i_FileName) {
  FileName = i_FileName;
  FilePath = i_FilePath;
  LogLevel = LogLevel_Verbose;
  FileMaxSize = 1024;
  NtpTimeSynced = false;
  LogMsg = "";
  LogMutex = xSemaphoreCreateMutex();
}

/**
 * @brief Construct a new Logs:: Logs object with log level, file path and file name
 * 
 * @param LogLevel_enum - log level
 * @param String - file path
 * @param String - file name
 */
Logs::Logs(LogLevel_enum i_LogLevel, String i_FilePath, String i_FileName) {
  FileName = i_FileName;
  FilePath = i_FilePath;
  LogLevel = i_LogLevel;
  FileMaxSize = 1024;
  NtpTimeSynced = false;
  LogMsg = "";
  LogMutex = xSemaphoreCreateMutex();
}

/**
 * @brief Construct a new Logs:: Logs object with file path, file name and file size
 * 
 * @param String - file path
 * @param String - file name
 * @param uint16_t - file size
 */
Logs::Logs(String i_FilePath, String i_FileName, uint16_t i_FileSize) {
  FileName = i_FileName;
  FilePath = i_FilePath;
  LogLevel = LogLevel_Verbose;
  FileMaxSize = i_FileSize;
  NtpTimeSynced = false;
  LogMsg = "";
  LogMutex = xSemaphoreCreateMutex();
}

/**
 * @brief Construct a new Logs:: Logs object with log level, file path, file name and file size
 * 
 * @param LogLevel_enum - log level
 * @param String - file path
 * @param String - file name
 * @param uint16_t - file size
 */
Logs::Logs(LogLevel_enum i_LogLevel, String i_FilePath, String i_FileName, uint16_t i_FileSize) {
  FileName = i_FileName;
  FilePath = i_FilePath;
  LogLevel = i_LogLevel;
  FileMaxSize = i_FileSize;
  NtpTimeSynced = false;
  LogMsg = "";
  LogMutex = xSemaphoreCreateMutex();
}

/**
   @info Init library for logs
   @param LogLevel_enum - log level
   @param String - file path
   @param String - file name
   @return none
*/
void Logs::Init() {
  Serial.println(F("----------------------------------------------------------------"));
  Serial.println(F("Init Logs library"));

  /* init micro SD card */
#if (true == ENABLE_SD_CARD)
  InitSdCard();
  LogFileOpened = OpenFile(&LogFile, FilePath + FileName);

  if (true == GetCardDetectedStatus()) {
    /* check maximum log file size */
    CheckMaxLogFileSize();

    /* added first message to log file after start MCU */
    LogMsg = F("----------------------------------------------------------------\n");
    LogMsg += F("Start MCU!\nSW Version: ");
    LogMsg += String(SW_VERSION);
    LogMsg += F(" ,Build: ");
    LogMsg += String(SW_BUILD);
    LogMsg += "\n";
    LogMsg += F("Verbose mode: ");
    LogMsg += (true == CONSOLE_VERBOSE_DEBUG) ? "true" : "false";
    LogMsg += "\n";
    LogMsg += F("Log level: ");
    LogMsg += String(LogLevel);
    LogMsg += "\n";
    AppendFile(&LogFile, &LogMsg);

  } else {
    Serial.println(F("Micro-SD card not found! Disable logs"));
  }
#else
  Serial.println(F("Micro-SD card not enabled! Disable logs to card"));
  LogFileOpened = false;
#endif
}

/**
 * @brief Open log file
 * 
 */
void Logs::LogOpenFile() {
#if (true == ENABLE_SD_CARD)
  LogFileOpened = OpenFile(&LogFile, FilePath + FileName);
#endif
}

/**
 * @brief Close log file
 * 
 */
void Logs::LogCloseFile() {
#if (true == ENABLE_SD_CARD)
  CloseFile(&LogFile);
#endif
}

/**
 * @brief Function for check opened log file
 * 
 */
void Logs::LogCheckOpenedFile() { 
#if (true == ENABLE_SD_CARD)
  LogFileOpened = CheckOpenFile(&LogFile);
#endif
}

/**
   @info set log level
   @param LogLevel_enum - log level
   @return none
*/
void Logs::SetLogLevel(LogLevel_enum level) {
  LogLevel = level;
}

/**
   @info Add new log event
   @param LogLevel_enum - log level
   @param String - log message
   @param bool - new line
   @param bool - date
   @return none
*/
void Logs::AddEvent(LogLevel_enum level, String msg, bool newLine, bool date) {
  /* hold mutex only for ring-buffer update; release before Serial + SD I/O */
  xSemaphoreTake(LogMutex, portMAX_DELAY);
  if (LogLevel < level) {
#if (true == CONSOLE_VERBOSE_DEBUG)
    Serial.println(msg);
#endif
    xSemaphoreGive(LogMutex);
    return;
  }

  LogMsg = "";
  if (true == date) {
    LogMsg += GetSystemTime();
    LogMsg += " - ";
  }
  LogMsg += msg;
  if (true == newLine) {
    LogMsg += "\n";
  }

  /* push to ring buffer — no heap allocation, trim trailing newline */
  { size_t _l = LogMsg.length(); if (_l > 0 && LogMsg[_l-1] == '\n') _l--; size_t _n = _l < (LOG_ENTRY_MAX-1) ? _l : (LOG_ENTRY_MAX-1); memcpy(_ring[_ringHead], LogMsg.c_str(), _n); _ring[_ringHead][_n] = '\0'; _ringHead = (_ringHead + 1) % LOG_RING_SIZE; if (_ringCount < LOG_RING_SIZE) _ringCount++; }

  String localMsg = LogMsg;  /* copy while mutex held, before release */
  xSemaphoreGive(LogMutex);  /* release before any I/O */

  Serial.print(localMsg);
#if (true == ENABLE_SD_CARD)
  if (true == LogFileOpened) {
    LogFileOpened = AppendFile(&LogFile, &localMsg);
    if (false == LogFileOpened) {
      LogCloseFile();
      if (true == GetCardDetectedStatus()) {
        LogOpenFile();
        if (true == LogFileOpened) {
          LogFileOpened = AppendFile(&LogFile, &localMsg);
        }
      }
    }
  }
#endif
}

/**
   @info Add new log event
   @param LogLevel_enum - log level
   @param const __FlashStringHelper - log message
   @param String - parameters
   @param bool - new line
   @param bool - date
   @return none
*/
void Logs::AddEvent(LogLevel_enum level, const __FlashStringHelper *msg, String parameters, bool newLine, bool date) {
  /* hold mutex only for ring-buffer update; release before Serial + SD I/O */
  xSemaphoreTake(LogMutex, portMAX_DELAY);
  if (LogLevel < level) {
#if (true == CONSOLE_VERBOSE_DEBUG)
    Serial.println(msg);
#endif
    xSemaphoreGive(LogMutex);
    return;
  }

  LogMsg = "";
  if (true == date) {
    LogMsg += GetSystemTime();
    LogMsg += " - ";
  }
  LogMsg += msg;
  LogMsg += parameters;
  if (true == newLine) {
    LogMsg += "\n";
  }

  /* push to ring buffer — no heap allocation */
  { size_t _l = LogMsg.length(); if (_l > 0 && LogMsg[_l-1] == '\n') _l--; size_t _n = _l < (LOG_ENTRY_MAX-1) ? _l : (LOG_ENTRY_MAX-1); memcpy(_ring[_ringHead], LogMsg.c_str(), _n); _ring[_ringHead][_n] = '\0'; _ringHead = (_ringHead + 1) % LOG_RING_SIZE; if (_ringCount < LOG_RING_SIZE) _ringCount++; }

  String localMsg = LogMsg;  /* copy before release */
  xSemaphoreGive(LogMutex);  /* release before any I/O */

  Serial.print(localMsg);
#if (true == ENABLE_SD_CARD)
  if (true == LogFileOpened) {
    LogFileOpened = AppendFile(&LogFile, &localMsg);
    if (false == LogFileOpened) {
      LogCloseFile();
      if (true == GetCardDetectedStatus()) {
        LogOpenFile();
        if (true == LogFileOpened) {
          AppendFile(&LogFile, &localMsg);
        }
      }
    }
  }
#endif
}

/**
   @info Set file name
   @param String - file name
   @return none
*/
void Logs::SetFileName(String i_data) {
  FileName = i_data;
}

/**
   @info Set file path
   @param String - file path
   @return none
*/
void Logs::SetFilePath(String i_data) {
  FilePath = i_data;
}

/**
   @info Set file max size
   @param uint16_t - file max size
   @return none
*/
void Logs::SetFileMaxSize(uint16_t i_data) {
  FileMaxSize = i_data;
}

/**
   @info Set NTP time synced
   @param bool - NTP time synced
   @return none
*/
void Logs::SetNtpTimeSynced(bool i_data) {
  NtpTimeSynced = i_data;
  AddEvent(LogLevel_Info, F("System time: "), GetSystemTime());
}

/**
   @info Get file name
   @param none
   @return String - file name
*/
String Logs::GetFileName() {
  return FileName;
}

/**
   @info Get file path
   @param none
   @return String - file path
*/
String Logs::GetFilePath() {
  return FilePath;
}

/**
   @info Get log level
   @param none
   @return LogLevel_enum - log level
*/
LogLevel_enum Logs::GetLogLevel() {
  return LogLevel;
}

/**
   @info Get NTP time synced
   @param none
   @return bool - NTP time synced
*/
bool Logs::GetNtpTimeSynced() {
  return NtpTimeSynced;
}

/**
   @info Check maximum log file size
   @param none
   @return none
*/
void Logs::CheckMaxLogFileSize() {
#if (true == ENABLE_SD_CARD)
  uint32_t FileSize = GetFileSize(SD_MMC, FilePath + FileName);
  AddEvent(LogLevel_Verbose, F("Log file size: "), String(FileSize) + "/" + String(LOGS_FILE_MAX_SIZE) + " B");

  if (FileSize >= LOGS_FILE_MAX_SIZE) {
    uint16_t file_count = FileCount(SD_MMC, FilePath, FileName);
    AddEvent(LogLevel_Info, F("Maximum log file size. File count: "), String(file_count));
    LogCloseFile();
    RenameFile(SD_MMC, FilePath + FileName, FilePath + FileName + String(file_count));
    LogOpenFile();
  }
#endif
}

void Logs::CheckCardSpace() {
#if (true == ENABLE_SD_CARD)
  CheckCardUsedStatus();
  AddEvent(LogLevel_Verbose, "Card size: " + String(GetCardSizeMB()) + " MB, Used: " + String(GetCardUsedMB()) + " MB, Free: " + String(GetCardFreeMB()) + " MB");
#endif
}

/**
   @info Get log file opened
   @param none
   @return bool - log file opened
*/
bool Logs::GetLogFileOpened() {
  return LogFileOpened;
}

/**
   @info Get system time
   @param none
   @return String - time
*/
String Logs::GetSystemTime() {
  String ret = "0000-00-00_00-00-00";
  if (true == NtpTimeSynced) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
#if (true == CONSOLE_VERBOSE_DEBUG)
      Serial.println(F("Failed to obtain time"));
#endif
      return ret;
    }

    char timeString[20];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d_%H-%M-%S", &timeinfo);
#if (true == CONSOLE_VERBOSE_DEBUG)
    Serial.println(timeString);
#endif
    ret = String(timeString);
  }
  return ret;
}

String Logs::GetRecentLogs() {
  /* pre-reserve outside the mutex; repeated += under the lock risks heap failure
     which silently truncates the response */
  String out;
  out.reserve(5120);
  xSemaphoreTake(LogMutex, portMAX_DELAY);
  out = "[";
  uint8_t start = (_ringHead + LOG_RING_SIZE - _ringCount) % LOG_RING_SIZE;
  for (uint8_t i = 0; i < _ringCount; i++) {
    if (i > 0) out += ",";
    out += "\"";
    for (const char* p = _ring[(start + i) % LOG_RING_SIZE]; *p; p++) {
      if (*p == '"')       out += "\\\"";
      else if (*p == '\\') out += "\\\\";
      else                 out += *p;
    }
    out += "\"";
  }
  out += "]";
  xSemaphoreGive(LogMutex);
  return out;
}

/* EOF */