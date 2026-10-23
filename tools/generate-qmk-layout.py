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
# G1-G6 are the first six of VIA's macros, which is what the keys are for: six keys with
# nothing printed on them but a number, beside an application that records macros and stores
# them on the keyboard. They sent F13-F18 before, the conventional stand-in for a macro key,
# which only stood in -- nothing was listening, and the keyboard had no way to say what G3
# should do. `MC_0` is the macro VIA calls M0, so G1 is its first row and G6 its sixth. A
# macro that has not been recorded is empty, and an empty one does nothing when the key is
# pressed.
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
# thing, and nothing for the wheel. It now steps the lighting effect; see
# qmk/keyboards/corsair/k100/lighting_keys.c.
NON_HID_KEYCODES = {
    "g1": "MC_0", "g2": "MC_1", "g3": "MC_2",
    "g4": "MC_3", "g5": "MC_4", "g6": "MC_5",
    "fn": "MO(1)",
    "profswitch": "K100_LIGHTS_TOGGLE", "lock": "GU_TOGG",
    "mute": "KC_MUTE", "ctrlwheelb": "K100_RGB_EFFECT",
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
    "K100_RGB_EFFECT": {
        "name": "LED Effect",
        "title": "Steps the lighting through the effects this firmware has",
        "shortName": "Effect",
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

# Keys the Fn layer does not pass through, by the name the geometry knows them by.
#
# Names rather than matrix positions, because a pair of numbers says nothing about which key
# it is and the generator already knows where each name sits. A name no key has stops the
# build; see build_keymap_c().
#
# **Fn+Esc has to reach QK_BOOT.** QMK does not speak Bragi, so once this firmware runs there
# is no vendor interface left to flash the keyboard through -- the keymap is one of the three
# ways back to Corsair's bootloader, and the only one a user can reach deliberately while the
# keyboard is working. See qmk/keyboards/corsair/k100/bootloader.c.
#
# **Fn+Backspace** clears the settings QMK keeps in the part's EEPROM. Those outlive a
# reflash, which is the point of keeping them and also means a changed default here reaches a
# keyboard that has never had settings written, and no other.
#
# **Fn+N** turns NKRO off and on. This firmware reports all keys at once, which is what the
# keyboard is for, and a host that cannot read that reads nothing at all -- a BIOS or a boot
# menu, where there is no application to fix it with and the keyboard is the only way in.
#
# **Fn+Left and Fn+Right** set the effect speed, which is the one lighting value with no
# control of its own on the keyboard: the wheel sets the brightness, the button beside it steps
# the effect, and the key beside that turns the lighting off.
FN_OVERRIDES = {
    "esc": "QK_BOOT",
    "bspace": "EE_CLR",
    "n": "NK_TOGG",
    "left": "RM_SPDD",
    "right": "RM_SPDU",
}

# The RGB effects this firmware is built with, in the order QMK numbers them.
#
# The order is not ours to pick. QMK numbers the effects it compiles in the order its own
# rgb_matrix_effects.inc includes them, starting at 1, and that number is what VIA's menu
# sends. Checked against the built firmware rather than assumed.
#
# Effects that read a position were left out while the LED positions were not known to agree
# with the LEDs they are attached to. Every slot is measured now, so one that sweeps across
# the keyboard is built.
#
# What each one responds to decides which controls VIA offers alongside it. `colour` is
# whether the effect reads the stored hue and saturation at all, and only the effect that
# *is* a colour does: QMK's own rainbows keep the saturation they are handed, which made a
# keyboard set to white paint white rainbows, so this keyboard has rainbows of its own that
# paint their hues fully saturated. One colour that reached across three effects was the
# fault; see rgb_matrix_kb.inc.
#
# `custom` marks the keyboard's own, in rgb_matrix_kb.inc, which QMK numbers after every
# built-in and enables through rules.mk rather than through keyboard.json -- so each is
# keyed by the name its own kind is known by: a built-in by the lowercase name
# keyboard.json takes, ours by the C name the effect is declared with. Only the plain
# colour is a built-in here, which is why the three below carry that mark.
ANIMATIONS = {
    "solid_color": {"label": "Solid Color", "colour": True, "speed": False},
    "K100_RAINBOW": {"label": "Rainbow", "colour": False, "speed": True, "custom": True},
    "K100_RAINBOW_SWEEP": {"label": "Rainbow Sweep", "colour": False, "speed": True,
                           "custom": True},
    "K100_LIGHTS_OFF": {"label": "Off", "colour": False, "speed": False, "custom": True},
    "K100_PER_KEY": {"label": "Per Key", "colour": False, "speed": False, "custom": True},
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

# What each of the firmware's 190 LED slots lights.
#
# Measured, one slot at a time, with the LED probe in VIA's Diagnostics menu: each index was
# lit on its own and what came up was written down. Nothing here is derived, and nothing in
# this project could have derived it -- the order is Corsair's wiring and agrees with neither
# the key matrix nor the way the keys are laid out. Three derivations were tried before this
# was measured and all three were wrong.
#
# `None` is a slot with nothing fitted, which still takes its number: 14 is the space between
# F12 and Print. A name twice over is a light with two LEDs, which the three buttons along the
# top and the wheel's centre have.
#
# Worth knowing before changing any of it:
#
#   * The keys fill 0 to 119 in six rows of 22, each row starting with a macro key, crossing
#     the main block and then taking that row's share of the navigation and number pads.
#   * The media keys sit in the top row, where they are printed, rather than with the other
#     keys of their strobe.
#   * The number pad's 1, 2, 3 and its enter key come before its 4, 5, 6, all in the row that
#     carries A to Ä.
#   * The top bar is not in order: its twelfth to fifteenth segments are at 150, 149, 152 and
#     151, in that sequence, between its eleventh at 141 and its sixteenth at 142.
#   * 173 to 178 are the six single-colour status icons. They are left unplaced on purpose --
#     indicators.c drives them by index, and no effect should reach them.
#
# One thing is still open, and it is narrower than it looks: in which order the eleven
# segments of each side bar run. Everything else is settled. The ring is, because ckb-next
# names its segments by angle -- 22.5 degrees through 337.5, clockwise from the top -- and the
# probe found 120 at the top and 127 to the left of it, which is the same way round. The top
# bar is, because its order was walked segment by segment, out-of-sequence ones and all.
#
# No effect this firmware has can show a side bar in the wrong order, which is worth knowing
# before anyone spends an evening on it: every segment of one carries the same x, so a sweep
# across the keyboard lights the whole bar together whichever end 157 and 179 are. It would
# take an effect that reads y, and settling it takes the probe and someone looking.
LED_SLOTS = (
    "g1", "esc", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",
    None, "prtscn", "scroll", "pause", "stop", "prev", "play", "next",

    "g2", "grave", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "minus", "equal",
    "bspace", "ins", "home", "pgup", "numlock", "numslash", "numstar", "numminus",

    "g3", "tab", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "lbrace", "rbrace",
    None, "del", "end", "pgdn", "num7", "num8", "num9", "numplus",

    "g4", "caps", "a", "s", "d", "f", "g", "h", "j", "k", "l", "colon", "quote", "hash",
    "enter", "num1", "num2", "num3", "numenter", "num4", "num5", "num6",

    "g5", "lshift", "bslash_iso", "z", "x", "c", "v", "b", "n", "m", "comma", "dot", "slash",
    None, "rshift", "up", "left", "down", "right", "num0", "numdot",

    "g6", "lctrl", "lwin", "lalt", None, "space", None, "ralt", "fn", "rmenu", "rctrl",

    # The wheel: eight around its rim, clockwise from the top, then two in its centre, which
    # are the button's own light.
    "ctrlwheel1", "ctrlwheel2", "ctrlwheel3", "ctrlwheel4",
    "ctrlwheel5", "ctrlwheel6", "ctrlwheel7", "ctrlwheel8",
    "ctrlwheelb", "ctrlwheelb",

    None,

    # The top bar, left to right, with the four out of sequence in the middle.
    "topbar1", "topbar2", "topbar3", "topbar4", "topbar5", "topbar6", "topbar7", "topbar8",
    "topbar9", "topbar10", "topbar11",
    "topbar16", "topbar17", "topbar18", "topbar19", "topbar20", "topbar21", "topbar22",
    "topbar13", "topbar12", "topbar15", "topbar14",

    # The two buttons beside the wheel, two LEDs each.
    "profswitch", "profswitch", "lock", "lock",

    "leftbar1", "leftbar2", "leftbar3", "leftbar4", "leftbar5", "leftbar6",
    "leftbar7", "leftbar8", "leftbar9", "leftbar10", "leftbar11",

    "mute", "mute",

    "logoleft", "logo", "logoright",

    # The status icons, driven by index from indicators.c.
    None, None, None, None, None, None,

    "rightbar1", "rightbar2", "rightbar3", "rightbar4", "rightbar5", "rightbar6",
    "rightbar7", "rightbar8", "rightbar9", "rightbar10", "rightbar11",
)

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
# QMK encodes it as BCD: 0.2.1 becomes 0x0021. Corsair's container carries only the minor and
# the patch, so the leading number stays where it is whatever this says.
FIRMWARE_VERSION = "0.2.1"

# One key unit in ckb-next's 16ths-of-an-inch grid.
UNIT = 12

# Past the bottom of the board, so that slots with no position sort after everything that has
# one rather than among the keys at the top edge.
UNREADABLE = 1 << 20

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


@dataclass(frozen=True)
class Slot:
    """One LED the firmware has: what drives it, and what the measurement found behind it."""

    led: Led
    key: "Key | None"

    @property
    def index(self) -> int:
        return self.led.index

    @property
    def label(self) -> str:
        """What the probe calls it. An empty slot and a status icon are not the same thing:
        the icons have one channel instead of three, which the firmware's own table says."""
        if self.key:
            return self.key.name
        return "empty" if self.led.is_rgb else "status icon"


def led_order(lit: list[Key], leds: list[Led]) -> list[Slot]:
    """What each LED index belongs to, out of the measured table.

    One place, because two things need it and they must agree: the table QMK is built with,
    and the list the LED probe offers in VIA. A probe that offered a different order from the
    one under test would prove nothing at all.

    Everything here is a lookup. What is worth having is the three ways it refuses: a name the
    geometry does not know, a key with no slot at all, and a slot count that is not the
    firmware's. Each of those used to be a silent shift of every LED after it.
    """
    if len(LED_SLOTS) != len(leds):
        raise SystemExit(f"the LED table has {len(LED_SLOTS)} slots and the firmware's map "
                         f"has {len(leds)}")

    by_name = {key.name: key for key in lit}
    named = {name for name in LED_SLOTS if name}

    unknown = sorted(named - set(by_name))
    if unknown:
        raise SystemExit(f"the geometry has no {', '.join(unknown)}, which the LED table names")

    dark = sorted({key.name for key in lit if key.matrix} - named)
    if dark:
        raise SystemExit(f"keys the LED table gives no slot: {', '.join(dark)}")

    return [Slot(led, by_name[name] if name else None)
            for led, name in zip(leds, LED_SLOTS)]


def rgb_layout(slots: list[Slot], to_qmk) -> list[dict]:
    """The lighting table: one entry per LED the firmware has, in the firmware's order.

    Every index comes from LED_SLOTS, which was measured on the keyboard. The table is as long
    as the firmware's own LED map, because QMK addresses nothing past its end and the keyboard
    lights some things ckb-next's geometry never places. Stopping at what has coordinates
    instead would make the length a number someone chose, and a chosen length drifts.

    A slot with a key behind it carries that key's matrix position and a keylight flag, so
    per-key effects and the reactive ones find it. A slot with a light carries the underglow
    flag. A slot with nothing carries no flags at all: no effect writes it, so the position it
    is given is never read.

    Two rules here are checks rather than corrections, and both guard something the firmware
    reads this table to find out.

    The single-colour rule is the first. is31fl3743b_leds.c is indexed
    by the firmware's LED index, and six of those LEDs have one channel instead of three --
    the status icons, which are on or off and mean whatever the keyboard is reporting, so
    colouring one is meaningless. The measurement already leaves exactly those six unplaced,
    and if it ever stopped doing so the two would have to be reconciled by hand rather than by
    quietly clearing a flag here.
    """
    coloured = [slot for slot in slots if slot.key and not slot.led.is_rgb]
    if coloured:
        raise SystemExit("single-channel LEDs the measured table places: " + ", ".join(
            f"{slot.index} ({slot.key.name})" for slot in coloured))

    def entry(key: Key) -> dict:
        x, y = to_qmk(key)
        if key.matrix:
            return {"matrix": list(key.matrix), "x": x, "y": y, "flags": LED_FLAG_KEYLIGHT}
        return {"x": x, "y": y, "flags": LED_FLAG_UNDERGLOW}

    unplaced_x, unplaced_y = UNPLACED_POSITION
    table = [entry(slot.key) if slot.key else
             {"x": unplaced_x, "y": unplaced_y, "flags": LED_FLAG_NONE}
             for slot in slots]

    # Two key LEDs at one position have to be two lights under one key, because that is how
    # the firmware finds them: VIA's key painter writes one index and per_key_colour.c gives
    # the colour to every other LED at the same place. Two different keys landing on one
    # position would quietly take each other's colour, which is what this refuses.
    together: dict[tuple[int, int], list[int]] = {}
    for index, slot in enumerate(table):
        if slot["flags"] == LED_FLAG_KEYLIGHT:
            together.setdefault((slot["x"], slot["y"]), []).append(index)

    confused = {at: shared for at, shared in together.items()
                if len({tuple(table[i]["matrix"]) for i in shared}) > 1}
    if confused:
        raise SystemExit("LEDs of different keys share a position: " + "; ".join(
            f"{at} has {', '.join(str(i) for i in shared)}"
            for at, shared in sorted(confused.items())))

    return table


def build_keyboard_json(placed: list[Key], all_keys: list[Key], slots: list[Slot]) -> dict:
    to_qmk = scale(all_keys)

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
            # QMK's own effects that this firmware compiles, which is one of
            # the 45 it has. An animation a definition offers and nothing
            # drives is a claim rather than a feature. The keyboard's three are
            # not named here -- QMK takes those through rules.mk and the schema
            # knows only its own -- so this is ANIMATIONS without them; see
            # there for what each is for, and rgb_matrix_kb.inc for the three.
            "animations": {name: True for name, animation in ANIMATIONS.items()
                           if not animation.get("custom")},
            # White, full brightness. A stored default survives a reflash, so
            # changing this means moving EECONFIG_MAGIC_NUMBER too; that note
            # lives in the keyboard's config.h, beside the number.
            "default": {"animation": "solid_color", "hue": 0, "sat": 0, "val": 255},
            "layout": rgb_layout(slots, to_qmk),
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

    unknown = sorted(set(FN_OVERRIDES) - {key.name for key in placed})
    if unknown:
        raise SystemExit(f"the Fn layer names {', '.join(unknown)}, which no key is")

    width = max(len(keycode_for(k)) for k in placed)

    def layer(transparent: bool) -> str:
        lines = []
        for strobe in sorted(rows):
            ordered = sorted(rows[strobe], key=lambda k: k.matrix[1])
            codes = [FN_OVERRIDES.get(k.name, "KC_TRNS") if transparent
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

    // The Fn layer passes everything through except five keys, and what each of the five
    // is for is in FN_OVERRIDES in the generator. In short:
    //
    //   Fn+Esc        the way back to Corsair's bootloader; see bootloader.c for why a
    //                 keyboard that cannot be reflashed is what this guards against.
    //   Fn+Backspace  clears the settings QMK keeps in the part's EEPROM, which is the only
    //                 way back to the defaults compiled in here once something has been
    //                 stored over them -- they outlive a reflash.
    //   Fn+N          all keys at once, or the six a host that cannot read that understands.
    //   Fn+Left/Right the effect speed, which has no control of its own on the keyboard.
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


# How bright a painted key is, and the one way to turn a single key off.
#
# A dropdown rather than a range, for the reason the LED probe has one: VIA draws a range with
# no number beside it, and "how bright is the brush" is a question a control has to be able to
# answer. These are the steps, as the byte each sends -- 255 is what an unpainted key already
# is, and 0 is the off this exists for.
PAINT_VALUES = [("Full", 255), ("Bright", 192), ("Half", 128), ("Dim", 64), ("Off", 0)]


def lighting_menu(per_key_value: int, paint_value: int) -> dict:
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

    The palette is the second group, and it is what turns VIA's key painter on: a control of
    type `color-palette` draws the palette beside the keyboard and makes the keys paintable
    while that pane is open. It is bound to the value the application sends a painted colour
    on, which is channel zero, value one -- VIA's number, not this keyboard's, which is why
    the keyboard's own values start at two. Only the keys can be painted, because only keys
    are drawn; the bars, the logo and the wheel's ring have no key to click. See
    per_key_colour.c.

    Beside it is the one thing the palette cannot send: how bright a painted key is. The
    application's palette carries a hue and a saturation, and neither can say "off" -- zero
    saturation is white. So the brightness belongs to the brush: it is picked here and every
    key painted afterwards takes it, with `Off` at the bottom of the same control, because a
    key that is off is a key whose brightness is zero.
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
        }, {
            "label": "Per Key",
            "content": [
                {"label": "Paint", "type": "color-palette",
                 "content": [f"{K100_VALUE}per_key_colour", K100_CHANNEL, per_key_value]},
                {"label": "Brightness", "type": "dropdown",
                 "options": [[label, value] for label, value in PAINT_VALUES],
                 "content": [f"{K100_VALUE}paint_value", K100_CHANNEL, paint_value]},
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


def reading_order(slot: Slot) -> tuple:
    """Across the keyboard, keys before the lights around them, empty slots last.

    Three groups rather than one, because the bars run down the edges: sorted by position
    alone they land between the key rows, and a list meant for walking the keys would hop to a
    bar and back on every row.

    The lights are then kept bar by bar, by their own names, for the same reason one step
    further in. Sorting those by position alternates between the left bar, the right bar and
    the top one, because that is how they are arranged -- and a bar is measured by walking it
    end to end, not by hopping around the edge of the board.
    """
    if slot.key is None:
        return (2, (UNREADABLE,), slot.index)
    if slot.key.matrix:
        return (0, (slot.key.top, slot.key.left), slot.index)

    strip = re.match(r"(\D+)(\d*)", slot.key.name)
    return (1, (strip.group(1), int(strip.group(2) or 0)), slot.index)


def diagnostics_group(probe_value: int, index_value: int, slots: list[Slot]) -> dict:
    """The LED probe: light one LED by index, and see what lights.

    This is the instrument LED_SLOTS was measured with, and it is what any claim about the
    order has to be made with. The channel table comes from Corsair's firmware and the
    positions from ckb-next, and the two number the same hardware differently, so no amount of
    reading either settles which index lights what. Lighting one and looking at it does.

    A list rather than a slider, because VIA draws a slider with no number on it -- and an
    instrument with no scale measures nothing. Each entry carries what the table says is
    there and the index it sends, so picking one is a prediction and the keyboard answers
    it: choose `f11` and either F11 lights or something else does.

    **Ordered across the keyboard, not by index.** The index is Corsair's wiring and jumps
    about -- the number pad's 1, 2, 3 come before its 4, 5, 6, and the up arrow is nowhere
    near the other three. Stepping down a list in that order tells you nothing, because every
    jump looks like a mistake. In reading order the light walks the keyboard, so a jump *is*
    one, and the whole table can be checked by holding the down arrow.

    The list appears only while the probe is on, which is `showIf` pointed at this keyboard's
    own value rather than at the lighting's.
    """
    return {
        "label": "Diagnostics",
        "content": [
            {"label": "LED probe", "type": "toggle",
             "content": [f"{K100_VALUE}led_probe", K100_CHANNEL, probe_value]},
            {"label": "LED index", "type": "dropdown",
             "options": [[f"{slot.label}  ({slot.index})", slot.index]
                         for slot in sorted(slots, key=reading_order)],
             "showIf": f"{{{K100_VALUE}led_probe}} == 1",
             "content": [f"{K100_VALUE}led_index", K100_CHANNEL, index_value]},
        ],
    }


def firmware_menu(version_value: int, probe_value: int, index_value: int,
                  slots: list[Slot]) -> dict:
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
            diagnostics_group(probe_value, index_value, slots),
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
                   slots: list[Slot]) -> dict:
    """VIA v3 definition. The keymap is KLE, emitted with explicit per-row offsets."""
    # Which LED sits under each key, which is what turns the key painter on: VIA
    # asks for per-key colours only if a key names an LED, and then for every
    # index from zero to the largest one named. The first of the two for a key
    # that has two -- the application paints one index, and the firmware gives
    # the colour to the key's other light; see per_key_colour.c.
    under: dict[str, int] = {}
    for slot in slots:
        if slot.key is not None:
            under.setdefault(slot.key.name, slot.index)

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
            # Row and column on the first line of the legend and the LED index on the
            # second, which is where VIA's reader looks for each: the labels are mapped
            # through its own alignment table, where line 0 is the matrix position and line
            # 1 is `l` and a number. Not a property on the key -- the schema takes no such
            # property, and the index would be dropped on the way in.
            segment.append(f"{key.matrix[0]},{key.matrix[1]}\nl{under[key.name]}")
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
        "menus": [lighting_menu(values["K100_VIA_PER_KEY_COLOUR"],
                                values["K100_VIA_PAINT_VALUE"]),
                  firmware_menu(values["K100_VIA_VERSION"],
                                values["K100_VIA_LED_PROBE"],
                                values["K100_VIA_LED_INDEX"],
                                slots)],
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
              ("K100_VIA_PER_KEY_COLOUR", "K100_VIA_VERSION",
               "K100_VIA_LED_PROBE", "K100_VIA_LED_INDEX",
               "K100_VIA_PAINT_VALUE")}

    # Everything that scans but did not land in the matrix must be an encoder, or the join
    # has silently dropped a key.
    expected_absent = {name for _, ccw, cw, *_ in ENCODERS for name in (ccw, cw)}
    absent = {n for n, k in keys.items() if k.has_scan and k.matrix is None}
    if absent != expected_absent:
        raise SystemExit(
            f"keys that scan but are not in the matrix: {', '.join(sorted(absent))}; "
            f"expected exactly the encoder rotations {', '.join(sorted(expected_absent))}"
        )

    slots = led_order([k for k in keys.values() if k.has_led], leds)

    keyboard = build_keyboard_json(placed, list(keys.values()), slots)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "keyboard.json").write_text(json.dumps(keyboard, indent=2) + "\n")

    keymap_dir = args.out / "keymaps" / "default"
    keymap_dir.mkdir(parents=True, exist_ok=True)
    (keymap_dir / "keymap.c").write_text(build_keymap_c(placed))

    args.via_out.parent.mkdir(parents=True, exist_ok=True)
    args.via_out.write_text(
        json.dumps(build_via_json(placed, keycodes, values, slots), indent=2) + "\n")

    entries = keyboard["rgb_matrix"]["layout"]
    print(f"matrix      {STROBES} x {SENSES} electrical, {len(placed)} positions")
    print(f"encoders    {len(ENCODERS)} (rotations are not matrix-scanned)")
    print(f"rgb matrix  {len(entries)} LEDs in the firmware's map "
          f"({sum(1 for e in entries if e['flags'] == LED_FLAG_KEYLIGHT)} on keys, "
          f"{sum(1 for e in entries if e['flags'] == LED_FLAG_UNDERGLOW)} on lights, "
          f"{sum(1 for e in entries if e['flags'] == LED_FLAG_NONE)} unplaced)")
    print(f"written     {args.out / 'keyboard.json'}")
    print(f"            {keymap_dir / 'keymap.c'}")
    print(f"            {args.via_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
