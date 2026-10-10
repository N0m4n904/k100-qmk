#!/usr/bin/env python3
"""Generates the QMK keyboard definition and VIA layout for the K100 from ckb-next's data.

Two inputs are joined:

  * the physical geometry, dumped by tools/extract-ckb-keymap (which links against
    ckb-next's own keymap implementation rather than reimplementing its transform chain), and
  * the LED ordering, read from ckb-next's `keymap_bragi` array, whose array index *is* the
    Bragi LED index.

Nothing here depends on the electrical matrix. QMK positions are a *logical* matrix of our
own choosing; mapping the optical scan onto it is the job of matrix_scan_custom(). See
docs/qmk-layout.md.
"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path

# ckb-next key name -> QMK keycode. Names are positional and follow the US layout, which is
# QMK's own convention, so `z` is the key where US has Z even though DE labels it Y.
KEYCODES = {
    # Media and control strip
    "profswitch": "KC_NO", "ctrlwheelccw": "KC_VOLD", "ctrlwheelb": "KC_MUTE",
    "ctrlwheelcw": "KC_VOLU", "lock": "KC_NO", "mute": "KC_MUTE",
    "volup": "KC_VOLU", "voldn": "KC_VOLD",
    # Macro keys. No QMK standard exists; F13-F18 is the conventional stand-in.
    "g1": "KC_F13", "g2": "KC_F14", "g3": "KC_F15",
    "g4": "KC_F16", "g5": "KC_F17", "g6": "KC_F18",
    # Function row
    "esc": "KC_ESC", **{f"f{n}": f"KC_F{n}" for n in range(1, 13)},
    "prtscn": "KC_PSCR", "scroll": "KC_SCRL", "pause": "KC_PAUS",
    "stop": "KC_MSTP", "prev": "KC_MPRV", "play": "KC_MPLY", "next": "KC_MNXT",
    # Number row
    "grave": "KC_GRV", **{str(n): f"KC_{n}" for n in range(0, 10)},
    "minus": "KC_MINS", "equal": "KC_EQL", "bspace": "KC_BSPC",
    "ins": "KC_INS", "home": "KC_HOME", "pgup": "KC_PGUP",
    "numlock": "KC_NUM", "numslash": "KC_PSLS", "numstar": "KC_PAST", "numminus": "KC_PMNS",
    # Upper row
    "tab": "KC_TAB", **{c: f"KC_{c.upper()}" for c in "qwertyuiop"},
    "lbrace": "KC_LBRC", "rbrace": "KC_RBRC",
    "del": "KC_DEL", "end": "KC_END", "pgdn": "KC_PGDN",
    "num7": "KC_P7", "num8": "KC_P8", "num9": "KC_P9",
    # Home row. `hash` is the ISO key beside Enter.
    "caps": "KC_CAPS", **{c: f"KC_{c.upper()}" for c in "asdfghjkl"},
    "colon": "KC_SCLN", "quote": "KC_QUOT", "hash": "KC_NUHS", "enter": "KC_ENT",
    "num4": "KC_P4", "num5": "KC_P5", "num6": "KC_P6", "numplus": "KC_PPLS",
    # Shift row. `bslash_iso` is the ISO key beside Left Shift.
    "lshift": "KC_LSFT", "bslash_iso": "KC_NUBS", **{c: f"KC_{c.upper()}" for c in "zxcvbnm"},
    "comma": "KC_COMM", "dot": "KC_DOT", "slash": "KC_SLSH", "rshift": "KC_RSFT",
    "up": "KC_UP", "num1": "KC_P1", "num2": "KC_P2", "num3": "KC_P3",
    # Bottom row
    "lctrl": "KC_LCTL", "lwin": "KC_LGUI", "lalt": "KC_LALT", "space": "KC_SPC",
    "ralt": "KC_RALT", "fn": "MO(1)", "rmenu": "KC_APP", "rctrl": "KC_RCTL",
    "left": "KC_LEFT", "down": "KC_DOWN", "right": "KC_RGHT",
    "num0": "KC_P0", "numdot": "KC_PDOT", "numenter": "KC_PENT",
    # ANSI-only and other models' keys, present in the LED table but not on an ISO board.
    "bslash": "KC_BSLS", "rwin": "KC_RGUI", "ro": "KC_INT1", "mr": "KC_NO",
    "light": "KC_NO", "status": "KC_NO",
}

# QMK rgb_matrix flags.
LED_FLAG_UNDERGLOW = 0x02
LED_FLAG_KEYLIGHT = 0x04

# QMK's rgb_matrix coordinate space.
QMK_MAX_X, QMK_MAX_Y = 224, 64

# Keys further apart than this vertically belong to different logical rows.
ROW_GAP = 7

# One key unit in ckb-next's 16ths-of-an-inch grid.
UNIT = 12


@dataclass
class Key:
    name: str
    label: str
    x: int
    y: int
    width: int
    height: int
    has_led: bool
    has_scan: bool
    led: int | None = None
    row: int = -1
    col: int = -1

    @property
    def cx(self) -> float:
        return self.x + self.width / 2

    @property
    def cy(self) -> float:
        return self.y + self.height / 2

    @property
    def keycode(self) -> str:
        return KEYCODES[self.name]


def read_geometry(path: Path) -> tuple[dict, list[Key]]:
    raw = json.loads(path.read_text())
    keys = [Key(k["name"], k["label"], k["x"], k["y"], k["width"], k["height"],
                k["has_led"], k["has_scan"]) for k in raw["keys"]]
    return raw, keys


def read_led_indices(keymap_c: Path) -> dict[str, int]:
    """The array index into ckb-next's `keymap_bragi` is the Bragi LED index."""
    source = keymap_c.read_text()
    start = source.index("const key keymap_bragi[")
    body = source[source.index("{", start) + 1: source.index("\n};", start)]

    entry = re.compile(r'\{\s*(?:"([^"]*)"|0)\s*,\s*(-?\w+)\s*,\s*([A-Za-z0-9_|\s]+?)\s*\}')
    return {m.group(1): index for index, m in enumerate(entry.finditer(body)) if m.group(1)}


