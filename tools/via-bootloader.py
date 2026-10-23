#!/usr/bin/env python3
"""Asks the keyboard to restart into Corsair's bootloader, over VIA.

The same command VIA's own "Bootloader Jump" button sends, which this firmware
answers in `qmk/keyboards/corsair/k100/bootloader.c` -- QMK declares the command
and does not handle it, so on a stock build the button does nothing.

Nothing replies: the keyboard resets inside the command, so silence is what
success sounds like. Which is why this does not wait for an answer but watches
what comes back on the bus, and says which program is answering now.

This is the jump on its own, for watching it happen or for getting a keyboard
into the bootloader and leaving it there. To install a build, use
`k100-update.py`, which does this and the writing in one command -- two commands
are a problem on a desk with one keyboard, because the first one takes it away.

Needs read and write on the hidraw node, so run it as root or install
udev/99-corsair-k100.rules.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_usb import (BOOTLOADER, PRODUCT, VENDOR, device, identity,  # noqa: E402
                      program, wait_for)
from k100_via import ask_to_jump, interface  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vid", default=VENDOR, help="USB vendor id, hex")
    parser.add_argument("--pid", default=PRODUCT, help="USB product id, hex")
    args = parser.parse_args()

    entry = device(args.vid, args.pid)
    if entry is None:
        print(f"no {args.vid}:{args.pid} on the bus.", file=sys.stderr)
        return 1

    version, _ = identity(entry)
    print(f"running  bcdDevice {version}  {program(version)}")
    if version == BOOTLOADER:
        print("already in the bootloader; nothing to do")
        return 0

    found = interface(entry)
    if found is None:
        print("no interface answered VIA, so there is nothing to ask", file=sys.stderr)
        return 1
    node, _ = found

    ask_to_jump(node)
    print("asked it to jump")

    version = wait_for(BOOTLOADER, args.vid, args.pid)
    if version == BOOTLOADER:
        print(f"back as bcdDevice {version}  {program(version)}")
        return 0

    print(f"it reads bcdDevice {version} ({program(version)}), so the jump did not "
          f"happen", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
