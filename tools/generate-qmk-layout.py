#!/usr/bin/env python3
"""Generates the QMK keyboard definition and VIA layout for the K100.

Three sources are joined:

  * the **electrical matrix**, read from an official firmware image by `k100_firmware.py`:
    8 strobes x 18 senses, with each populated position's HID usage code or Bragi key index;
  * the **physical geometry**, dumped by `tools/extract-ckb-keymap`, which links against
    ckb-next's own keymap implementation rather than transcribing it; and
  * the **LED ordering**, from ckb-next's `keymap_bragi` array, whose index *is* the LED index.

Earlier versions of this generator invented a *logical* matrix, because the electrical one
was unknown. It is known now, so the matrix positions emitted here are the real
`(strobe, sense)` pairs and the custom matrix driver has nothing to translate.

See docs/pin-mapping.md and docs/qmk-layout.md.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_firmware import (  # noqa: E402
    BRAGI_NAMES, QMK_KEYCODES, SENSES, STROBES, STROBE_PIN_BASE, Firmware, Led,
)

# QMK keycodes for the keys that have no HID usage code of their own.
#
# G1-G6 use F13-F18, the conventional stand-in since QMK has no macro-key standard, and are
# trivially remapped in VIA.
#
# `lock` is the Windows lock key the manual names, between the iCUE wheel and the macro keys,
# and GU_TOGG is QMK's own version of it: it suppresses the GUI keys, which is what the key is
# for and what the Win Lock icon above the keys reports. `profswitch` cycles the profiles the
# stock firmware stores on the keyboard, which this one has no equivalent of, so it has no
# keycode until it is given a purpose.
#
# `ctrlwheelb` is the button in the middle of the iCUE wheel. It used to carry
# KC_MUTE, which is what the mute key beside it already does -- two keys for one
# thing, and nothing for the wheel. It now steps the lighting; see
# qmk/keyboards/corsair/k100/lighting_profiles.c.
NON_HID_KEYCODES = {
    "g1": "KC_F13", "g2": "KC_F14", "g3": "KC_F15",
    "g4": "KC_F16", "g5": "KC_F17", "g6": "KC_F18",
    "fn": "MO(1)",
    "profswitch": "KC_NO", "lock": "GU_TOGG",
    "mute": "KC_MUTE", "ctrlwheelb": "K100_RGB_PROFILE",
    "stop": "KC_MSTP", "prev": "KC_MPRV", "play": "KC_MPLY", "next": "KC_MNXT",
}

# The two rotary controls. Neither rotation is matrix-scanned, so they are encoders rather
# than keys; the wheel's *button* is in the matrix. Their pins are not yet known.
ENCODERS = [
    ("iCUE dial", "ctrlwheelccw", "ctrlwheelcw", "KC_VOLD", "KC_VOLU"),
    ("volume roller", "voldn", "volup", "KC_VOLD", "KC_VOLU"),
]

# Keys the Fn layer does not pass through, by (row, column).
#
# Fn+Esc has to reach QK_BOOT. QMK does not speak Bragi, so once this firmware runs there is
# no vendor interface left to flash the keyboard through -- the keymap is one of the three ways
# back to Corsair's bootloader, and the only one a user can reach deliberately while the
# keyboard is working. See qmk/keyboards/corsair/k100/bootloader.c.
#
# Fn+Backspace clears the settings QMK keeps in the part's EEPROM. Those outlive a reflash,
# which is the point of keeping them and also means a changed default here reaches a keyboard
# that has never had settings written, and no other.
FN_OVERRIDES = {
    (0, 1): "QK_BOOT",
    (1, 14): "EE_CLR",
}

LED_FLAG_NONE = 0x00
LED_FLAG_UNDERGLOW = 0x02
LED_FLAG_KEYLIGHT = 0x04

# What an entry carries when nothing places it. Its flags keep every effect
# away, so the coordinates are not wrong -- nothing reads them.
UNPLACED_POSITION = (0, 0)

QMK_MAX_X, QMK_MAX_Y = 224, 64

# One key unit in ckb-next's 16ths-of-an-inch grid.
UNIT = 12


@dataclass
class Key:
    """A physical key, from ckb-next's geometry."""

    name: str
    label: str
    x: int
    y: int
    width: int
    height: int
    has_led: bool
    has_scan: bool
    led: int | None = None
    matrix: tuple[int, int] | None = field(default=None)

    @property
    def cx(self) -> float:
        return self.x + self.width / 2

    @property
    def cy(self) -> float:
        return self.y + self.height / 2


