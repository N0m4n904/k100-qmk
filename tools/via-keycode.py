#!/usr/bin/env python3
"""Reads or writes one entry of the keyboard's dynamic keymap, over VIA.

One entry, because that is what answers the questions worth asking of a fresh
VIA build. Does the firmware speak the protocol at all? Does a write reach the
keymap? And -- the one this project needed -- does the write survive a reboot,
which is the difference between QMK's transient EEPROM, a RAM buffer that is
cleared on the way up, and the part's own EEPROM.

    python3 tools/via-keycode.py --layer 0 --row 2 --col 2
    python3 tools/via-keycode.py --layer 0 --row 2 --col 2 --set 0x0005

So the persistence test is three commands and no special mode: set it, reboot
the keyboard any way you like, read it back.

Needs read and write on the hidraw node, so run it as root or install
udev/99-corsair-k100.rules.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_usb import PRODUCT, VENDOR, device, identity  # noqa: E402
from k100_via import GET_KEYCODE, SET_KEYCODE, exchange, interface  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vid", default=VENDOR, help="USB vendor id, hex")
    parser.add_argument("--pid", default=PRODUCT, help="USB product id, hex")
    parser.add_argument("--layer", type=int, default=0)
    parser.add_argument("--row", type=int, default=0)
    parser.add_argument("--col", type=int, default=0)
    parser.add_argument("--set", dest="keycode",
                        type=lambda v: int(v, 0), default=None,
                        help="write this keycode instead of reading")
    args = parser.parse_args()

    entry = device(args.vid, args.pid)
    if entry is None:
        print(f"no {args.vid}:{args.pid} on the bus.", file=sys.stderr)
        return 1

    version, product = identity(entry)
    print(f"running  bcdDevice {version}  {product}")

    found = interface(entry)
    if found is None:
        print("none of this device's HID interfaces answered VIA's protocol "
              "version request. Either VIA is not in this firmware, or it is "
              "not reachable.", file=sys.stderr)
        return 1

    node, protocol = found
    print(f"VIA on {node}, protocol {protocol:#06x}")

    where = bytes([args.layer, args.row, args.col])

    if args.keycode is not None:
        exchange(node, bytes([SET_KEYCODE]) + where +
                 args.keycode.to_bytes(2, "big"))

    reply = exchange(node, bytes([GET_KEYCODE]) + where)
    if reply is None or reply[0] != GET_KEYCODE:
        print("the keymap read was not answered.", file=sys.stderr)
        return 1

    keycode = int.from_bytes(reply[4:6], "big")
    print(f"layer {args.layer} row {args.row} col {args.col}: {keycode:#06x}")

    if args.keycode is not None and keycode != args.keycode:
        print(f"\nthe write did not take: asked for {args.keycode:#06x}, read "
              f"back {keycode:#06x}.", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
