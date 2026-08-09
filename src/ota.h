/**
   @file ota.h

   @brief On-demand firmware update from GitHub releases.

   Deliberately NOT a poller. The previous implementation checked every 30 s, which
   bought nothing — the install was user-triggered anyway, so the poll only kept a
   version string fresh in the UI — while burning a TLS handshake each time. At
   GitHub's 60 requests/hour unauthenticated limit that is 120/hour, so half were
   being rate-limited, and the handshakes were a leading cause of the WDT panics that
   got OTA removed in the first place.

   Here the user presses "Check for update" and nothing happens otherwise.

   Threading: web handlers only ever set a request flag. The HTTPS work runs on the
   housekeeping task, because TLS and JSON parsing must not run on the AsyncTCP task
   (it would stall every other request, and the stack there is not sized for it).
*/

#pragma once

#include <Arduino.h>
#include "cfg.h"
#include "log.h"

enum OtaState_e : uint8_t {
  OtaIdle = 0,        ///< nothing has been attempted this session
  OtaChecking,        ///< check queued or in progress
  OtaUpToDate,        ///< checked; running the newest release
  OtaAvailable,       ///< checked; a newer release exists
  OtaInstalling,      ///< downloading and writing to the inactive app partition
  OtaSuccess,         ///< written and verified; reboot to run it
  OtaFailed,          ///< see GetMessage()
};

class OtaUpdate {
public:
  OtaUpdate(Logs *log) : _log(log) {}

  /* Called from web handlers — cheap, just raises a flag. */
  void RequestCheck();
  bool RequestInstall();

  /* Called from the housekeeping task; performs the actual network work. */
  void Service();

  OtaState_e GetState()      const { return _state; }
  String     GetMessage()    const { return _message; }
  String     GetLatest()     const { return _latest; }
  uint8_t    GetProgress()   const { return _progress; }
  bool       Busy()          const { return _state == OtaChecking || _state == OtaInstalling; }

  static bool IsNewer(const String &candidate, const String &current);

private:
  void DoCheck();
  void DoInstall();

  Logs *_log;
  volatile bool _checkRequested   = false;
  volatile bool _installRequested = false;
  volatile OtaState_e _state = OtaIdle;
  volatile uint8_t _progress = 0;      ///< install progress, percent
  String _message;
  String _latest;                       ///< tag_name of the newest release
  String _assetUrl;                     ///< download URL for this board's binary
};

extern OtaUpdate SystemOta;

/* EOF */