def one_line(label: str) -> str:
    """Collapses a caption onto one line.

    ckb-next spells two-legend keys across two lines, as "Print Screen\nSysRq".
    QMK's keyboard.json schema requires labels to match ^[^\n]*$ and discards
    the whole file otherwise, reporting it as a keyboard with no layouts at all.
    """
    return " ".join(label.split())


def read_geometry(path: Path) -> dict[str, Key]:
    raw = json.loads(path.read_text())
    return {
        k["name"]: Key(k["name"], one_line(k["label"]), k["x"], k["y"],
                       k["width"], k["height"], k["has_led"], k["has_scan"])
        for k in raw["keys"]
    }


def read_led_indices(keymap_c: Path) -> dict[str, int]:
    """The array index into ckb-next's `keymap_bragi` is the Bragi LED index."""
    source = keymap_c.read_text()
    start = source.index("const key keymap_bragi[")
    body = source[source.index("{", start) + 1: source.index("\n};", start)]

    entry = re.compile(r'\{\s*(?:"([^"]*)"|0)\s*,\s*(-?\w+)\s*,\s*([A-Za-z0-9_|\s]+?)\s*\}')
    return {m.group(1): index for index, m in enumerate(entry.finditer(body)) if m.group(1)}


def join_matrix(keys: dict[str, Key], firmware: Firmware, layout: str) -> list[Key]:
    """Attaches each firmware matrix position to the physical key that sits there.

    A position with a HID usage code is matched by inverting the geometry's keycode mapping;
    one without is named directly by its Bragi key index.
    """
    by_keycode: dict[str, list[str]] = {}
    for name in keys:
        code = QMK_KEYCODES_BY_NAME.get(name)
        if code:
            by_keycode.setdefault(code, []).append(name)

    placed = []
    for position in firmware.positions(layout):
        if position.hid:
            keycode = QMK_KEYCODES.get(position.hid)
            if keycode is None:
                raise SystemExit(f"no QMK keycode for HID {position.hid:#04x} at "
                                 f"{position.strobe_pin}/{position.sense_pin}")
            candidates = by_keycode.get(keycode, [])
            if len(candidates) != 1:
                raise SystemExit(
                    f"{keycode} at {position.strobe_pin}/{position.sense_pin} matches "
                    f"{len(candidates)} geometry keys ({', '.join(candidates) or 'none'}); "
                    "the join is ambiguous"
                )
            name = candidates[0]
        else:
            name = BRAGI_NAMES.get(position.bragi)
            if name is None:
                raise SystemExit(f"unknown Bragi key id {position.bragi} at "
                                 f"{position.strobe_pin}/{position.sense_pin}")

        key = keys.get(name)
        if key is None:
            raise SystemExit(f"matrix names {name!r}, absent from the geometry")
        if key.matrix is not None:
            raise SystemExit(f"{name!r} appears at two matrix positions")

        key.matrix = (position.strobe, position.sense)
        placed.append(key)
    return placed


def keycode_for(key: Key) -> str:
    if key.name in NON_HID_KEYCODES:
        return NON_HID_KEYCODES[key.name]
    code = QMK_KEYCODES_BY_NAME.get(key.name)
    if code is None:
        raise SystemExit(f"no QMK keycode for {key.name!r}")
    return code


def scale(keys: list[Key]):
    """Maps ckb-next's grid onto QMK's rgb_matrix space, preserving the bounding box."""
    lit = [k for k in keys if k.has_led]
    min_x, max_x = min(k.cx for k in lit), max(k.cx for k in lit)
    min_y, max_y = min(k.cy for k in lit), max(k.cy for k in lit)

    def to_qmk(key: Key) -> tuple[int, int]:
        return (round((key.cx - min_x) / (max_x - min_x) * QMK_MAX_X),
                round((key.cy - min_y) / (max_y - min_y) * QMK_MAX_Y))

    return to_qmk


