#!/usr/bin/env python3
"""Check that every class index.html uses actually exists in styles.css.

Worth having because the CDN build this replaced could not fail this way. The CDN
script watched the live DOM and generated a class the moment it appeared, so a
class assembled at runtime still worked. Building ahead of time means Tailwind
only emits what it can find as literal text in index.html — write
`'bg-' + colour` and the class silently resolves to nothing, on a device you have
to reflash to debug.

Run from the webpage/ directory, after `npm run build`:

    python check_classes.py

CI runs this on every push.
"""
import re
import sys

html = open("index.html", encoding="utf-8").read()
css = open("styles.css", encoding="utf-8").read()

cands = set()
for m in re.finditer(r'class="([^"]*)"', html):
    cands.update(m.group(1).split())
for m in re.finditer(r"className\s*=\s*'([^']*)'", html):
    cands.update(m.group(1).split())
for m in re.finditer(r"classList\.\w+\(([^)]*)\)", html):
    for lit in re.findall(r"'([^']*)'", m.group(1)):
        cands.update(lit.split())
# the state -> class lookup table in applyPlStatus()
for m in re.finditer(r"^\s*[A-Z_]+:\s*'([a-z0-9/: -]+)',\s*$", html, re.M):
    cands.update(m.group(1).split())

SPECIAL = set(":/.[]%!#$&*+,;<=>?@^`{|}~")


def selector(cls):
    """Tailwind backslash-escapes CSS-special characters in the emitted selector."""
    return "." + "".join("\\" + ch if ch in SPECIAL else ch for ch in cls)


# Not Tailwind utilities: component classes from tailwind.src.css, JS state hooks,
# and a <select> option value that the class regex picks up by accident.
SKIP = {"active", "sl", "toggle", "tab-btn", "tab-pane", "blink", "toast", "printer"}

missing = sorted(c for c in cands if c and c not in SKIP and selector(c) not in css)
checked = len(cands) - len(SKIP & cands)

print(f"classes referenced by index.html : {len(cands)}")
print(f"checked against styles.css       : {checked}")
print(f"skipped (component/state/value)  : {sorted(SKIP & cands)}")

if missing:
    print(f"\n!! {len(missing)} MISSING - these would silently do nothing at runtime:")
    for c in missing:
        print("   ", c)
    sys.exit(1)

print(f"\nOK: all {checked} utility classes resolve in styles.css")
