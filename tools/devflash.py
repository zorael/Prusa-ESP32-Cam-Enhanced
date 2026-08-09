#!/usr/bin/env python3
"""
devflash.py — flash-test helper for the ESP32-CAM.

Testing the from-scratch install means erasing the chip, which throws away your
WiFi credentials and Prusa Connect token every cycle. This backs them up first and
puts them back afterwards, so a full wipe costs one command instead of a trip
through the setup wizard.

Everything the firmware persists lives in ONE place: the 20 KB `nvs` partition at
0x9000. Arduino's EEPROM library is a shim over an NVS blob, and the WiFi driver
caches credentials in the same partition, so a single dump captures the lot.

    python tools/devflash.py backup            # save NVS to tools/nvs-backup.bin
    python tools/devflash.py fresh             # ERASE ALL + flash + restore NVS
    python tools/devflash.py fresh --virgin    # ERASE ALL + flash, NO restore
    python tools/devflash.py upgrade           # flash without erasing (the normal path)
    python tools/devflash.py restore           # put NVS back
    python tools/devflash.py verify            # read NVS and diff against the backup

`fresh --virgin` is the one that actually tests what a new user experiences: blank
chip, factory defaults, setup AP. Use `fresh` (restores afterwards) when you just
want the device working again quickly.

The serial port is auto-detected. Pass `--port` (or set `ESPTOOL_PORT`) if you
have more than one serial device attached.

`upgrade` also clears otadata, so the board runs the image you just flashed
even if it was last booted from the other OTA slot.
"""

import argparse
import hashlib
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

NVS_OFFSET = 0x9000
NVS_SIZE = 0x5000          # 20 KB, per min_spiffs.csv
OTADATA_OFFSET = 0xe000    # selects which app partition the bootloader runs
OTADATA_SIZE = 0x2000
APP0_OFFSET = 0x10000
BACKUP = os.path.join(HERE, "nvs-backup.bin")

DEFAULT_ENV = "ai_thinker"


def pio_python():
    """Find a python that has esptool — PlatformIO's venv always does.

    PLATFORMIO_CORE_DIR wins if set, otherwise try the default location. Falling
    back to the interpreter running this script is right whenever esptool is on
    the normal path (`pip install esptool`, or running under pio's own venv).
    """
    roots = []
    if os.environ.get("PLATFORMIO_CORE_DIR"):
        roots.append(os.environ["PLATFORMIO_CORE_DIR"])
    roots.append(os.path.join(os.path.expanduser("~"), ".platformio"))

    for root in roots:
        for rel in (("penv", "Scripts", "python.exe"), ("penv", "bin", "python")):
            cand = os.path.join(root, *rel)
            if os.path.isfile(cand):
                return cand
    return sys.executable


def factory_bin(env):
    return os.path.join(ROOT, ".pio", "build", env, "firmware.factory.bin")


def esptool(args, port, baud):
    cmd = [pio_python(), "-m", "esptool", "--chip", "esp32"]
    # No --port means esptool auto-detects, which is the right default when the
    # board is the only serial device attached.
    if port:
        cmd += ["--port", port]
    cmd += ["--baud", str(baud)] + args
    print("  $ " + " ".join(cmd[3:]))
    r = subprocess.run(cmd, cwd=ROOT)
    if r.returncode != 0:
        sys.exit(f"  esptool failed ({r.returncode})")


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()[:16]


def do_backup(a):
    esptool(["read_flash", hex(NVS_OFFSET), hex(NVS_SIZE), BACKUP], a.port, a.baud)
    size = os.path.getsize(BACKUP)
    blank = open(BACKUP, "rb").read().count(b"\xff")
    print(f"  saved {BACKUP} ({size} B, sha {sha(BACKUP)})")
    if blank == size:
        print("  WARNING: backup is entirely 0xFF — the device had no stored config.")
        print("           Restoring this would not bring settings back.")
    else:
        print(f"  looks populated ({size - blank} non-blank bytes)")


def do_restore(a):
    if not os.path.isfile(BACKUP):
        sys.exit(f"  no backup at {BACKUP} — run 'backup' first")
    esptool(["write_flash", hex(NVS_OFFSET), BACKUP], a.port, a.baud)
    print(f"  restored NVS from {BACKUP} (sha {sha(BACKUP)})")


