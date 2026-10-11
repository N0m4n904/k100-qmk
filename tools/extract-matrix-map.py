#!/usr/bin/env python3
"""Prints the K100's electrical key matrix, read from an official firmware image.

The tables and how they were located are described in docs/pin-mapping.md; the reading
itself lives in k100_firmware.py, which generate-qmk-layout.py also uses.

Usage: extract-matrix-map.py <firmware.bin> [--layout iso]
"""

from __future__ import annotations

import argparse
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_firmware import (  # noqa: E402
    BRAGI_NAMES, LAYOUT_SIGNATURES, PWM_PAGE_SIZE, QMK_KEYCODES, SENSES, STROBES,
    STROBE_PIN_BASE, Firmware,
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", type=Path)
    parser.add_argument("--layout", default="iso", choices=sorted(LAYOUT_SIGNATURES))
    parser.add_argument("--leds", action="store_true",
                        help="also print the LED driver map: which chip and channel drives "
                             "each colour of each LED")
    args = parser.parse_args()

    firmware = Firmware(args.image.read_bytes())

    print("layout masks, identified by which marker keys they enable:")
    for address, mask in firmware.masks.items():
        popcount = sum(bin(word).count("1") for word in mask)
        found = ", ".join(sorted(firmware.markers(mask))) or "none"
        print(f"  {address:#07x}  {popcount:3} positions  {found}")

    try:
        address, _ = firmware.mask_for(args.layout)
    except ValueError as error:
        raise SystemExit(f"{error}. The marker signatures need revisiting.")
    print(f"\n{args.layout.upper()} mask is {address:#07x}\n")

    cells: dict[tuple[int, int], str] = {}
    unmapped = set()
    for position in firmware.positions(args.layout):
        if position.hid:
            name = QMK_KEYCODES.get(position.hid)
            if name is None:
                name = f"HID_{position.hid:02X}"
                unmapped.add(position.hid)
        else:
            name = BRAGI_NAMES.get(position.bragi, f"id{position.bragi}")
        cells[(position.strobe, position.sense)] = name

    print(f"electrical matrix, strobe P1_{STROBE_PIN_BASE}..P1_{STROBE_PIN_BASE + STROBES - 1}"
          f" x sense P1_0..P1_{SENSES - 1}")
    print("  names without a KC_ prefix have no HID usage code; they are Bragi key ids\n")
    for strobe in range(STROBES):
        row = " ".join(cells.get((strobe, sense), "-------").ljust(8)
                       for sense in range(SENSES))
        print(f"  P1_{STROBE_PIN_BASE + strobe:<2} {row}")

    hid = sum(1 for name in cells.values() if name.startswith("KC_"))
    print(f"\n  {len(cells)} positions populated: {hid} with a HID code, "
          f"{len(cells) - hid} without")
    if unmapped:
        print("  no QMK mapping for HID codes: "
              + ", ".join(f"{code:#04x}" for code in sorted(unmapped)))

    if args.leds:
        print_leds(firmware)
    return 0


def print_leds(firmware: Firmware) -> None:
    leds = firmware.leds()
    per_chip: dict[int, set[int]] = defaultdict(set)
    for led in leds:
        for chip, channel in led.channels:
            per_chip[chip].add(channel)

    print(f"\nLED driver map: {len(leds)} LEDs across {len(per_chip)} "
          f"IS31FL3743B chips on SPI3")
    print(f"  {sum(1 for l in leds if l.is_rgb)} RGB, "
          f"{sum(1 for l in leds if not l.is_rgb)} single colour")
    for chip in sorted(per_chip):
        used = per_chip[chip]
        print(f"  chip {chip}: {len(used)} of {PWM_PAGE_SIZE} channels used, "
              f"{min(used)}..{max(used)}")

    print("\n  led  chip  R    G    B    rgb")
    for led in leds[:4] + leds[-2:]:
        (c, r), (_, g), (_, b) = led.channels
        print(f"  {led.index:3}  {c:4}  {r:3}  {g:3}  {b:3}  {led.is_rgb}")
    print("  (full table available programmatically via Firmware.leds())")


if __name__ == "__main__":
    raise SystemExit(main())
