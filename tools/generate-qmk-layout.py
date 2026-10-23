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
# stock firmware stores on the keyboard, which this one has no equivalent of; it turns the
# lighting off and on instead, which is the nearest thing the keyboard has to a profile and
# sits beside the wheel that sets the brightness.
#
# `ctrlwheelb` is the button in the middle of the iCUE wheel. It used to carry
# KC_MUTE, which is what the mute key beside it already does -- two keys for one
# thing, and nothing for the wheel. It now steps the lighting; see
# qmk/keyboards/corsair/k100/lighting_profiles.c.
NON_HID_KEYCODES = {
    "g1": "KC_F13", "g2": "KC_F14", "g3": "KC_F15",
    "g4": "KC_F16", "g5": "KC_F17", "g6": "KC_F18",
    "fn": "MO(1)",
    "profswitch": "K100_LIGHTS_TOGGLE", "lock": "GU_TOGG",
    "mute": "KC_MUTE", "ctrlwheelb": "K100_RGB_PROFILE",
    "stop": "KC_MSTP", "prev": "KC_MPRV", "play": "KC_MPLY", "next": "KC_MNXT",
}

# How VIA names this keyboard's own keycodes.
#
# VIA knows every standard QMK keycode and none of these, so without an entry here the key
# reads as a bare `0x7e00` in the app and cannot be picked from the list at all -- it has to be
# typed as a hex code into `Any`.
#
# Keyed by the C name rather than ordered, because the order VIA needs is k100.h's: its
# `customKeycodes` array is positional, entry 0 being the first `QK_KB` keycode. Taking the
# order from the header instead of from here is what stops the two drifting apart, and a
# drifted order is silent -- the app would simply label the wrong keycode.
CUSTOM_KEYCODE_LABELS = {
    "K100_RGB_PROFILE": {
        "name": "LED Profile",
        "title": "Steps the lighting through white, red and rainbow",
        "shortName": "LED",
    },
    "K100_LIGHTS_TOGGLE": {
        "name": "Lights",
        "title": "Turns the key lighting off and on, status icons included",
        "shortName": "Light",
    },
}

# The two rotary controls. Neither rotation is matrix-scanned, so they are encoders rather
# than keys; the wheel's *button* is in the matrix.
#
# The pins come from the stock firmware's INPUTMUX configuration, which attaches exactly four
# pins to pin interrupts -- P1_28 to P1_31, all outside the matrix, which is what two
# quadrature encoders need. The pairing below groups them adjacently, which is how the
# firmware's own index-to-channel table groups them, and turning them confirmed it: the first
# pair is the iCUE wheel and the second the volume roller. What each one does, and which way
# round, is in the keyboard's encoders.c rather than here.
ENCODER_PINS = [("P1_28", "P1_29"), ("P1_30", "P1_31")]

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

# The RGB effects this firmware is built with, in the order QMK numbers them.
#
# The order is not ours to pick. QMK numbers the effects it compiles in the order its own
# rgb_matrix_effects.inc includes them, starting at 1, and that number is what VIA's menu
# sends. Checked against the built firmware rather than assumed: the profile table in
# lighting_profiles.c compiles to modes 1 and 2.
#
# Effects that read a position were left out while the LED positions were not known to agree
# with the LEDs they are attached to. The key LEDs are settled now -- the first 119 entries
# follow the matrix, measured with the LED probe -- so one that sweeps across the keys is
# built: `cycle_left_right`. The lights that are not keys are still unmeasured, so a sweep
# reaches them in whatever order ckb-next had, which is the one thing to expect to look wrong.
#
# What each one responds to decides which controls VIA offers alongside it.
#
# `custom` marks the keyboard's own, in rgb_matrix_kb.inc, which QMK numbers after every
# built-in and enables through rules.mk rather than through keyboard.json -- so each is
# keyed by the name its own kind is known by: a built-in by the lowercase name
# keyboard.json takes, ours by the C name the effect is declared with.
ANIMATIONS = {
    "solid_color": {"label": "Solid Color", "colour": True, "speed": False},
    "cycle_all": {"label": "Rainbow", "colour": False, "speed": True},
    "cycle_left_right": {"label": "Rainbow Sweep", "colour": False, "speed": True},
    "K100_LIGHTS_OFF": {"label": "Off", "colour": False, "speed": False, "custom": True},
}