def do_verify(a):
    if not os.path.isfile(BACKUP):
        sys.exit("  no backup to compare against")
    tmp = BACKUP + ".readback"
    esptool(["read_flash", hex(NVS_OFFSET), hex(NVS_SIZE), tmp], a.port, a.baud)
    same = open(tmp, "rb").read() == open(BACKUP, "rb").read()
    print(f"  backup   sha {sha(BACKUP)}")
    print(f"  on-device sha {sha(tmp)}")
    print("  MATCH" if same else "  DIFFERS (expected if the device has since saved settings)")
    os.remove(tmp)


def do_flash(a, erase):
    if erase:
        # Full install: the factory image carries bootloader, partition table and
        # app, so it also (re)establishes the partition layout.
        fw = factory_bin(a.env)
        if not os.path.isfile(fw):
            sys.exit(f"  no firmware at {fw}\n  build first:  pio run -e {a.env}")
        print(f"  image: {fw} ({os.path.getsize(fw)} B)")

        if not a.virgin:
            print("\n[1/3] backing up NVS before erase")
            do_backup(a)
        print("\n[erase] wiping entire flash")
        esptool(["erase_flash"], a.port, a.baud)
        time.sleep(1)

        print("\n[flash] writing factory image at 0x0")
        esptool(["write_flash", "0x0", fw], a.port, a.baud)

        if not a.virgin:
            print("\n[restore] putting NVS back")
            time.sleep(1)
            do_restore(a)
            print("\n  Device should rejoin WiFi with your existing credentials.")
        else:
            print("\n  VIRGIN INSTALL — no config restored.")
            print("  Expect: setup AP 'ESP32_camera_*', config at http://192.168.0.1")
            print("  This is what a first-time user sees. Run 'restore' when done.")
        return

    # Upgrade: app partition ONLY.
    #
    # It is tempting to reuse the factory image here, and it is wrong. That image is
    # CONTIGUOUS from 0x0, so the gaps between partitions are 0xFF padding — and
    # esptool writes padding exactly like real data. NVS lives at 0x9000..0xE000,
    # inside that span, so flashing the factory image at 0x0 silently erases the
    # WiFi credentials and the Prusa Connect token. Verified the hard way: the
    # device came back with an empty SSID and "SSID: not found".
    #
    # Writing just the app at 0x10000 leaves every other partition untouched, which
    # is what an upgrade should do. It does NOT update the partition table — fine,
    # because the table only changes if partitions.csv changes, and that needs a
    # full reinstall anyway.
    app = os.path.join(ROOT, ".pio", "build", a.env, "firmware.bin")
    if not os.path.isfile(app):
        sys.exit(f"  no app image at {app}\n  build first:  pio run -e {a.env}")
    print(f"  image: {app} ({os.path.getsize(app)} B)")
    print("\n[flash] writing app only at 0x10000 (NVS untouched)")
    esptool(["write_flash", hex(APP0_OFFSET), app], a.port, a.baud)

    # Land on the image just written, whichever slot was live before.
    #
    # min_spiffs.csv has two app partitions - app0 at 0x10000, app1 at 0x1F0000 -
    # and otadata says which one boots. A device that has taken an OTA update is
    # running from app1, so writing app0 lands in the INACTIVE slot: esptool
    # reports "Hash of data verified" and the device reboots still running the old
    # firmware. The failure is silent and genuinely confusing, because the flash
    # did succeed - it just wasn't the half being executed.
    #
    # Erasing otadata makes the bootloader fall back to the first app partition,
    # which is the one written above. NVS at 0x9000..0xE000 sits below this and is
    # untouched. The cost is the OTA rollback slot, which a dev flash does not need.
    print("\n[otadata] erasing so the bootloader runs the image just written")
    esptool(["erase_region", hex(OTADATA_OFFSET), hex(OTADATA_SIZE)], a.port, a.baud)
    print("\n  Upgrade complete. Settings preserved — NVS was never written.")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("action", choices=["backup", "restore", "verify", "fresh", "upgrade"])
    p.add_argument("--port", default=os.environ.get("ESPTOOL_PORT"),
                   help="serial port (default: $ESPTOOL_PORT, else auto-detect)")
    p.add_argument("--baud", type=int, default=460800)
    p.add_argument("--env", default=DEFAULT_ENV)
    p.add_argument("--virgin", action="store_true",
                   help="with 'fresh': do NOT restore config afterwards")
    a = p.parse_args()

    print(f"port={a.port or 'auto'} baud={a.baud} env={a.env}\n")
    {
        "backup": do_backup,
        "restore": do_restore,
        "verify": do_verify,
        "fresh": lambda x: do_flash(x, erase=True),
        "upgrade": lambda x: do_flash(x, erase=False),
    }[a.action](a)


if __name__ == "__main__":
    main()