def assign_logical_matrix(keys: list[Key]) -> list[list[Key]]:
    """Groups keys into rows by vertical centre, then orders each row left to right.

    Banding on the centre rather than the top edge is what places the tall ISO Enter and the
    numpad's Plus and Enter in their conventional rows.
    """
    scan_keys = sorted((k for k in keys if k.has_scan), key=lambda k: k.cy)

    rows: list[list[Key]] = [[scan_keys[0]]]
    for key in scan_keys[1:]:
        if key.cy - rows[-1][-1].cy > ROW_GAP:
            rows.append([])
        rows[-1].append(key)

    for index, row in enumerate(rows):
        row.sort(key=lambda k: k.cx)
        for col, key in enumerate(row):
            key.row, key.col = index, col
    return rows


def scale(keys: list[Key]):
    """Maps ckb-next's grid onto QMK's rgb_matrix space, preserving the bounding box."""
    lit = [k for k in keys if k.has_led]
    min_x, max_x = min(k.cx for k in lit), max(k.cx for k in lit)
    min_y, max_y = min(k.cy for k in lit), max(k.cy for k in lit)

    def to_qmk(key: Key) -> tuple[int, int]:
        x = (key.cx - min_x) / (max_x - min_x) * QMK_MAX_X
        y = (key.cy - min_y) / (max_y - min_y) * QMK_MAX_Y
        return round(x), round(y)

    return to_qmk


def build_keyboard_json(meta: dict, rows: list[list[Key]], keys: list[Key]) -> dict:
    to_qmk = scale(keys)
    lit = sorted((k for k in keys if k.has_led), key=lambda k: k.led)

    layout = []
    for row in rows:
        for key in row:
            layout.append({
                "matrix": [key.row, key.col],
                "x": round(key.x / UNIT, 2),
                "y": round(key.y / UNIT, 2),
                "w": round(key.width / UNIT, 2),
                "h": round(key.height / UNIT, 2),
                "label": key.label,
            })

    return {
        "manufacturer": "Corsair",
        "keyboard_name": "K100 RGB (OPX, ISO DE)",
        "maintainer": "k100-qmk",
        "processor": "LPC54605",
        "bootloader": "custom",
        "diode_direction": "CUSTOM_MATRIX",
        "matrix_size": {"rows": len(rows), "cols": max(len(r) for r in rows)},
        "features": {
            "bootmagic": False,
            "command": False,
            "console": False,
            "extrakey": True,
            "mousekey": False,
            "nkro": True,
            "rgb_matrix": True,
        },
        "usb": {"vid": "0x1B1C", "pid": "0x1B7C", "device_version": "0.0.1"},
        "rgb_matrix": {
            "driver": "custom",
            "layout": [
                {"matrix": [k.row, k.col], "x": to_qmk(k)[0], "y": to_qmk(k)[1],
                 "flags": LED_FLAG_KEYLIGHT}
                if k.has_scan else
                {"x": to_qmk(k)[0], "y": to_qmk(k)[1], "flags": LED_FLAG_UNDERGLOW}
                for k in lit
            ],
        },
        "layouts": {"LAYOUT_k100_iso_de": {"layout": layout}},
    }


