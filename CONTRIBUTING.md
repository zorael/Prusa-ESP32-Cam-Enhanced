# Contributing

This is a small fork maintained by one person with one printer. That shapes what
is useful to send.

## The most useful thing you can contribute

**Board verification.** Only the AI Thinker ESP32-CAM is built and tested.
`platformio.ini` carries five ESP32-S3 environments inherited from upstream. They
compile, but none has been flashed against this firmware's camera and PSRAM
changes. If you own one of those boards and flash it, open an issue saying what
happened — working or not. That is the single biggest gap in the project.

## Building

```bash
pio run -e ai_thinker              # build
pio run -e ai_thinker -t upload    # build and flash
pio device monitor -b 115200       # serial console
```

Editing the web UI means regenerating the compressed assets afterwards:

```bash
cd webpage && python webpage_gz_generator.py
```

If you touched a Tailwind class, rebuild the stylesheet first (`npm ci && npm run
build` in `webpage/`). CI fails if either generated file is stale.

**The stylesheet is built ahead of time, which has one sharp edge.** Tailwind only
emits classes it can find as literal text in `index.html`. Write a class name by
concatenation and it will compile, ship, and silently do nothing on the device:

```js
el.className = 'bg-' + colour;        // no. Tailwind cannot see this
el.className = ok ? 'bg-success' : 'bg-danger';   // yes, both are literals
```

`webpage/check_classes.py` catches this and runs in CI.

## Before you open a PR

- **Build it.** `pio run -e ai_thinker` must succeed, and CI runs the same thing on
  every push.
- **Watch the flash number.** The build prints it. It sits near 79 %, so a change
  that adds tens of KB needs to say what it bought.
- **Run it on hardware.** This is firmware for a camera that uploads over TLS on a
  microcontroller with a watchdog. Plenty of changes compile and then reboot the
  device every few minutes. If you cannot test on hardware, say so in the PR — that
  is fine, it just changes how the change gets reviewed.
- **Match the surrounding style.** Comments here explain *why*, especially where
  the obvious approach is wrong; there are several places where it is. Keep that.

## Reporting a bug

Firmware bugs are hard to act on without context. Please include:

- the firmware version (System tab, or the boot banner on serial)
- your board, camera sensor and resolution
- serial log around the failure — `pio device monitor -b 115200`
- whether it survives a reboot, and whether it reproduces after a `--virgin` flash

**Scrub your logs before posting.** They can contain your WiFi SSID and, at verbose
level, your Prusa Connect token and PrusaLink API key.

## Relationship to upstream

This fork diverges from [prusa3d/Prusa-Firmware-ESP32-Cam][up] in the upload path,
task model and web UI, and versions independently from 1.0.0. Bugs that reproduce
on stock firmware belong upstream. Bugs in the timelapse, OTA, PrusaLink or upload
reliability work belong here.

[up]: https://github.com/prusa3d/Prusa-Firmware-ESP32-Cam

## Licence

Contributions are accepted under GPL-3.0, the licence this project inherits.
