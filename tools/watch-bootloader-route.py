#!/usr/bin/env python3
"""Watches the keyboard hand itself back to the bootloader, and says whether it did.

Press Fn+Esc while this runs. That is `QK_BOOT` on the Fn layer, which reaches
`bootloader_jump()` in the keyboard's own code: it writes Corsair's operating
mode into EEPROM word 2, reads it back to check the write took, and resets.

This matters more than it looks. QMK does not speak Bragi, so once this
firmware runs there is no vendor interface left to flash through and no stock
application to do the flashing. Without a route back, replacing a custom image
means opening the case and attaching a programmer -- which is how this project
spent a day recovering a keyboard it had bricked. A route that works turns every
later reflash into two commands over USB.

The route has three distinguishable outcomes on the bus, and this reports which
one happened rather than leaving it to be guessed from a changed `lsusb`:

    the keyboard goes and comes back as the bootloader   the route works
    the keyboard never goes                              the EEPROM write did
                                                         not take, or the key
                                                         never reached QK_BOOT
    the keyboard goes and comes back as itself           it reset, but the
                                                         bootloader did not act
                                                         on the mode

    python3 tools/watch-bootloader-route.py --seconds 60
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_usb import (  # noqa: E402
    ABSENT,
    BOOTLOADER,
    OURS,
    PRODUCT,
    VENDOR,
    device,
    identity,
    program,
)

# Enumeration takes far longer than this, so nothing that reaches the bus can
# slip between two polls -- and the gap is worth measuring, because a reset the
# firmware asked for is immediate where one the watchdog imposed is not.
INTERVAL = 0.02


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vid", default=VENDOR, help="USB vendor id, hex")
    parser.add_argument("--pid", default=PRODUCT, help="USB product id, hex")
    parser.add_argument("--seconds", type=float, default=60.0,
                        help="how long to wait for the attempt")
    args = parser.parse_args()

    before = identity(device(args.vid, args.pid))
    if before[0] == ABSENT:
        print(f"no {args.vid}:{args.pid} on the bus to watch.", file=sys.stderr)
        return 1

    # Refused rather than reported, because the question has no meaning from
    # anywhere else: the bootloader cannot hand itself to the bootloader, and
    # the stock application has Bragi and does not need this route.
    if before[0] != OURS:
        print(f"the keyboard is running {program(before[0])}, and this route "
              f"only exists in this project's firmware. Start it first.",
              file=sys.stderr)
        return 1

    print(f"running  bcdDevice {before[0]}  {before[1]}")
    print(f"press Fn+Esc now; watching for {args.seconds:.0f}s")

    deadline = time.monotonic() + args.seconds
    left_at = None

    while time.monotonic() < deadline:
        time.sleep(INTERVAL)
        now = identity(device(args.vid, args.pid))

        if left_at is None:
            if now[0] == ABSENT:
                left_at = time.monotonic()
                print("  left the bus")
            continue

        if now[0] == ABSENT:
            continue

        away = time.monotonic() - left_at
        print(f"  back after {away:.2f}s as bcdDevice {now[0]}  {now[1]}")

        if now[0] == BOOTLOADER:
            print("\nThe route works. The keyboard asked for the bootloader "
                  "and got it, so a reflash from here needs no programmer:")
            print("  k100-bragi flash <image> --device 0x1b7c --commit")
            print("  k100-bragi exit-bootloader")
            print("\nThis looks identical to the firmware falling over, "
                  "because it is the same door: the route forces the watchdog "
                  "reset that a crash would have caused anyway. What "
                  "distinguishes them is that this one happened when the key "
                  "was pressed.")
            return 0

        if now[0] == OURS:
            print("\nIt reset and came straight back to itself, so the reset "
                  "works and the mode does not.")
            print("\nThat narrows it to the bootloader's side. "
                  "k100_enter_bootloader() resets only after writing the mode "
                  "word and reading it back, so a reset at all means the "
                  "write and the read-back both succeeded -- and the "
                  "bootloader started this firmware anyway. Either it does "
                  "not key on that word, or it wants more than that word.")
            return 1

        print(f"\nIt came back as {program(now[0])}, which is neither this "
              f"firmware nor the bootloader.")
        return 1

    if left_at is not None:
        print("\nIt left the bus and did not come back inside the window. "
              "That is the state this project's recovery exists for; see "
              "swd/README.md.")
        return 1

    print("\nIt never left the bus. Either Fn+Esc did not reach QK_BOOT, or "
          "the EEPROM write did not take -- bootloader_jump() returns to a "
          "keyboard that keeps running when that check fails, which is "
          "deliberate and is why nothing happened.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