# QMK's channel for the RGB matrix and the prefix VIA's own menu gives its four values,
# from `enum via_channel_id` and `enum via_qmk_rgb_matrix_value`. The names are VIA's to
# recognise; the numbers are what reaches the firmware.
# This keyboard's own channel -- `id_custom_channel`, the one VIA leaves to keyboards -- and
# the prefix the application knows its values by. The numbers on it are read from the
# keyboard's own header; see via_value().
K100_CHANNEL = 0
K100_VALUE = "id_k100_"

RGB_MATRIX_CHANNEL = 3
RGB_MATRIX_VALUE = "id_qmk_rgb_matrix_"
EFFECT_VALUE = f"{RGB_MATRIX_VALUE}effect"

LED_FLAG_NONE = 0x00
LED_FLAG_UNDERGLOW = 0x02
LED_FLAG_KEYLIGHT = 0x04

# What an entry carries when nothing places it. Its flags keep every effect
# away, so the coordinates are not wrong -- nothing reads them.
UNPLACED_POSITION = (0, 0)

# The firmware's last eleven lights are the right light bar.
#
# Measured rather than derived: nothing ever lit them, and writing their channels by hand lit
# the right bar. ckb-next has the bar too, under these names, but at different indices -- so
# its coordinates are borrowed here while its own entries stay where ckb-next puts them. Both
# cannot be right, and which of the two drives the real bar is now known: this one.
#
# Which end of the bar index 179 is has **not** been measured. A sweep that runs the wrong way
# on this bar alone is what that would look like, and the fix is to reverse this tuple.
RIGHT_BAR = tuple(f"rightbar{segment}" for segment in range(1, 12))

QMK_MAX_X, QMK_MAX_Y = 224, 64

# This firmware's version, declared once and nowhere else.
#
# Three readers want it and all three have to agree, so all three are fed from here. QMK builds
# it into the USB descriptor as `DEVICE_VER`, which is the only thing that tells this firmware
# apart from Corsair's -- `tools/k100_usb.py` maps the number to a name, so changing this means
# changing that in the same breath. The keyboard's config.h hands the same macro to VIA as its
# firmware version. And `tools/pack-firmware.py` reads it back out of the generated
# keyboard.json for the container header, which is what Corsair's bootloader reads.
#
# QMK encodes it as BCD: 0.1.0 becomes 0x0010. Corsair's container carries only the minor and
# the patch, so the leading number stays where it is whatever this says.
FIRMWARE_VERSION = "0.1.0"

# One key unit in ckb-next's 16ths-of-an-inch grid.
UNIT = 12

