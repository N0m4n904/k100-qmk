"""Reads the K100's key matrix out of its official firmware image.

Shared by `extract-matrix-map.py`, which presents it, and `generate-qmk-layout.py`, which
generates the QMK definition from it. See docs/pin-mapping.md for how the tables were found.

The image is linked at flash 0x10000, so file offset = flash address - 0x10000.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

BASE = 0x10000

# Indexed by `strobe * SENSES + sense`, holding each position's HID usage code, or 0.
# Identified by the literal that the disassembly of `keyToHidKeycode` loads -- the Esc-then-F
# run that would let a signature search find it appears in *both* tables, so searching picks
# the wrong one.
HID_KEY_CODES = 0x2FEBD - BASE

# Same shape, carrying a Bragi key index for exactly the positions HidKeyCodes leaves at zero.
BRAGI_KEY_IDS = 0x2FDA4 - BASE

# Five per-layout masks of eight words; bit `i` of word `s` marks position (s, i) populated.
MASK_BASE = 0x2FD04 - BASE
MASK_STRIDE = 0x20
MASK_COUNT = 5

STROBES = 8
SENSES = 18

# Strobes drive P1_18..P1_25; senses read P1_0..P1_17.
STROBE_PIN_BASE = 18

# HID usage codes that exist on only some physical layouts, which is what makes the unlabelled
# masks identifiable.
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


@dataclass(frozen=True)
class Position:
    """One populated matrix position."""

    strobe: int
    sense: int
    hid: int
    """HID usage code, or 0 when this key has none."""
    bragi: int
    """Bragi key index, meaningful when `hid` is 0."""

    @property
    def strobe_pin(self) -> str:
        return f"P1_{STROBE_PIN_BASE + self.strobe}"

    @property
    def sense_pin(self) -> str:
        return f"P1_{self.sense}"


class Firmware:
    """The matrix tables of one firmware image."""

    def __init__(self, image: bytes):
        span = STROBES * SENSES
        self.hid_codes = image[HID_KEY_CODES:HID_KEY_CODES + span]
        self.bragi_ids = image[BRAGI_KEY_IDS:BRAGI_KEY_IDS + span]
        if len(self.hid_codes) != span or len(self.bragi_ids) != span:
            raise ValueError("image too short to contain both key tables")

        self.masks: dict[int, list[int]] = {}
        for index in range(MASK_COUNT):
            offset = MASK_BASE + index * MASK_STRIDE
            words = list(struct.unpack("<8I", image[offset:offset + 32]))
            if any(word >> SENSES for word in words):
                raise ValueError(f"mask {index} uses bits above {SENSES}; wrong address?")
            self.masks[offset + BASE] = words

    def markers(self, mask: list[int]) -> set[str]:
        """Which layout-exclusive keys a mask enables."""
        return {
            MARKERS[code]
            for index, code in enumerate(self.hid_codes)
            if code in MARKERS and mask[index // SENSES] >> (index % SENSES) & 1
        }

    def mask_for(self, layout: str) -> tuple[int, list[int]]:
        """The mask for a physical layout, identified by its marker keys.

        Raises rather than guessing: an ambiguous match means the signatures need revisiting,
        and silently picking one would corrupt everything downstream.
        """
        required, forbidden = LAYOUT_SIGNATURES[layout]
        matches = [
            (address, mask)
            for address, mask in self.masks.items()
            if required <= self.markers(mask) and not (forbidden & self.markers(mask))
        ]
        if len(matches) != 1:
            raise ValueError(f"expected exactly one {layout} mask, found {len(matches)}")
        return matches[0]

    def positions(self, layout: str) -> list[Position]:
        """Every populated position for a physical layout, in scan order."""
        _, mask = self.mask_for(layout)
        found = []
        for strobe in range(STROBES):
            for sense in range(SENSES):
                if not (mask[strobe] >> sense & 1):
                    continue
                index = strobe * SENSES + sense
                found.append(Position(strobe, sense,
                                      self.hid_codes[index], self.bragi_ids[index]))
        return found


# HID usage code -> QMK keycode, covering every code this keyboard's table uses.
QMK_KEYCODES = {
    **{0x04 + n: f"KC_{c}" for n, c in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ")},
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

# Bragi key index -> ckb-next's name, for the positions with no HID code of their own.
BRAGI_NAMES = {
    131: "g1", 132: "g2", 133: "g3", 134: "g4", 135: "g5", 136: "g6",
    122: "fn", 128: "profswitch", 137: "ctrlwheelb", 114: "lock", 102: "mute",
    123: "stop", 124: "play", 125: "next", 126: "prev",
}