def rgb_layout(lit: list[Key], to_qmk, leds: list[Led]) -> list[dict]:
    """The lighting table: a position for each LED, as far as either source knows one.

    Two rules, and both come from somewhere rather than from taste.

    **It is as long as the firmware's own LED map.** QMK will not address an LED
    past the end of this table, and the keyboard addresses some that ckb-next's
    geometry never places -- the status icons among them. Stopping at what has
    coordinates is what made the length a number someone had to choose, and a
    chosen length is one that drifts.

    **An entry whose LED has one channel carries no flags.** The channel table in
    is31fl3743b_leds.c is indexed by the firmware's LED index, so position *n*
    here drives whatever LED *n* is there -- and six of them are single-channel
    status icons. Colouring one is meaningless: it is on or off, and what it
    means is decided by what the keyboard is reporting. See indicators.c.

    The positions themselves come from ckb-next and are dense over what it
    places, which does **not** line up with the firmware's index space. That is
    a real disagreement and it is not settled here; docs/pin-mapping.md has the
    measurements. It costs nothing under one flat colour and would show under
    the first effect that reads a position.
    """
    single_colour = {led.index for led in leds if not led.is_rgb}

    def entry(key: Key) -> dict:
        x, y = to_qmk(key)
        if key.matrix:
            return {"matrix": list(key.matrix), "x": x, "y": y, "flags": LED_FLAG_KEYLIGHT}
        return {"x": x, "y": y, "flags": LED_FLAG_UNDERGLOW}

    unplaced_x, unplaced_y = UNPLACED_POSITION
    table = [entry(key) for key in lit]
    table += [{"x": unplaced_x, "y": unplaced_y, "flags": LED_FLAG_NONE}
              for _ in range(len(leds) - len(table))]

    for index in single_colour:
        table[index]["flags"] = LED_FLAG_NONE
        table[index].pop("matrix", None)

    return table


def build_keyboard_json(placed: list[Key], all_keys: list[Key], leds: list[Led]) -> dict:
    to_qmk = scale(all_keys)
    lit = sorted((k for k in all_keys if k.has_led), key=lambda k: k.led)

    layout = [
        {
            "matrix": list(key.matrix),
            "x": round(key.x / UNIT, 2),
            "y": round(key.y / UNIT, 2),
            "w": round(key.width / UNIT, 2),
            "h": round(key.height / UNIT, 2),
            "label": key.label,
        }
        for key in placed
    ]

    return {
        "manufacturer": "Corsair",
        "keyboard_name": "K100 RGB (OPX, ISO DE)",
        "maintainer": "k100-qmk",
        "processor": "LPC54605",
        "bootloader": "custom",
        "matrix_size": {"rows": STROBES, "cols": SENSES},
        # custom_lite: matrix.c supplies matrix_init_custom() and
        # matrix_scan_custom() and QMK keeps debounce and the rest. The schema
        # sets additionalProperties false here, so the note goes at top level --
        # a stray key in this object makes QMK discard the whole file.
        "matrix_pins": {"custom_lite": True},
        "_comment_matrix_pins": (
            f"Strobes are P1_{STROBE_PIN_BASE}..P1_{STROBE_PIN_BASE + STROBES - 1}, "
            f"push-pull outputs driven low to assert; senses are P1_0..P1_{SENSES - 1}, "
            "inputs with pull-ups. Rows are strobes, columns are senses. Driven by "
            "matrix_scan_custom() in matrix.c; see docs/pin-mapping.md."
        ),
        "features": {
            "bootmagic": False, "command": False, "console": False,
            "extrakey": True, "mousekey": False, "nkro": True, "rgb_matrix": True,
            "via": True,
        },
        "usb": {"vid": "0x1B1C", "pid": "0x1B7C", "device_version": "0.0.1"},
        "rgb_matrix": {
            "driver": "is31fl3743b",
            # The two the wheel button steps through, and no more. An
            # animation a definition offers and nothing drives is a claim
            # rather than a feature -- and the ones left out are the ones that
            # read an LED's position, which this keyboard does not yet have
            # right. See lighting_profiles.c on why the rainbow is CYCLE_ALL.
            "animations": {"solid_color": True, "cycle_all": True},
            # White, full brightness. A stored default survives a reflash, so
            # changing this means moving EECONFIG_MAGIC_NUMBER too; that note
            # lives in the keyboard's config.h, beside the number.
            "default": {"animation": "solid_color", "hue": 0, "sat": 0, "val": 255},
            "layout": rgb_layout(lit, to_qmk, leds),
        },
        "layouts": {"LAYOUT_k100_iso_de": {"layout": layout}},
    }