# The one key ckb-next cannot describe. An ISO enter is two rectangles, 1.5 units wide in the
# row above and 1.25 in the row below, where the key beside it tucks under the overhang --
# measured, not assumed: `hash` ends 0.25 units past where the enter's box begins. ckb-next
# stores the box around the pair, which is why the application drew a plain tall block.
#
# KLE says it with a second rectangle. `indent` moves the key's own rectangle right to where
# the narrow part starts, and `x2` reaches back out for the wide one. Keyed by ckb-next's
# name, and only the VIA definition carries it: QMK's layout has no second rectangle, and
# nothing in the firmware looks at a key's shape.
L_SHAPED = {
    "enter": {"indent": 0.25, "props": {"w": 1.25, "w2": 1.5, "h2": 1, "x2": -0.25}},
}


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

    # ckb-next positions a key by its **centre**, which is what `x` and `y` are
    # here. QMK's layout and KLE both position one by its top left corner, so
    # the two have to be told apart: `cx`/`cy` for where the LED sits, and
    # `left`/`top` for where the key starts.
    #
    # Read off the map rather than assumed: adjacent keys differ by half of
    # both widths, not by the left one's. `lctrl` at 25 is 16 wide and `lwin`
    # at 39 is 12, and 39 - 25 is 14, which is (16 + 12) / 2.

    @property
    def cx(self) -> float:
        return float(self.x)

    @property
    def cy(self) -> float:
        return float(self.y)

    @property
    def left(self) -> float:
        return self.x - self.width / 2

    @property
    def top(self) -> float:
        return self.y - self.height / 2


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

    Three rules, and all three come from somewhere rather than from taste.

    **The first 119 entries are the keys, in the matrix's own order.** Measured on the
    keyboard with the LED probe: index 0 lights G1, which is strobe 0, sense 0, and the number
    pad lights in the order the matrix has it rather than the order it is printed in. This was
    ckb-next's ordering before, which is alphabetical by key name -- so every key LED was
    attached to the wrong key, invisibly, because a flat colour looks the same either way.

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

    **What comes after the keys is still ckb-next's order, and still unverified.** The
    firmware's table continues with 54 more lights, then the six icons, then the right light
    bar -- which is known because the bar is the one thing that never lit until its channels
    were written by hand. Which of the remaining 54 is which is not measured yet, so they keep
    the order they had: wrong in the same way as before rather than newly wrong, and visible
    only to an effect that reads a position.
    """
    single_colour = {led.index for led in leds if not led.is_rgb}

    def entry(key: Key) -> dict:
        x, y = to_qmk(key)
        if key.matrix:
            return {"matrix": list(key.matrix), "x": x, "y": y, "flags": LED_FLAG_KEYLIGHT}
        return {"x": x, "y": y, "flags": LED_FLAG_UNDERGLOW}

    scanning = sorted((key for key in lit if key.matrix), key=lambda k: k.matrix)
    resting = [key for key in lit if not key.matrix]

    unplaced_x, unplaced_y = UNPLACED_POSITION
    table = [entry(key) for key in scanning] + [entry(key) for key in resting]
    table += [{"x": unplaced_x, "y": unplaced_y, "flags": LED_FLAG_NONE}
              for _ in range(len(leds) - len(table))]

    # The right bar, at the end of the firmware's table, where this used to leave padding.
    by_name = {key.name: key for key in lit}
    for offset, name in enumerate(RIGHT_BAR):
        segment = by_name.get(name)
        if segment is None:
            raise SystemExit(f"the geometry has no {name}, so the right bar cannot be placed")
        x, y = to_qmk(segment)
        table[len(leds) - len(RIGHT_BAR) + offset] = {
            "x": x, "y": y, "flags": LED_FLAG_UNDERGLOW,
        }

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
            "x": round(key.left / UNIT, 2),
            "y": round(key.top / UNIT, 2),
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
            "encoder": True,
            "extrakey": True, "mousekey": False, "nkro": True, "rgb_matrix": True,
            "via": True,
        },
        "usb": {"vid": "0x1B1C", "pid": "0x1B7C", "device_version": FIRMWARE_VERSION},
        "rgb_matrix": {
            "driver": "is31fl3743b",
            # The two the wheel button steps through, and no more. An
            # animation a definition offers and nothing drives is a claim
            # rather than a feature. See ANIMATIONS for why these two, and
            # lighting_profiles.c on why the rainbow is CYCLE_ALL. The
            # keyboard's own effect is not named here: QMK takes that one
            # through rules.mk, and the schema knows only its own.
            "animations": {name: True for name, animation in ANIMATIONS.items()
                           if not animation.get("custom")},
            # White, full brightness. A stored default survives a reflash, so
            # changing this means moving EECONFIG_MAGIC_NUMBER too; that note
            # lives in the keyboard's config.h, beside the number.
            "default": {"animation": "solid_color", "hue": 0, "sat": 0, "val": 255},
            "layout": rgb_layout(lit, to_qmk, leds),
        },
        # What each one does is decided in encoders.c, not here: one sets the
        # lighting's brightness and the other the volume, and neither is a
        # keycode a keymap would want to carry.
        "encoder": {
            "rotary": [{"pin_a": a, "pin_b": b} for a, b in ENCODER_PINS],
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
        f"// {label}: {ccw} / {cw} -- not scanned; encoder {index} on {a}/{b}"
        for index, ((label, ccw, cw, *_), (a, b))
        in enumerate(zip(ENCODERS, ENCODER_PINS))
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


def effect_numbers(responds_to: str) -> list[int]:
    """The effect numbers that answer to a given control."""
    return [number for number, animation in enumerate(ANIMATIONS.values(), start=1)
            if animation[responds_to]]


def only_for(effects: list[int]) -> dict:
    """A `showIf` restricting a control to those effects, or nothing if all of them do.

    VIA shows a control unconditionally when it carries no condition, which is the right
    answer when every effect responds to it, and keeps the condition out of the definition
    rather than emitting one that is always true.
    """
    if len(effects) == len(ANIMATIONS):
        return {}
    return {"showIf": " || ".join(f"{{{EFFECT_VALUE}}} == {number}" for number in effects)}


def lighting_menu() -> dict:
    """VIA's lighting controls, declared here rather than taken from its built-in set.

    VIA ships a `qmk_rgb_matrix` menu that a definition can ask for by name, and it is the
    wrong menu for any keyboard that does not compile every effect QMK has. Its dropdown
    lists all 45 of them and sends the position in that list, while QMK numbers only the
    effects that are *built*. With two compiled, 2 is this keyboard's rainbow and VIA's
    list has "Breathing" there, so picking breathing would silently start the rainbow.

    Declaring the menu instead means the dropdown holds exactly the effects this firmware
    has, at the numbers it answers to, and that the colour and speed controls appear only
    for the effects that read them.

    Zero is not offered at all, although VIA's own menu offers it as "All Off". Sending it
    does not select an effect, it disables the matrix, and QMK draws the status icons only
    while an effect is running -- so turning the key lighting off from the app would take
    Num Lock with it. "Off" here is an effect that paints black, in rgb_matrix_kb.inc, and
    the icons stay lit under it. The entries are in the order QMK numbers them, which puts
    that one last, since it is the keyboard's own.
    """
    return {
        "label": "Lighting",
        "content": [{
            "label": "Backlight",
            "content": [
                {"label": "Brightness", "type": "range", "options": [0, 255],
                 "content": [f"{RGB_MATRIX_VALUE}brightness", RGB_MATRIX_CHANNEL, 1]},
                {"label": "Effect", "type": "dropdown",
                 "options": [[animation["label"], number] for number, animation
                             in enumerate(ANIMATIONS.values(), start=1)],
                 "content": [EFFECT_VALUE, RGB_MATRIX_CHANNEL, 2]},
                {"label": "Effect Speed", "type": "range", "options": [0, 255],
                 **only_for(effect_numbers("speed")),
                 "content": [f"{RGB_MATRIX_VALUE}effect_speed", RGB_MATRIX_CHANNEL, 3]},
                {"label": "Color", "type": "color",
                 **only_for(effect_numbers("colour")),
                 "content": [f"{RGB_MATRIX_VALUE}color", RGB_MATRIX_CHANNEL, 4]},
            ],
        }],
    }


def via_value(header: Path, name: str) -> int:
    """The number the keyboard answers a custom value on, read from its own enum.

    The same reasoning as the keycodes below: the firmware decides the number and the
    definition can only repeat it, so it is taken from the header rather than written down
    twice and hoped about.
    """
    declaration = re.search(r"enum\s+k100_via_value\s*\{(.*?)\}", header.read_text(), re.S)
    if declaration is None:
        raise SystemExit(f"{header} declares no enum k100_via_value")

    declared = re.search(rf"^\s*{name}\s*=\s*(\d+)", declaration.group(1), re.M)
    if declared is None:
        raise SystemExit(f"{header}'s enum k100_via_value does not declare {name} with a "
                         f"number of its own")
    return int(declared.group(1))


def diagnostics_group(probe_value: int, index_value: int, leds: int) -> dict:
    """The LED probe: light one LED by index, and see what lights.

    Which index lights which LED is not settled on this keyboard. The channel table comes
    from Corsair's own firmware and the positions from ckb-next, and the two number the same
    hardware differently -- the right light bar is eleven real LEDs that no effect reaches,
    because they sit where this layout keeps the entries it cannot place. Finding out what
    sits where needs a way to light one and look at it, and that is what this is.

    The slider appears only while the probe is on, which is `showIf` pointed at this
    keyboard's own value rather than at the lighting's.
    """
    return {
        "label": "Diagnostics",
        "content": [
            {"label": "LED probe", "type": "toggle",
             "content": [f"{K100_VALUE}led_probe", K100_CHANNEL, probe_value]},
            {"label": "LED index", "type": "range", "options": [0, leds - 1],
             "showIf": f"{{{K100_VALUE}led_probe}} == 1",
             "content": [f"{K100_VALUE}led_index", K100_CHANNEL, index_value]},
        ],
    }


def firmware_menu(version_value: int, probe_value: int, index_value: int,
                  leds: int) -> dict:
    """A menu that shows which firmware is on the keyboard, asked of the keyboard.

    VIA displays no firmware version anywhere. It reads one, and uses it only to hide menu
    entries a firmware is too old for. What it does display is a `label` item bound to a
    value, which it renders as the text the keyboard returns -- so the version shown here is
    one the keyboard was asked for, not a number this file claims. A definition loaded against
    a different build says so by showing that build's version. See via_values.c.
    """
    return {
        "label": "Firmware",
        "content": [
            {
                "label": "Installed",
                "content": [
                    {"label": "Version", "type": "label",
                     "content": [f"{K100_VALUE}version", K100_CHANNEL, version_value]},
                ],
            },
            diagnostics_group(probe_value, index_value, leds),
        ],
    }


def custom_keycodes(header: Path) -> list[dict]:
    """VIA's `customKeycodes`, in the order the keyboard's enum declares them.

    The enum is the source of truth for the order because it is the source of truth for the
    keycodes: the first enumerator *is* `CUSTOM(0)`. A keycode the header declares and
    CUSTOM_KEYCODE_LABELS does not is an error rather than an unnamed entry in the app, and so
    is a label for a keycode no longer declared, which would shift every entry after it.
    """
    declaration = re.search(r"enum\s+k100_keycodes\s*\{(.*?)\}", header.read_text(), re.S)
    if declaration is None:
        raise SystemExit(f"{header} declares no enum k100_keycodes")

    declared = re.findall(r"^\s*(K100_\w+)\s*(?:=|,)", declaration.group(1), re.M)
    if not declared:
        raise SystemExit(f"{header}'s enum k100_keycodes declares nothing")

    unlabelled = [name for name in declared if name not in CUSTOM_KEYCODE_LABELS]
    if unlabelled:
        raise SystemExit(f"no VIA label for: {', '.join(unlabelled)}")

    stale = sorted(set(CUSTOM_KEYCODE_LABELS) - set(declared))
    if stale:
        raise SystemExit(f"labelled but not declared in {header.name}: {', '.join(stale)}")

    return [CUSTOM_KEYCODE_LABELS[name] for name in declared]


def build_via_json(placed: list[Key], keycodes: list[dict], values: dict[str, int],
                   leds: int) -> dict:
    """VIA v3 definition. The keymap is KLE, emitted with explicit per-row offsets."""
    # By the top edge, not the centre: a key two units tall shares its top with
    # the row it starts in and its centre with nothing, and grouping by the
    # centre puts the ISO enter in a row of its own.
    rows: dict[float, list[Key]] = {}
    for key in placed:
        rows.setdefault(key.top / UNIT, []).append(key)

    kle: list = []
    previous_y = None
    for row_y in sorted(rows):
        segment: list = []
        if previous_y is not None and abs(row_y - previous_y) > 0.001:
            segment.append({"y": round(row_y - previous_y - 1, 3)})
        previous_y = row_y

        cursor = 0.0
        for key in sorted(rows[row_y], key=lambda k: k.left):
            shape = L_SHAPED.get(key.name)
            props = {}
            gap = key.left / UNIT + (shape["indent"] if shape else 0.0) - cursor
            if abs(gap) > 0.001:
                props["x"] = round(gap, 3)
            if abs(key.width / UNIT - 1) > 0.001:
                props["w"] = round(key.width / UNIT, 3)
            if abs(key.height / UNIT - 1) > 0.001:
                props["h"] = round(key.height / UNIT, 3)
            if shape:
                props.update(shape["props"])
            if props:
                segment.append(props)
            segment.append(f"{key.matrix[0]},{key.matrix[1]}")
            # Past the whole key, overhang included, not past its own rectangle.
            cursor = (key.left + key.width) / UNIT
        kle.append(segment)

    return {
        "name": "Corsair K100 RGB (OPX, ISO DE)",
        "vendorId": "0x1B1C",
        "productId": "0x1B7C",
        "matrix": {"rows": STROBES, "cols": SENSES},
        "layouts": {"keymap": kle},
        "customKeycodes": keycodes,
        "menus": [lighting_menu(), firmware_menu(values["K100_VIA_VERSION"],
                                                 values["K100_VIA_LED_PROBE"],
                                                 values["K100_VIA_LED_INDEX"],
                                                 leds)],
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


#: Where the keyboard lives. Both an output directory and, for the keycode enum, an input.
KEYBOARD_DIR = Path("qmk/keyboards/corsair/k100")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--firmware", type=Path, required=True,
                        help="an official firmware image, from tools/fetch-firmware.py")
    parser.add_argument("--geometry", type=Path,
                        default=Path("tools/extract-ckb-keymap/k100-de.json"))
    parser.add_argument("--ckb", type=Path, required=True, help="a ckb-next checkout")
    parser.add_argument("--layout", default="iso")
    parser.add_argument("--out", type=Path, default=KEYBOARD_DIR)
    parser.add_argument("--keycodes", type=Path, default=KEYBOARD_DIR / "k100.h",
                        help="the header declaring enum k100_keycodes, read for VIA's labels")
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
    keycodes = custom_keycodes(args.keycodes)
    values = {name: via_value(args.keycodes, name) for name in
              ("K100_VIA_VERSION", "K100_VIA_LED_PROBE", "K100_VIA_LED_INDEX")}

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
    args.via_out.write_text(
        json.dumps(build_via_json(placed, keycodes, values, len(leds)), indent=2) + "\n")

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
