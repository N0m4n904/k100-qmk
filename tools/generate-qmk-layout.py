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
    BRAGI_NAMES, QMK_KEYCODES, SENSES, STROBES, STROBE_PIN_BASE, Firmware,
)

# QMK keycodes for the keys that have no HID usage code of their own.
#
# G1-G6 use F13-F18, the conventional stand-in since QMK has no macro-key standard, and are
# trivially remapped in VIA. `profswitch` and `lock` have no QMK equivalent at all and need
# custom keycodes once their purpose is decided.
NON_HID_KEYCODES = {
    "g1": "KC_F13", "g2": "KC_F14", "g3": "KC_F15",
    "g4": "KC_F16", "g5": "KC_F17", "g6": "KC_F18",
    "fn": "MO(1)",
    "profswitch": "KC_NO", "lock": "KC_NO",
    "mute": "KC_MUTE", "ctrlwheelb": "KC_MUTE",
    "stop": "KC_MSTP", "prev": "KC_MPRV", "play": "KC_MPLY", "next": "KC_MNXT",
}

# The two rotary controls. Neither rotation is matrix-scanned, so they are encoders rather
# than keys; the wheel's *button* is in the matrix. Their pins are not yet known.
ENCODERS = [
    ("iCUE dial", "ctrlwheelccw", "ctrlwheelcw", "KC_VOLD", "KC_VOLU"),
    ("volume roller", "voldn", "volup", "KC_VOLD", "KC_VOLU"),
]

LED_FLAG_UNDERGLOW = 0x02
LED_FLAG_KEYLIGHT = 0x04

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


def read_geometry(path: Path) -> dict[str, Key]:
    raw = json.loads(path.read_text())
    return {
        k["name"]: Key(k["name"], k["label"], k["x"], k["y"], k["width"], k["height"],
                       k["has_led"], k["has_scan"])
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


def build_keyboard_json(placed: list[Key], all_keys: list[Key]) -> dict:
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
        "matrix_pins": {
            "custom": True,
            "_comment": (
                f"Strobes are P1_{STROBE_PIN_BASE}..P1_{STROBE_PIN_BASE + STROBES - 1}, "
                f"open-drain outputs; senses are P1_0..P1_{SENSES - 1}, inputs with "
                "optional pull-ups. Driven by matrix_scan_custom(); see docs/pin-mapping.md."
            ),
        },
        "features": {
            "bootmagic": False, "command": False, "console": False,
            "extrakey": True, "mousekey": False, "nkro": True, "rgb_matrix": True,
        },
        "usb": {"vid": "0x1B1C", "pid": "0x1B7C", "device_version": "0.0.1"},
        "rgb_matrix": {
            "driver": "custom",
            "layout": [
                ({"matrix": list(k.matrix), "x": to_qmk(k)[0], "y": to_qmk(k)[1],
                  "flags": LED_FLAG_KEYLIGHT}
                 if k.matrix else
                 {"x": to_qmk(k)[0], "y": to_qmk(k)[1], "flags": LED_FLAG_UNDERGLOW})
                for k in lit
            ],
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
            codes = ("KC_TRNS" if transparent else keycode_for(k) for k in ordered)
            lines.append(f"        /* P1_{STROBE_PIN_BASE + strobe} */ "
                         + ", ".join(c.ljust(width) for c in codes) + ",")
        return "\n".join(lines).rstrip(",")

    encoders = "\n".join(
        f"// {label}: {ccw} / {cw} -- rotation is not matrix-scanned; pins unknown"
        for label, ccw, cw, *_ in ENCODERS
    )

    return f"""// Generated by tools/generate-qmk-layout.py -- do not edit by hand.
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

    // Fn layer, intentionally left as pass-through until the hardware is understood.
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
        json.dumps(build_keyboard_json(placed, list(keys.values())), indent=2) + "\n")

    keymap_dir = args.out / "keymaps" / "default"
    keymap_dir.mkdir(parents=True, exist_ok=True)
    (keymap_dir / "keymap.c").write_text(build_keymap_c(placed))

    args.via_out.parent.mkdir(parents=True, exist_ok=True)
    args.via_out.write_text(json.dumps(build_via_json(placed), indent=2) + "\n")

    lit = [k for k in keys.values() if k.has_led]
    print(f"matrix      {STROBES} x {SENSES} electrical, {len(placed)} positions")
    print(f"encoders    {len(ENCODERS)} (rotations are not matrix-scanned)")
    print(f"rgb matrix  {len(lit)} LEDs "
          f"({sum(1 for k in lit if k.matrix)} key, {sum(1 for k in lit if not k.matrix)} underglow)")
    print(f"written     {args.out / 'keyboard.json'}")
    print(f"            {keymap_dir / 'keymap.c'}")
    print(f"            {args.via_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
