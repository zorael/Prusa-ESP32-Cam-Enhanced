# Publish a Freenove ESP32-WROVER-DEV binary

Origin: forum request from **Solididi** on
[Enhancing Prusa ESP32 Cam](https://forum.prusa3d.com/forum/prusa-core-one-general-discussion-announcements-and-releases/enhancing-prusa-esp32-cam/#post-801125)
— "How much work would it be for me to compile a .bin for the WROVER? It is
supported in the original project. (I use Linux.)"

## Plan

- [x] Move `OTA_ASSET_NAME` from `mcu_cfg.h` into each `src/module_<board>.h`
- [x] Guard against a board header omitting it (`#error` in `ota.cpp`)
- [x] Convert `.github/workflows/release.yml` to a board build matrix
- [x] Add `docs/manifest-wrover.json` and a board picker to the web flasher
- [x] Expose `sd_hw` in `json_input`; hide the timelapse UI on boards without SD
- [x] Update README, release notes and CLAUDE.md

## Review

**The bug that nearly shipped.** `pio run -e wrover` did not build WROVER
firmware. `module_templates.h` selects the board header by macro *value*
(`#if (true == BOARD)`), and `mcu_cfg.h` defined all seven board macros
unconditionally — `AI_THINKER_ESP32_CAM true`, the rest `false` — which
overwrote the `-D` flag from the PlatformIO environment. Every environment built
AI Thinker firmware. The images differed in size only because the PlatformIO
board JSON differed, never the camera pins, so nothing about a green build looked
wrong. Caught by grepping the finished binary: the "wrover" image contained
`AI Thinker ESP32-CAM` and `firmware-ai_thinker.app.bin`, and no WROVER string at
all. Tagging without this fix would have published AI Thinker firmware under a
WROVER filename — worse than publishing nothing, since the board picker would
have lent it credibility.

Fixed by making those defaults `#ifndef`-guarded with no fallback board, so the
`!= 1` guard in `module_templates.h` fails a build that names no board. The
wrover image dropped 74 KB on the next build (SD code compiling out) — the first
visible sign it was finally the real thing. All six environments still compile,
and each now produces a genuinely distinct binary carrying its own board name.

**The bug this turned up.** `mcu_cfg.h` defined one global `OTA_ASSET_NAME`,
`firmware-ai_thinker.app.bin`, while `ota.cpp` matched release assets by exact
filename on the stated assumption that each board names its own. With one board
published that was harmless. The moment a second board shipped, every WROVER
pressing "check for update" would have installed AI Thinker firmware — wrong
camera pins. For the S3 environments it would have been an ESP32-S3 app image on
an ESP32: a device that does not boot. The name now lives in the board header
where the rest of the board's identity lives, `ota.cpp` `#error`s if a header
omits it, and CI asserts that each published board's header names exactly the
artifact the matrix produces.

**Board matrix.** `release.yml` was one job doing checks, build, Pages and
release. The Pages site and the GitHub release both carry every board, so those
two steps could not stay inside a per-board matrix leg — two legs racing to
create the same release is a coin flip over which assets survive. Split into
`checks` (board-independent, runs once) → `build` (matrix, `fail-fast: false`) →
`publish` (downloads all artifacts) → `deploy-pages`.

**SD card.** The WROVER board definition sets `ENABLE_SD_CARD false`, so
timelapse and SD logging cannot work there. The endpoints already returned 503,
but the SPA had no way to distinguish "no card inserted" from "no card reader
exists" and would have shown a full timelapse UI whose every button failed.
`json_input` now carries `sd_hw`; the SPA reads it once and hides the timelapse
section, the TL Files tab, the SD bar and the SD pill. It also stops polling
`/timelapse/status` on such boards.

## Verification

- All six environments build. ai_thinker Flash 78.8% (1,550,188 B), wrover
  74.4% (1,462,764 B), plus the four S3 envs — which compile their own headers
  now rather than AI Thinker's.
- Every artifact greps for its own `OTA_ASSET_NAME` and for no other board's —
  the check CI now runs. Passes for all six; the pre-fix wrover image fails it.
- `npm run build` leaves `styles.css` byte-identical; `check_classes.py` passes
  (179/179 classes resolve); `webpage_gz_generator.py` regenerated
  `src/WebPage_gz.h`.
- CI's new OTA-name assertion tested locally: passes for `ai_thinker`/`wrover`
  and for the unpublished `esp32s3_eye`, fails for a nonexistent board.
- Workflow YAML parses; job graph is checks → build → publish → deploy-pages.
- SPA exercised in Chrome against a mock `json_input` on both paths:
  - `sd_hw: false` — timelapse section, SD row, SD pill, TL Files tab and its
    pane all hidden; zero `/timelapse/status` requests over 11 s (2 `json_input`
    polls in the same window).
  - `sd_hw: true` — everything visible, SD bar reads "6064 MB free / 7580 MB",
    `/timelapse/status` still polled. No regression.
  - Race case (TL Files open before the first status arrives) falls back to the
    System tab instead of leaving an empty pane.
- Flasher page exercised in Chrome: switching the picker to WROVER and back
  rewrites the install manifest, all three filenames in the manual instructions,
  and the board-specific notes, in both directions.
- Flasher tested against a Pages site assembled exactly as the `publish` job
  does (real binaries, `__VERSION__` substituted to v1.1.0):
  - esp-web-tools resolves the manifest at click time —
    `n.manifestPath = t.manifest || t.getAttribute("manifest")` in the shipped
    bundle — so swapping the attribute at runtime is honoured. The element
    exposes no `manifest` property, which is why the picker must use
    `setAttribute`, not the property.
  - For each board: manifest 200, correct `chipFamily`, binary 200, valid ESP
    image (0xE9 at both 0x1000 and 0x10000), chip id 0 = ESP32 matching the
    manifest, and the board's own name and OTA asset string inside the image.

## Not done

- **Nobody has run the WROVER image on real hardware.** It is published as
  untested, and labelled that way in the release notes, the flasher page and the
  README.
- **SD on the WROVER was not attempted.** The Freenove board does have a microSD
  slot (SDMMC 1-bit would be CLK 14 / CMD 15 / D0 2), but the board header
  assigns GPIO 14 to the flash LED and GPIO 2 to the status LED, so enabling it
  means reassigning both — unverifiable without the board. Worth revisiting if
  Solididi is willing to test.