def build_keymap_c(rows: list[list[Key]]) -> str:
    width = max(len(code) for row in rows for code in (k.keycode for k in row))

    body = []
    for row in rows:
        body.append("        " + ", ".join(k.keycode.ljust(width) for k in row) + ",")
    base = "\n".join(body).rstrip(",")

    return f"""// Generated by tools/generate-qmk-layout.py -- do not edit by hand.
#include QMK_KEYBOARD_H

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {{
    [0] = LAYOUT_k100_iso_de(
{base}
    ),

    // Fn layer, intentionally left as pass-through until the hardware is understood.
    [1] = LAYOUT_k100_iso_de(
{chr(10).join("        " + ", ".join("KC_TRNS".ljust(width) for _ in row) + "," for row in rows).rstrip(",")}
    ),
}};
"""


def build_via_json(rows: list[list[Key]]) -> dict:
    """VIA v3 definition. The keymap is KLE, emitted with explicit per-row offsets."""
    kle: list = []
    previous_y = None

    for row in rows:
        cursor = 0.0
        row_y = row[0].y / UNIT
        segment: list = []

        if previous_y is not None and abs(row_y - previous_y) > 0.001:
            segment.append({"y": round(row_y - previous_y - 1, 3)})
        previous_y = row_y

        for key in row:
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
            segment.append(f"{key.row},{key.col}")
            cursor = key.x / UNIT + key.width / UNIT

        kle.append(segment)

    return {
        "name": "Corsair K100 RGB (OPX, ISO DE)",
        "vendorId": "0x1B1C",
        "productId": "0x1B7C",
        "matrix": {"rows": len(rows), "cols": max(len(r) for r in rows)},
        "layouts": {"keymap": kle},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--geometry", type=Path,
                        default=Path("tools/extract-ckb-keymap/k100-de.json"))
    parser.add_argument("--ckb", type=Path, required=True,
                        help="path to a ckb-next checkout")
    parser.add_argument("--out", type=Path, default=Path("qmk/keyboards/corsair/k100"))
    parser.add_argument("--via-out", type=Path, default=Path("via/k100-iso-de.json"))
    args = parser.parse_args()

    meta, keys = read_geometry(args.geometry)
    led_indices = read_led_indices(args.ckb / "src/daemon/keymap.c")

    unmapped = sorted(k.name for k in keys if k.has_scan and k.name not in KEYCODES)
    if unmapped:
        raise SystemExit(f"no QMK keycode for: {', '.join(unmapped)}")

    without_led = sorted(k.name for k in keys if k.has_led and k.name not in led_indices)
    if without_led:
        raise SystemExit(f"no Bragi LED index for: {', '.join(without_led)}")

    for key in keys:
        key.led = led_indices.get(key.name)

    rows = assign_logical_matrix(keys)

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "keyboard.json").write_text(
        json.dumps(build_keyboard_json(meta, rows, keys), indent=2) + "\n")

    keymap_dir = args.out / "keymaps" / "default"
    keymap_dir.mkdir(parents=True, exist_ok=True)
    (keymap_dir / "keymap.c").write_text(build_keymap_c(rows))

    args.via_out.parent.mkdir(parents=True, exist_ok=True)
    args.via_out.write_text(json.dumps(build_via_json(rows), indent=2) + "\n")

    lit = [k for k in keys if k.has_led]
    print(f"layout      {len(rows)} rows x {max(len(r) for r in rows)} cols, "
          f"{sum(len(r) for r in rows)} keys")
    print(f"rgb matrix  {len(lit)} LEDs "
          f"({sum(1 for k in lit if k.has_scan)} key, "
          f"{sum(1 for k in lit if not k.has_scan)} underglow)")
    print(f"written     {args.out / 'keyboard.json'}")
    print(f"            {keymap_dir / 'keymap.c'}")
    print(f"            {args.via_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
