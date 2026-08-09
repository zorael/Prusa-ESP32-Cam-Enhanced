/**
   @file WebPage.h

   @brief The small legal pages, plus the status messages the web handlers return.

   @author Miroslav Pivovarsky
   Contact: miroslav.pivovarsky@gmail.com

   index.html lives in WebPage_gz.h, gzip-compressed and served with
   Content-Encoding: gzip. Regenerate it with:

       cd webpage && python webpage_gz_generator.py

   The pages below stay uncompressed — each is a few hundred bytes, so a gzip
   header would cost more than it saves. They carry their own styling because
   the shared stylesheet was removed with the upstream multi-page UI.
*/

#pragma once

#define MSG_REBOOT_MCU        "Reboot process started, wait several seconds for mcu to boot up. You can close this window now"
#define MSG_SAVE_OK_REBOOT    "Save OK. Please reboot MCU"            ///< WEB app msg save OK
#define MSG_SAVE_OK_WIFI      "Save OK. Connecting to Wi-Fi. Please wait several second"
#define MSG_SAVE_OK           "Save cfg OK"                           ///< WEB app msg save OK
#define MSG_SAVE_NOTOK        "Save cfg NOT OK!"                      ///< WEB app msg save NOT OK
#define MSG_SCANNING          "Scanning Wi-Fi networks. Wait 8s..."   ///< WEB app msg Scanning wifi
#define MSG_UPDATE_START      "Start updating"

/* Shared by the four pages below. Kept as one string so the compiler pools it
   instead of emitting four near-identical copies into .rodata. */
#define LEGAL_PAGE_STYLE \
  "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" \
  "<style>body{margin:0;padding:2rem 1.25rem;background:#1e1e1e;color:#e5e5e5;" \
  "font:16px/1.6 -apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}" \
  "main{max-width:34rem;margin:0 auto}a{color:#FA6831}</style>"

const char license_html[] PROGMEM = LEGAL_PAGE_STYLE R"rawliteral(
<main>
<p>This firmware is free software under the
<a href="https://www.gnu.org/licenses/gpl-3.0.html" target="_blank" rel="noopener">GPL-3.0 licence</a>.</p>
<p>It is a community fork of
<a href="https://github.com/prusa3d/Prusa-Firmware-ESP32-Cam" target="_blank" rel="noopener">prusa3d/Prusa-Firmware-ESP32-Cam</a>,
originally by Miroslav Pivovarsky for Prusa Research. Exif generation is by
David Imhoff under the BSD 3-clause licence. Source, including these changes:
<a href="https://github.com/gw-tan/Prusa-ESP32-Cam-Enhanced" target="_blank" rel="noopener">gw-tan/Prusa-ESP32-Cam-Enhanced</a>.</p>
<p><b>Not affiliated with, endorsed by, or supported by Prusa Research.</b>
&ldquo;Prusa&rdquo;, &ldquo;Prusa Connect&rdquo; and &ldquo;PrusaLink&rdquo; are
their trademarks, used here only to describe what this firmware talks to.</p>
</main>
)rawliteral";

const char privacypolicy_html[] PROGMEM = LEGAL_PAGE_STYLE R"rawliteral(
<main>
<p>This firmware runs on your own hardware. Nobody operates a service on your
behalf, and the authors of this fork receive no data from it, ever.</p>
<p>It stores on the device: your Wi-Fi credentials, your Prusa Connect token, and
your PrusaLink address and API key. These are held unencrypted and are readable
by anyone with physical or serial access to the board. Flash the factory image
before you give the board away.</p>
<p>It makes network connections to exactly three places:</p>
<ul>
<li><b>connect.prusa3d.com</b> &mdash; photos, but only once you enter a Prusa
Connect token. What happens to them afterwards is governed by
<a href="https://www.prusa3d.com/en/page/privacy-policy_231258/" target="_blank" rel="noopener">Prusa Research&rsquo;s privacy policy</a>,
not by this page.</li>
<li><b>api.github.com</b> &mdash; only when you press <i>Check for update</i>.
There is no background polling.</li>
<li><b>Your printer</b>, on your own network, if you enable PrusaLink polling.</li>
</ul>
<p>Timelapse videos stay on the SD card until you download or delete them. The web
interface sets no cookies and stores nothing in your browser.</p>
<p>The interface itself is plain HTTP with authentication off by default. Keep the
device on a trusted network and do not expose it to the internet.</p>
</main>
)rawliteral";

/* EOF  */
