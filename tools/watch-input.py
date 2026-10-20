#!/usr/bin/env python3
"""Reports key presses arriving from a keyboard, as the kernel sees them.

The last question about a keyboard firmware is the one no register can answer:
does a key reach the host. This reads the evdev nodes the kernel bound to the
device and prints each press and release, so a single switch pressed by hand is
enough to prove the whole path -- matrix scan, HID report, USB transfer, driver.

Finds the keyboard's event devices itself from a USB vendor and product id, so
there is nothing to look up and nothing to get wrong when the numbers move
between re-enumerations.

    tools/watch-input.py --seconds 30
    tools/watch-input.py --quiet --seconds 180    # sweep every key
"""

from __future__ import annotations

import argparse
import select
import struct
import sys
import time
from pathlib import Path

# struct input_event on a 64-bit kernel: two longs of timestamp, then the type,
# code and value.
EVENT = struct.Struct("qqHHi")

EV_KEY = 0x01
ACTIONS = {0: "release", 1: "press", 2: "repeat"}


def event_devices(vid: str, pid: str) -> list[Path]:
    """The evdev nodes belonging to one USB device, newest first."""
    found = []
    for event in sorted(Path("/sys/class/input").glob("event*")):
        # Walk up to the USB device: an input node sits under the HID device,
        # which sits under the interface, which sits under the device itself.
        node = event.resolve()
        for parent in node.parents:
            vendor, product = parent / "idVendor", parent / "idProduct"
            if not vendor.exists():
                continue
            if (vendor.read_text().strip().lower() == vid.lower()
                    and product.read_text().strip().lower() == pid.lower()):
                found.append(Path("/dev/input") / event.name)
            break
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vid", default="1b1c", help="USB vendor id, hex")
    parser.add_argument("--pid", default="1b7c", help="USB product id, hex")
    parser.add_argument("--seconds", type=float, default=30.0,
                        help="how long to listen")
    parser.add_argument("--quiet", action="store_true",
                        help="count distinct keys instead of printing every "
                             "event, for sweeping a whole keyboard")
    args = parser.parse_args()

    paths = event_devices(args.vid, args.pid)
    if not paths:
        print(f"no event device for {args.vid}:{args.pid} -- is it on the bus, "
              f"and did the kernel bind it as an input device?", file=sys.stderr)
        return 1

    streams = {}
    for path in paths:
        try:
            streams[open(path, "rb", buffering=0)] = path.name
        except OSError as error:
            print(f"{path}: {error}", file=sys.stderr)

    if not streams:
        print("found the device but cannot read its event nodes; they are "
              "usually root-only", file=sys.stderr)
        return 1

    print(f"listening on {' '.join(streams.values())} for {args.seconds:.0f}s")
    print("press a key now")

    deadline = time.monotonic() + args.seconds
    presses = 0
    distinct = {}

    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        ready, _, _ = select.select(list(streams), [], [], remaining)
        for stream in ready:
            data = stream.read(EVENT.size)
            if not data or len(data) < EVENT.size:
                continue
            _, _, etype, code, value = EVENT.unpack(data)
            if etype != EV_KEY:
                continue
            presses += 1
            if value == 1:
                distinct[code] = distinct.get(code, 0) + 1
            if not args.quiet:
                action = ACTIONS.get(value, str(value))
                print(f"  {streams[stream]}  keycode {code}  {action}")
            elif value == 1:
                # Enough to watch progress across a hundred keys without a
                # hundred lines of it.
                print(f"\r  {len(distinct)} distinct keys so far", end="",
                      flush=True)

    if args.quiet:
        print()

    if not presses:
        print("\nNo key events. Either no key was pressed, or they are not "
              "arriving.")
        return 1

    print(f"\n{presses} key event(s), {len(distinct)} distinct keys.")
    print("  " + " ".join(str(code) for code in sorted(distinct)))

    # A key that reports twice as often as its neighbours is a key being read
    # twice, which is what a settle time still too short looks like once every
    # key works at all.
    if len(distinct) > 1:
        counts = sorted(distinct.values())
        median = counts[len(counts) // 2]
        noisy = [c for c, n in sorted(distinct.items()) if n > 3 * median]
        if noisy:
            print("\n  pressed far more often than the rest, which is worth a "
                  "look: " + " ".join(str(c) for c in noisy))

    print("\nThe firmware delivers keystrokes.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
