"""Everything the keyboard keeps because somebody chose it, read and written as a whole.

Four separate things live in the keyboard's 2 KB of EEPROM, and until now each was
reachable on its own and none of them together:

    the keymap      every layer, as VIA stores it, which is what the firmware reads
    the macros      one buffer, packed one after another
    the colours     one per LED, with its own brightness
    the reaction    what the reactive effects do, which is a handful of bytes

The one thing that clears all four is a change to the *shape* of what is stored --
`EECONFIG_MAGIC_NUMBER` moving, deliberately, because something new needed room. That is
survivable precisely once per keyboard if it can be read out first and written back after,
which is what this is for. `tools/k100-backup.py` is the command; this is where the knowing
lives, so that the backup and `via-per-key.py` cannot disagree about what is stored or where.

Nothing here invents a number. The values this keyboard answers on are read out of the
generated VIA definition, which is the record of what the firmware chose; the two VIA fixed
itself -- the key painter's colour on channel zero, value one -- are written down, because
there is nothing to read them from.
"""

from __future__ import annotations

import json
from pathlib import Path

from k100_via import CUSTOM_GET_VALUE, CUSTOM_SET_VALUE, exchange

#: What one report can carry of a buffer, which VIA's protocol fixes: the command, the two
#: offset bytes and the length take four of the 32.
CHUNK = 28

LAYER_COUNT = 0x11
KEYMAP_GET_BUFFER = 0x12
KEYMAP_SET_BUFFER = 0x13

MACRO_GET_COUNT = 0x0C
MACRO_GET_BUFFER_SIZE = 0x0D
MACRO_GET_BUFFER = 0x0E
MACRO_SET_BUFFER = 0x0F

#: VIA's own channel for a keyboard's values, and the value its key painter uses on it.
#: Neither is this keyboard's to choose -- the application has the number 1 built in, which is
#: why the keyboard's own values start at 2. See qmk/keyboards/corsair/k100/lighting_store.c.
K100_CHANNEL = 0
PER_KEY_COLOUR = 1

#: A byte the application always sends after the index. Nothing in the firmware reads it; it
#: is here because the reports have to match.
ONE = 1

#: Every LED the firmware stores a colour for, which is every LED it has.
#: K100_PER_KEY_LEDS in k100.h.
LEDS = 190

#: The definition the application loads, which is also the record of which value each of this
#: keyboard's own settings is sent on.
DEFINITION = Path(__file__).parent.parent / "via" / "k100-iso-de.json"

#: The keyboard's own settings, by the name the definition knows each by. A hue and a
#: saturation arrive together, which is why the colour counts as one.
#:
#: Each is one byte wide unless it is a colour. A name the definition does not declare is left
#: out rather than guessed at, so a backup taken against an older definition is still a
#: backup of what that firmware had.
SETTINGS = ("reaction_source", "reaction_colour", "buttons_react", "lights_react",
            "sleep_after")


def menu_value(wanted: str) -> tuple[int, int] | None:
    """The channel and value a named control is sent on, out of the definition."""

    def walk(item):
        if isinstance(item, dict):
            content = item.get("content")
            if isinstance(content, list) and content and content[0] == wanted:
                return (content[1], content[2])
            return walk(content)
        if isinstance(item, list):
            for each in item:
                found = walk(each)
                if found:
                    return found
        return None

    return walk(json.loads(DEFINITION.read_text()).get("menus", []))


class Refused(Exception):
    """The keyboard did not answer, or answered something else. Never guessed around."""


def ask(node: Path, request: bytes) -> bytes:
    """One command and its reply, where a reply that does not echo the command is a failure.

    VIA's replies repeat the command they answer, so a reader that does not check this is one
    answer behind for the rest of the session the moment anything is dropped -- which is a
    fault this keyboard has actually had; see docs/usb-bringup.md.
    """
    reply = exchange(node, request)
    if reply is None:
        raise Refused(f"the keyboard did not answer {request[0]:#04x}")
    if reply[: len(request)] != request:
        raise Refused(f"the keyboard answered {request[0]:#04x} with "
                      f"{reply[:len(request)].hex(' ')}")
    return reply


def read_buffer(node: Path, command: int, size: int) -> bytes:
    """One of the keyboard's buffers, a report at a time."""
    buffer = bytearray()
    while len(buffer) < size:
        want = min(CHUNK, size - len(buffer))
        offset = len(buffer)
        reply = ask(node, bytes([command, offset >> 8, offset & 0xFF, want]))
        buffer += reply[4:4 + want]
    return bytes(buffer)


def write_buffer(node: Path, command: int, payload: bytes) -> None:
    for at in range(0, len(payload), CHUNK):
        chunk = payload[at:at + CHUNK]
        ask(node, bytes([command, at >> 8, at & 0xFF, len(chunk)]) + chunk)


def layers(node: Path) -> int:
    return ask(node, bytes([LAYER_COUNT]))[1]


def read_keymap(node: Path, rows: int, cols: int) -> bytes:
    """Every layer's keycodes, as the buffer the firmware itself reads them from.

    Two bytes per position, so the whole of it is layers by rows by columns by two -- 1152
    bytes for this keyboard's four layers and eight by eighteen matrix. Taken as a buffer
    rather than a position at a time because it is one: 42 reports instead of 576.
    """
    return read_buffer(node, KEYMAP_GET_BUFFER, layers(node) * rows * cols * 2)