def build_keymap_c(placed: list[Key]) -> str:
    rows: dict[int, list[Key]] = {}
    for key in placed:
        rows.setdefault(key.matrix[0], []).append(key)

    width = max(len(keycode_for(k)) for k in placed)

    def layer(transparent: bool) -> str:
        lines = []
        for strobe in sorted(rows):
            ordered = sorted(rows[strobe], key=lambda k: k.matrix[1])
            codes = [FN_OVERRIDES.get(k.matrix, "KC_TRNS") if transparent
                     else keycode_for(k) for k in ordered]
            lines.append(f"        /* P1_{STROBE_PIN_BASE + strobe} */ "
                         + ", ".join(c.ljust(width) for c in codes) + ",")
        return "\n".join(lines).rstrip(",")

    encoders = "\n".join(
        f"// {label}: {ccw} / {cw} -- rotation is not matrix-scanned; pins unknown"
        for label, ccw, cw, *_ in ENCODERS
    )

    return f"""// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Generated by tools/generate-qmk-layout.py -- do not edit by hand.
//
// Rows are the keyboard's real strobe lines and columns its sense lines, so a position here
// is the electrical one. See docs/pin-mapping.md.
//
// Not in the matrix, because neither rotation is scanned:
{encoders}
#include QMK_KEYBOARD_H

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {{
    [0] = LAYOUT_k100_iso_de(
{layer(False)}
    ),

    // Fn layer, pass-through except for the overrides below, until the hardware is
    // understood. Fn+Esc is QK_BOOT: see bootloader.c for why a route back to Corsair's
    // bootloader is a requirement rather than a convenience.
    //
    // Fn+Backspace clears the settings QMK keeps in the part's EEPROM, which is the only
    // way back to the defaults compiled in here once something has been stored over them.
    // It is not a convenience either: the stored settings outlive a reflash, so a new
    // default -- a colour, a mode, a brightness -- has no effect on a keyboard that has
    // ever had one written.
    [1] = LAYOUT_k100_iso_de(
{layer(True)}
    ),
}};
"""


def build_via_json(placed: list[Key]) -> dict:
    """VIA v3 definition. The keymap is KLE, emitted with explicit per-row offsets."""
    rows: dict[float, list[Key]] = {}
    for key in placed:
        rows.setdefault(key.y / UNIT, []).append(key)

    kle: list = []
    previous_y = None
    for row_y in sorted(rows):
        segment: list = []
        if previous_y is not None and abs(row_y - previous_y) > 0.001:
            segment.append({"y": round(row_y - previous_y - 1, 3)})
        previous_y = row_y

        cursor = 0.0
        for key in sorted(rows[row_y], key=lambda k: k.x):
            props = {}
            gap = key.x / UNIT - cursor
            if abs(gap) > 0.001:
                props["x"] = round(gap, 3)
            if abs(key.width / UNIT - 1) > 0.001:
                props["w"] = round(key.width / UNIT, 3)
            if abs(key.height / UNIT - 1) > 0.001:
                props["h"] = round(key.height / UNIT, 3)
            if props:
                segment.append(props)
            segment.append(f"{key.matrix[0]},{key.matrix[1]}")
            cursor = key.x / UNIT + key.width / UNIT
        kle.append(segment)

    return {
        "name": "Corsair K100 RGB (OPX, ISO DE)",
        "vendorId": "0x1B1C",
        "productId": "0x1B7C",
        "matrix": {"rows": STROBES, "cols": SENSES},
        "layouts": {"keymap": kle},
    }


