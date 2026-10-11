#!/usr/bin/env python3
"""Extracts the K100's electrical key matrix from its official firmware.

The firmware carries two tables that together give the real matrix:

  * `HidKeyCodes`, 144 bytes at flash 0x2FEBD, indexed by `strobe * 18 + sense` and holding
    the HID usage code for that position. It contains every physical layout's keys at once.
  * five per-layout masks of 8 words each from flash 0x2FD04, where bit `i` of word `s`
    marks position `(s, i)` as populated.

Both were located by disassembling the functions named in the firmware's own assert
strings — `keyToHidKeycode` in `keyToHidCodeMap.c` indexes the first, and the mask
initialiser copies one group into SRAM. See docs/pin-mapping.md.

Usage: extract-matrix-map.py <firmware.bin> [--layout iso]
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

# Flash 0x2FEBD, and the application is linked at flash 0x10000.
HID_KEY_CODES = 0x2FEBD - 0x10000
MASK_BASE = 0x2FD04 - 0x10000
MASK_STRIDE = 0x20

STROBES = 8
SENSES = 18

# Strobes drive P1_18..P1_25; senses read P1_0..P1_17.
STROBE_PIN_BASE = 18

# Marker keys that identify a physical layout, since the masks are not labelled.
MARKERS = {
    0x31: "ansi_backslash",
    0x32: "iso_hash",
    0x64: "iso_backslash",
    0x87: "jis_ro",
    0x89: "jis_yen",
    0x8A: "jis_henkan",
    0x8B: "jis_muhenkan",
}

LAYOUT_SIGNATURES = {
    "ansi": ({"ansi_backslash"}, {"iso_hash", "iso_backslash", "jis_ro"}),
    "iso": ({"iso_hash", "iso_backslash"}, {"ansi_backslash", "jis_ro", "jis_yen"}),
    "jis": ({"iso_hash", "jis_ro", "jis_yen"}, {"ansi_backslash", "iso_backslash"}),
}

# HID usage code -> QMK keycode. Only codes this keyboard's table actually uses.
QMK = {
    0x04: "KC_A", 0x05: "KC_B", 0x06: "KC_C", 0x07: "KC_D", 0x08: "KC_E", 0x09: "KC_F",
    0x0A: "KC_G", 0x0B: "KC_H", 0x0C: "KC_I", 0x0D: "KC_J", 0x0E: "KC_K", 0x0F: "KC_L",
    0x10: "KC_M", 0x11: "KC_N", 0x12: "KC_O", 0x13: "KC_P", 0x14: "KC_Q", 0x15: "KC_R",
    0x16: "KC_S", 0x17: "KC_T", 0x18: "KC_U", 0x19: "KC_V", 0x1A: "KC_W", 0x1B: "KC_X",
    0x1C: "KC_Y", 0x1D: "KC_Z",
    0x1E: "KC_1", 0x1F: "KC_2", 0x20: "KC_3", 0x21: "KC_4", 0x22: "KC_5", 0x23: "KC_6",
    0x24: "KC_7", 0x25: "KC_8", 0x26: "KC_9", 0x27: "KC_0",
    0x28: "KC_ENT", 0x29: "KC_ESC", 0x2A: "KC_BSPC", 0x2B: "KC_TAB", 0x2C: "KC_SPC",
    0x2D: "KC_MINS", 0x2E: "KC_EQL", 0x2F: "KC_LBRC", 0x30: "KC_RBRC", 0x31: "KC_BSLS",
    0x32: "KC_NUHS", 0x33: "KC_SCLN", 0x34: "KC_QUOT", 0x35: "KC_GRV", 0x36: "KC_COMM",
    0x37: "KC_DOT", 0x38: "KC_SLSH", 0x39: "KC_CAPS",
    **{0x3A + n: f"KC_F{n + 1}" for n in range(12)},
    0x46: "KC_PSCR", 0x47: "KC_SCRL", 0x48: "KC_PAUS", 0x49: "KC_INS", 0x4A: "KC_HOME",
    0x4B: "KC_PGUP", 0x4C: "KC_DEL", 0x4D: "KC_END", 0x4E: "KC_PGDN",
    0x4F: "KC_RGHT", 0x50: "KC_LEFT", 0x51: "KC_DOWN", 0x52: "KC_UP",
    0x53: "KC_NUM", 0x54: "KC_PSLS", 0x55: "KC_PAST", 0x56: "KC_PMNS", 0x57: "KC_PPLS",
    0x58: "KC_PENT", 0x59: "KC_P1", 0x5A: "KC_P2", 0x5B: "KC_P3", 0x5C: "KC_P4",
    0x5D: "KC_P5", 0x5E: "KC_P6", 0x5F: "KC_P7", 0x60: "KC_P8", 0x61: "KC_P9",
    0x62: "KC_P0", 0x63: "KC_PDOT", 0x64: "KC_NUBS", 0x65: "KC_APP",
    0x87: "KC_INT1", 0x89: "KC_INT3", 0x8A: "KC_INT4", 0x8B: "KC_INT5",
    0xE0: "KC_LCTL", 0xE1: "KC_LSFT", 0xE2: "KC_LALT", 0xE3: "KC_LGUI",
    0xE4: "KC_RCTL", 0xE5: "KC_RSFT", 0xE6: "KC_RALT",
}


def read_tables(image: bytes) -> tuple[bytes, dict[int, list[int]]]:
    codes = image[HID_KEY_CODES:HID_KEY_CODES + STROBES * SENSES]
    if len(codes) != STROBES * SENSES:
        raise SystemExit("image too short to contain HidKeyCodes")

    masks = {}
    for index in range(5):
        offset = MASK_BASE + index * MASK_STRIDE
        words = list(struct.unpack("<8I", image[offset:offset + 32]))
        if any(word >> SENSES for word in words):
            raise SystemExit(f"mask group {index} uses bits above {SENSES}; wrong address?")
        masks[offset + 0x10000] = words
    return codes, masks


def identify(codes: bytes, mask: list[int]) -> set[str]:
    present = set()
    for position, code in enumerate(codes):
        if code in MARKERS and mask[position // SENSES] >> (position % SENSES) & 1:
            present.add(MARKERS[code])
    return present


def pick_layout(codes: bytes, masks: dict[int, list[int]], wanted: str) -> tuple[int, list[int]]:
    required, forbidden = LAYOUT_SIGNATURES[wanted]
    matches = [
        (address, mask)
        for address, mask in masks.items()
        if required <= identify(codes, mask) and not (forbidden & identify(codes, mask))
    ]
    if len(matches) != 1:
        raise SystemExit(
            f"expected exactly one {wanted} mask, found {len(matches)}. "
            "The marker signatures need revisiting."
        )
    return matches[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", type=Path)
    parser.add_argument("--layout", default="iso", choices=sorted(LAYOUT_SIGNATURES))
    args = parser.parse_args()

    codes, masks = read_tables(args.image.read_bytes())

    print("layout masks, identified by which marker keys they enable:")
    for address, mask in masks.items():
        popcount = sum(bin(word).count("1") for word in mask)
        found = identify(codes, mask)
        print(f"  {address:#07x}  {popcount:3} positions  {', '.join(sorted(found)) or 'none'}")

    address, mask = pick_layout(codes, masks, args.layout)
    print(f"\n{args.layout.upper()} mask is {address:#07x}\n")

    unmapped = set()
    rows = []
    for strobe in range(STROBES):
        row = []
        for sense in range(SENSES):
            if not (mask[strobe] >> sense & 1):
                row.append(None)
                continue
            code = codes[strobe * SENSES + sense]
            if code == 0:
                row.append("XXXXXXX")       # populated, but no HID code of its own
            elif code in QMK:
                row.append(QMK[code])
            else:
                row.append(f"HID_{code:02X}")
                unmapped.add(code)
        rows.append(row)

    print(f"electrical matrix, strobe P1_{STROBE_PIN_BASE}..P1_{STROBE_PIN_BASE + STROBES - 1}"
          f" x sense P1_0..P1_{SENSES - 1}")
    print("  XXXXXXX = key present but with no HID usage code (macro, media or wheel)\n")
    for strobe, row in enumerate(rows):
        cells = " ".join((cell or "-------").ljust(7) for cell in row)
        print(f"  P1_{STROBE_PIN_BASE + strobe:<2} {cells}")

    populated = sum(1 for row in rows for cell in row if cell)
    hid = sum(1 for row in rows for cell in row if cell and cell != "XXXXXXX")
    print(f"\n  {populated} positions populated: {hid} with a HID code, "
          f"{populated - hid} without")
    if unmapped:
        print(f"  no QMK mapping for HID codes: "
              f"{', '.join(f'{c:#04x}' for c in sorted(unmapped))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
