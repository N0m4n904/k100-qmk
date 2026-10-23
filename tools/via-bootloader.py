#!/usr/bin/env python3
"""Asks the keyboard to restart into Corsair's bootloader, over VIA.

The same command VIA's own "Bootloader Jump" button sends, which this firmware
answers in `qmk/keyboards/corsair/k100/bootloader.c` -- QMK declares the command
and does not handle it, so on a stock build the button does nothing.

Nothing replies: the keyboard resets inside the command, so silence is what
success sounds like. Which is why this does not wait for an answer but watches
what comes back on the bus, and says which program is answering now.

With it, installing a build needs no key press and no hands:

    python3 tools/via-bootloader.py
    tools/k100-bragi/target/release/k100-bragi flash k100.bin --device 0x1b7c --commit

Needs read and write on the hidraw node, so run it as root or install
udev/99-corsair-k100.rules.
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_usb import BOOTLOADER, PRODUCT, VENDOR, device, identity, program  # noqa: E402
from k100_via import REPORT_SIZE, interface  # noqa: E402

#: VIA's `id_bootloader_jump`.
BOOTLOADER_JUMP = 0x0B

#: Long enough for a reset, a re-enumeration and the bootloader's own startup.
RETURN_TIMEOUT_S = 10.0
POLL_S = 0.25


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

    # Written without reading: the reply would have to come from a firmware that
    # has already reset itself.
    fd = os.open(node, os.O_RDWR)
    try:
        os.write(fd, b"\x00" + bytes([BOOTLOADER_JUMP]).ljust(REPORT_SIZE, b"\x00"))
    finally:
        os.close(fd)
    print("asked it to jump")

    deadline = time.monotonic() + RETURN_TIMEOUT_S
    while time.monotonic() < deadline:
        time.sleep(POLL_S)
        version, _ = identity(device(args.vid, args.pid))
        if version == BOOTLOADER:
            print(f"back as bcdDevice {version}  {program(version)}")
            return 0

    print(f"after {RETURN_TIMEOUT_S:.0f}s it reads bcdDevice {version} "
          f"({program(version)}), so the jump did not happen", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