# ckb-next key name -> QMK keycode, for the keys that carry a HID usage code. Names are
# positional and follow the US layout, which is QMK's own convention, so `z` is the key where
# US has Z even though DE labels it Y.
QMK_KEYCODES_BY_NAME = {
    "esc": "KC_ESC", **{f"f{n}": f"KC_F{n}" for n in range(1, 13)},
    "prtscn": "KC_PSCR", "scroll": "KC_SCRL", "pause": "KC_PAUS",
    "grave": "KC_GRV", **{str(n): f"KC_{n}" for n in range(10)},
    "minus": "KC_MINS", "equal": "KC_EQL", "bspace": "KC_BSPC",
    "ins": "KC_INS", "home": "KC_HOME", "pgup": "KC_PGUP",
    "numlock": "KC_NUM", "numslash": "KC_PSLS", "numstar": "KC_PAST", "numminus": "KC_PMNS",
    "tab": "KC_TAB", **{c: f"KC_{c.upper()}" for c in "qwertyuiop"},
    "lbrace": "KC_LBRC", "rbrace": "KC_RBRC",
    "del": "KC_DEL", "end": "KC_END", "pgdn": "KC_PGDN",
    "num7": "KC_P7", "num8": "KC_P8", "num9": "KC_P9",
    "caps": "KC_CAPS", **{c: f"KC_{c.upper()}" for c in "asdfghjkl"},
    "colon": "KC_SCLN", "quote": "KC_QUOT", "hash": "KC_NUHS", "enter": "KC_ENT",
    "num4": "KC_P4", "num5": "KC_P5", "num6": "KC_P6", "numplus": "KC_PPLS",
    "lshift": "KC_LSFT", "bslash_iso": "KC_NUBS", **{c: f"KC_{c.upper()}" for c in "zxcvbnm"},
    "comma": "KC_COMM", "dot": "KC_DOT", "slash": "KC_SLSH", "rshift": "KC_RSFT",
    "up": "KC_UP", "num1": "KC_P1", "num2": "KC_P2", "num3": "KC_P3",
    "lctrl": "KC_LCTL", "lwin": "KC_LGUI", "lalt": "KC_LALT", "space": "KC_SPC",
    "ralt": "KC_RALT", "rmenu": "KC_APP", "rctrl": "KC_RCTL",
    "left": "KC_LEFT", "down": "KC_DOWN", "right": "KC_RGHT",
    "num0": "KC_P0", "numdot": "KC_PDOT", "numenter": "KC_PENT",
    "bslash": "KC_BSLS", "ro": "KC_INT1", "rwin": "KC_RGUI",
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--firmware", type=Path, required=True,
                        help="an official firmware image, from tools/fetch-firmware.py")
    parser.add_argument("--geometry", type=Path,
                        default=Path("tools/extract-ckb-keymap/k100-de.json"))
    parser.add_argument("--ckb", type=Path, required=True, help="a ckb-next checkout")
    parser.add_argument("--layout", default="iso")
    parser.add_argument("--out", type=Path, default=Path("qmk/keyboards/corsair/k100"))
    parser.add_argument("--via-out", type=Path, default=Path("via/k100-iso-de.json"))
    args = parser.parse_args()

    keys = read_geometry(args.geometry)
    led_indices = read_led_indices(args.ckb / "src/daemon/keymap.c")
    for name, key in keys.items():
        key.led = led_indices.get(name)

    missing = sorted(n for n, k in keys.items() if k.has_led and k.led is None)
    if missing:
        raise SystemExit(f"no Bragi LED index for: {', '.join(missing)}")

    firmware = Firmware(args.firmware.read_bytes())
    leds = firmware.leds()
    placed = join_matrix(keys, firmware, args.layout)

    # Everything that scans but did not land in the matrix must be an encoder, or the join
    # has silently dropped a key.
    expected_absent = {name for _, ccw, cw, *_ in ENCODERS for name in (ccw, cw)}
    absent = {n for n, k in keys.items() if k.has_scan and k.matrix is None}
    if absent != expected_absent:
        raise SystemExit(
            f"keys that scan but are not in the matrix: {', '.join(sorted(absent))}; "
            f"expected exactly the encoder rotations {', '.join(sorted(expected_absent))}"
        )

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "keyboard.json").write_text(
        json.dumps(build_keyboard_json(placed, list(keys.values()), leds), indent=2) + "\n")

    keymap_dir = args.out / "keymaps" / "default"
    keymap_dir.mkdir(parents=True, exist_ok=True)
    (keymap_dir / "keymap.c").write_text(build_keymap_c(placed))

    args.via_out.parent.mkdir(parents=True, exist_ok=True)
    args.via_out.write_text(json.dumps(build_via_json(placed), indent=2) + "\n")

    lit = [k for k in keys.values() if k.has_led]
    print(f"matrix      {STROBES} x {SENSES} electrical, {len(placed)} positions")
    print(f"encoders    {len(ENCODERS)} (rotations are not matrix-scanned)")
    print(f"rgb matrix  {len(leds)} LEDs in the firmware's map "
          f"({sum(1 for k in lit if k.matrix)} key, "
          f"{sum(1 for k in lit if not k.matrix)} underglow placed by ckb-next, "
          f"{sum(1 for led in leds if not led.is_rgb)} single colour, "
          f"{len(leds) - len(lit)} with no position)")
    print(f"written     {args.out / 'keyboard.json'}")
    print(f"            {keymap_dir / 'keymap.c'}")
    print(f"            {args.via_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
