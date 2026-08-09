/**
   @file WebPage_Icons.h

   @brief Favicon for the web UI.

   Originally by Miroslav Pivovarsky; the mark itself is not his work and not
   Prusa's. The upstream favicon was Prusa Connect's own logo, which this project
   has no licence to ship — GPL grants copyright permissions, not trademark
   rights. Replaced with a plain camera glyph drawn for this fork.

   The rest of the upstream icon set (logo, github, light, refresh, reboot, wifi
   bars, eye) went with the multi-page UI it was drawn for. The SPA inlines its
   own SVGs, so the favicon is the only icon the firmware still has to serve.
*/

#pragma once

const char favicon_svg[] PROGMEM = R"rawliteral(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48" width="48" height="48">
  <rect width="48" height="48" rx="10" fill="#1e1e1e"/>
  <path d="M17 16.5 19.6 12h8.8l2.6 4.5z" fill="#FA6831"/>
  <rect x="6" y="16" width="36" height="21" rx="3.5" fill="#FA6831"/>
  <circle cx="24" cy="26.5" r="7.5" fill="#1e1e1e"/>
  <circle cx="24" cy="26.5" r="3.4" fill="#FA6831"/>
  <circle cx="36" cy="20.5" r="1.6" fill="#1e1e1e"/>
</svg>
)rawliteral";

/* EOF  */
