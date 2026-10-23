#!/usr/bin/env python3
"""Reads or writes one LED's own colour, the way VIA's key painter does.

The painter sends a colour per key on VIA's own channel zero, value one, and the
keyboard stores it. This drives the same two commands, so the path can be
checked without the application -- and so that what the application will show
can be read back before trusting it:

    python3 tools/via-per-key.py --led 0                  # what is stored
    python3 tools/via-per-key.py --led 0 --colour 170,255 # paint it blue

Hue and saturation only. Brightness is not part of a per-key colour: it stays
the one the wheel sets, for the whole keyboard at once.

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
    parser.add_argument("--led", type=int, required=True, help="which LED, by index")
    parser.add_argument("--colour", type=colour, help="hue,saturation, each 0 to 255")
    args = parser.parse_args()

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
