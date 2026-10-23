#!/usr/bin/env python3
"""Reads or writes one LED's own colour, the way VIA's key painter does.

The painter sends a colour per key on VIA's own channel zero, value one, and the
keyboard stores it. This drives the same two commands, so the path can be
checked without the application -- and so that what the application will show
can be read back before trusting it:

    python3 tools/via-per-key.py --led 0                  # what is stored
    python3 tools/via-per-key.py --led 0 --colour 170,255 # paint it blue

Hue and saturation are what the application sends. The brightness is the
keyboard's own addition -- the only way to say that one key is off, since zero
saturation is white -- and it belongs to the brush rather than to the stroke, so
`--brightness` sets what the *next* painted key gets:

    python3 tools/via-per-key.py --brightness 0 --led 0 --colour 0,0  # off
    python3 tools/via-per-key.py --brightness 255                     # brush back to full

The LED is the index in the firmware's own map -- the same number the LED probe
in VIA's Diagnostics menu uses, and the one the definition gives each key. A key
with two LEDs takes the colour on both, which the firmware does for itself, so
writing 128 and reading 129 is the way to see that happen.

Needs read and write on the hidraw node, so run it as root or install
udev/99-corsair-k100.rules.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_usb import PRODUCT, VENDOR, device, identity  # noqa: E402
from k100_via import CUSTOM_GET_VALUE, CUSTOM_SET_VALUE, exchange, interface  # noqa: E402

#: VIA's own channel for a keyboard's values, and the value the painter uses on
#: it. Neither is this keyboard's to choose: the application has the number 1
#: built in, which is why the keyboard's own values start at 2. See
#: qmk/keyboards/corsair/k100/per_key_colour.c.
K100_CHANNEL = 0
PER_KEY_COLOUR = 1

#: The brush's brightness, which *is* one of this keyboard's own numbers --
#: `K100_VIA_PAINT_VALUE` in k100.h, where the generated definition reads it
#: from. Written down again here because a test tool that generated its own
#: definition would be testing itself.
PAINT_VALUE = 5

#: A byte the application always sends as one, after the index. Nothing in the
#: firmware reads it; it is here because the reports have to match.
ONE = 1


def colour(text: str) -> tuple[int, int]:
    hue, _, sat = text.partition(",")
    return (int(hue), int(sat))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--vid", default=VENDOR, help="USB vendor id, hex")
    parser.add_argument("--pid", default=PRODUCT, help="USB product id, hex")
    parser.add_argument("--led", type=int, help="which LED, by index")
    parser.add_argument("--colour", type=colour, help="hue,saturation, each 0 to 255")
    parser.add_argument("--brightness", type=int,
                        help="0 to 255, what the next painted key is given; 0 is off")
    parser.add_argument("--all", type=int, metavar="N", nargs="?", const=190,
                        help="read the first N LEDs and report the ones that carry a "
                             "colour, which is what a stored picture looks like from here")
    args = parser.parse_args()

    if args.led is None and args.brightness is None and args.all is None:
        raise SystemExit("--led, --brightness, --all, or a combination")

    entry = device(args.vid, args.pid)
    if entry is None:
        print(f"no {args.vid}:{args.pid} on this machine", file=sys.stderr)
        return 1
    version, product = identity(entry)
    print(f"running  bcdDevice {version}  {product}")

    found = interface(entry)
    if found is None:
        print("no interface answered VIA", file=sys.stderr)
        return 1
    node, protocol = found
    print(f"VIA on {node}, protocol {protocol:#06x}")

    if args.brightness is not None:
        if exchange(node, bytes([CUSTOM_SET_VALUE, K100_CHANNEL, PAINT_VALUE,
                                 args.brightness])) is None:
            print("the brush was not set", file=sys.stderr)
            return 1
        brush = exchange(node, bytes([CUSTOM_GET_VALUE, K100_CHANNEL, PAINT_VALUE]))
        print(f"brush: brightness {brush[3] if brush else '?'}")

    if args.all is not None:
        painted = []
        for led in range(args.all):
            reply = exchange(node, bytes([CUSTOM_GET_VALUE, K100_CHANNEL, PER_KEY_COLOUR,
                                          led, ONE]))
            if reply is None:
                print(f"LED {led}: no answer", file=sys.stderr)
                return 1
            if (reply[5], reply[6]) != (0, 0):
                painted.append((led, reply[5], reply[6]))

        print(f"{len(painted)} of {args.all} LEDs carry a colour")
        for led, hue, sat in painted:
            print(f"  LED {led:<3} hue {hue:<3} saturation {sat}")

    if args.led is None:
        return 0

    if args.colour is not None:
        hue, sat = args.colour
        written = exchange(node, bytes([CUSTOM_SET_VALUE, K100_CHANNEL, PER_KEY_COLOUR,
                                        args.led, ONE, hue, sat]))
        if written is None:
            print("the write was not answered", file=sys.stderr)
            return 1

    reply = exchange(node, bytes([CUSTOM_GET_VALUE, K100_CHANNEL, PER_KEY_COLOUR,
                                  args.led, ONE]))
    if reply is None:
        print("the read was not answered", file=sys.stderr)
        return 1

    print(f"LED {args.led}: hue {reply[5]}, saturation {reply[6]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
