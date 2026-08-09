# Security

## Reporting a vulnerability

Report privately through GitHub's [Report a vulnerability][ghsa] button on the
Security tab. Please do not open a public issue for anything that lets an attacker
reach the device or the credentials on it.

This is a hobby project maintained by one person — expect a reply in days, not
hours, and no bounty.

[ghsa]: https://github.com/gw-tan/Prusa-ESP32-Cam-Enhanced/security/advisories/new

## What this firmware assumes

Worth knowing before you decide where to put the camera. None of the below is a
vulnerability report — it is the design, and it matches upstream.

- **The web UI is plain HTTP.** There is no TLS on port 80; an ESP32 cannot
  terminate it for a self-signed cert anyone would accept anyway. Basic auth is
  available and off by default, and over HTTP it sends the password base64-encoded,
  not encrypted. Treat the device as trusted-LAN-only. Do not port-forward it.
- **Anyone on your network can reach it.** With basic auth disabled, every route —
  the live stream, the config setters, the timelapse files on the SD card — is open
  to anything that can route to the device.
- **Credentials live in NVS in the clear.** WiFi password, Prusa Connect token and
  PrusaLink API key are stored unencrypted; flash encryption is not enabled. Physical
  access to the board, or a serial console, reads them out.
- **Outbound uploads are properly authenticated.** The connection to
  `connect.prusa3d.com` is TLS with a pinned CA certificate (`src/Certificate.h`),
  as are the OTA update check and download.
- **A factory flash wipes NVS.** That is deliberate and documented, and it is also
  how you decommission a board: flash the factory image before giving one away, or
  your WiFi password and Connect token go with it.

## Supported versions

The latest release only. Fixes go on `master` and ship in the next tag.
