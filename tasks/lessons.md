# Lessons

Patterns worth not repeating. One entry per correction.

## A board header describes one revision, not the hardware

**What happened.** Asked to publish a Freenove WROVER binary, I read
`ENABLE_SD_CARD false` in `src/module_ESP32-WROVER-DEV.h` and reported that the
board has no SD card, so timelapse could not work on it. That went into the
README, the release notes and the flasher page of a published release. The user
pushed back — "i think it has sd card?" — and sent a photo of the board. It is a
v3.0 with a microSD slot on the back. Freenove added the slot in a later
revision; upstream's header, and their doc saying "Missing micro SD card slot",
described the board as it was years ago.

**Why it matters.** The false claim was the most confident sentence in the
release notes, and it was about someone else's hardware — the one thing I could
not check by building. Upstream's header is evidence of what upstream tested,
not of what the board has.

**Rule.** For any claim about physical hardware — pins, peripherals, LEDs —
check the vendor's own example code or datasheet before writing it down.
Freenove's `Sketch_03.1_SDMMC_Test` gives the SD pins with "Please do not modify
it" next to each, and `Sketch_01.1_Blink` gives `LED_BUILTIN 2`; together they
proved both the slot and the GPIO 2 collision in a way no amount of reading the
firmware could. Say "this build does not enable X" when that is what I know, not
"the board does not have X".

## A flag that nothing reads is not a feature

**What happened.** `STATUS_LED_ENABLE` was defined in all seven board headers,
documented as "enable/disable status LED", and read nowhere in the codebase.
`module_ESP32-S3_Wroom_Freenove.h` had set it `false` and been silently ignored
for its whole life. I nearly set it `false` for the WROVER and called the pin
conflict solved.

**Rule.** Before relying on an existing config flag, grep for a *read* of it, not
just its definition. A `#define` in a header proves someone intended the
behaviour, not that it exists.

## Verify the artifact, not the build log

**What happened.** `pio run -e wrover` succeeded for months while producing AI
Thinker firmware, because `mcu_cfg.h` overwrote the environment's `-D`. Every
signal a build normally gives — exit status, differing binary sizes, differing
flash usage — looked correct.

**Rule.** When output is supposed to differ per configuration, prove it from the
output: `grep` the binary for a string only that configuration produces, or
disassemble the function that was supposed to change. `sys_led::toggle()`
compiling to `entry; retw.n` on one board and two `callx8` calls on another is
proof; a green build is not.