def write_keymap(node: Path, keymap: bytes) -> None:
    write_buffer(node, KEYMAP_SET_BUFFER, keymap)


def macro_buffer_size(node: Path) -> int:
    """How many bytes this keyboard has left for macros, which it works out for itself.

    Not a constant: it is whatever the 2 KB EEPROM has left once the dynamic keymap and this
    keyboard's own stored lighting are in it, so growing either shrinks this. That has
    happened -- deliberately, to give the lighting room -- and it is why the macros are fitted
    to the buffer on the way back rather than written at the length they were saved.
    """
    return int.from_bytes(ask(node, bytes([MACRO_GET_BUFFER_SIZE]))[1:3], "big")


def read_macros(node: Path) -> bytes:
    return read_buffer(node, MACRO_GET_BUFFER, macro_buffer_size(node))


def macro_content(buffer: bytes) -> bytes:
    """What a macro buffer actually holds, as opposed to how long it is.

    The macros are packed from the front and every macro ends in a NUL, so the trailing
    emptiness is free space rather than data. Comparing the content instead of the whole
    buffer is what lets a backup be checked across a change in the buffer's *size*: 269 bytes
    of nothing and 237 bytes of nothing hold the same macros, and a tool that called those
    different would be crying wolf about the one case it exists for.
    """
    return buffer.rstrip(b"\x00")


def fit_macros(saved: bytes, size: int) -> bytes:
    """The saved buffer as this keyboard's buffer, padded or trimmed, or a refusal.

    Trimmed only where there is nothing to trim. QMK skips bytes past the end of its buffer
    rather than writing them, so a buffer that is too long is written safely and silently
    short -- which is exactly how a restore would lose the last macro without saying a word.
    """
    content = macro_content(saved)
    if len(content) > size:
        raise Refused(f"the saved macros hold {len(content)} bytes and this keyboard's buffer "
                      f"is {size}; restoring would cut them off, so nothing was written")

    return saved[:size].ljust(size, b"\x00")


def write_macros(node: Path, macros: bytes) -> None:
    write_buffer(node, MACRO_SET_BUFFER, macros)


def read_colours(node: Path) -> list[list[int]]:
    """Every LED's stored colour: a hue, a saturation and a brightness each.

    The brightness is in the byte after the two the application asks for, which it never looks
    at. Without it a backup would carry two thirds of each colour, which is worse than none --
    it would look like a backup.
    """
    stored = []
    for led in range(LEDS):
        reply = ask(node, bytes([CUSTOM_GET_VALUE, K100_CHANNEL, PER_KEY_COLOUR, led, ONE]))
        stored.append([reply[5], reply[6], reply[7]])
    return stored


def write_colours(node: Path, stored: list[list[int]], paint_value: tuple[int, int]) -> None:
    """Puts them back, grouped by brightness.

    The brightness is the brush's and setting it is a command of its own, so this sets the
    brush once per distinct value instead of once per LED. See lighting_store.c for why the
    brightness is a property of the brush at all: the application's palette carries a hue and
    a saturation, and neither can say "off".
    """
    if len(stored) != LEDS:
        raise Refused(f"{len(stored)} colours, and this keyboard has {LEDS}")

    by_value: dict[int, list[int]] = {}
    for led, (_, _, val) in enumerate(stored):
        by_value.setdefault(val, []).append(led)

    for val, leds in sorted(by_value.items()):
        ask(node, bytes([CUSTOM_SET_VALUE, *paint_value, val]))
        for led in leds:
            hue, sat, _ = stored[led]
            ask(node, bytes([CUSTOM_SET_VALUE, K100_CHANNEL, PER_KEY_COLOUR, led, ONE,
                             hue, sat]))


def read_settings(node: Path) -> dict[str, list[int]]:
    """Every setting the keyboard answers for, by the names the definition gives them.

    A few bytes, and worth carrying for the same reason as the rest: they are choices somebody
    made, and a reshape takes them with everything else.
    """
    found = {}
    for name in SETTINGS:
        where = menu_value(f"id_k100_{name}")
        if where is None:
            continue
        reply = ask(node, bytes([CUSTOM_GET_VALUE, *where]))
        # Two bytes for the colour, one for the rest. The reply carries the whole report, so
        # the width has to come from somewhere: the colour is the one that is a colour.
        width = 2 if name.endswith("colour") else 1
        found[name] = list(reply[3:3 + width])
    return found


def write_settings(node: Path, settings: dict[str, list[int]]) -> None:
    """Puts them back, the colour first.

    Order matters by one step: setting the colour is what selects it as the source, so the
    source has to be written afterwards or a keyboard restored to "the painted colours" would
    come back set to one colour. See k100_reaction_colour_set() in lighting_store.c.
    """
    for name in sorted(settings, key=lambda name: name != "reaction_colour"):
        where = menu_value(f"id_k100_{name}")
        if where is None:
            raise Refused(f"this definition declares no id_k100_{name}")
        ask(node, bytes([CUSTOM_SET_VALUE, *where, *settings[name]]))
