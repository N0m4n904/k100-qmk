#!/usr/bin/env python3
"""Reads or writes one LED's own colour, the way VIA's key painter does.

The painter sends a colour per key on VIA's own channel zero, value one, and the
keyboard stores it. This drives the same two commands, so the path can be
checked without the application -- and so that what the application will show
can be read back before trusting it:

    python3 tools/via-per-key.py --led 0                  # what is stored
    python3 tools/via-per-key.py --led 0 --colour 170,255 # paint it blue

Hue and saturation are what the application sends. The brightness is the
keyboard's own addition -- the only way to say that one key is off, since zero
saturation is white -- and it belongs to the brush rather than to the stroke, so
`--brightness` sets what the *next* painted key gets:

    python3 tools/via-per-key.py --brightness 0 --led 0 --colour 0,0  # off
    python3 tools/via-per-key.py --brightness 255                     # brush back to full

Whole zones -- the light bars, the ring around the wheel, the logo -- are what the
application's painter cannot reach, because it paints keys. They have a colour control each,
and `--zone` drives those:

    python3 tools/via-per-key.py --zone leftbar --colour 85,255   # left bar green
    python3 tools/via-per-key.py --zone logo                      # what it is set to
    python3 tools/via-per-key.py --zone ctrlwheel --dim 128       # and how bright it is

A zone's brightness is its own and acts at once, unlike the brush: the colour control sets
the colour, the brightness control sets the brightness, and neither waits for the other.

Which number each zone is sent on is read out of the generated VIA definition rather than
written down here, so this cannot drift from what the application does.

Everything the keyboard has stored can be taken off it and put back, which is what makes a
change to the shape of that storage survivable -- it is the one thing that clears the colours:

    python3 tools/via-per-key.py --dump painted.json
    python3 tools/via-per-key.py --load painted.json

The dump carries the brightness as well, which the application cannot read: the firmware puts
it in the byte after the two VIA asks for.

The LED is the index in the firmware's own map -- the same number the LED probe
in VIA's Diagnostics menu uses, and the one the definition gives each key. A key
with two LEDs takes the colour on both, which the firmware does for itself, so
writing 128 and reading 129 is the way to see that happen.

Needs read and write on the hidraw node, so run it as root or install
udev/99-corsair-k100.rules.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_stored import (  # noqa: E402
    DEFINITION, K100_CHANNEL, LEDS, ONE, PER_KEY_COLOUR, Refused, menu_value, read_colours,
    write_colours,
)
from k100_usb import PRODUCT, VENDOR, device, identity  # noqa: E402
from k100_via import CUSTOM_GET_VALUE, CUSTOM_SET_VALUE, exchange, interface  # noqa: E402

#: The brush's brightness, which *is* one of this keyboard's own numbers --
#: `K100_VIA_PAINT_VALUE` in k100.h, where the generated definition reads it
#: from. Written down again here because a test tool that generated its own
#: definition would be testing itself.
PAINT_VALUE = 5


def colour(text: str) -> tuple[int, int]:
    hue, _, sat = text.partition(",")
    return (int(hue), int(sat))


def zone_value(family: str, what: str = "") -> tuple[int, int]:
    """The channel and value one of a zone's controls is sent on."""
    found = menu_value(f"id_k100_zone_{family}{what}")
    if found is None:
        raise SystemExit(f"{DEFINITION} has no zone called {family}")
    return found


#: What the reaction's source byte means, from `enum k100_reaction_source` in
#: lighting_store.c.
REACTION_PAINTED = 0
REACTION_CHOSEN = 1


def reaction(node: Path, wanted: str, buttons: str | None) -> int:
    """Reads or sets how the reactive effects behave.

    The colour is one option for both of its halves, because the firmware treats them as one:
    picking a colour is what selects it, so `--reaction 10,255` says everything and
    `--reaction painted` is the way back. Whether the buttons above the keys react is its own,
    because it is its own question.
    """
    source = menu_value("id_k100_reaction_source")
    chosen = menu_value("id_k100_reaction_colour")
    react = menu_value("id_k100_buttons_react")
    if source is None or chosen is None or react is None:
        raise SystemExit(f"{DEFINITION} declares no reaction settings; regenerate it with "
                         f"tools/generate-qmk-layout.py")

    if wanted == "painted":
        if exchange(node, bytes([CUSTOM_SET_VALUE, *source, REACTION_PAINTED])) is None:
            print("the reaction's source was not set", file=sys.stderr)
            return 1
    elif wanted:
        hue, sat = colour(wanted)
        if exchange(node, bytes([CUSTOM_SET_VALUE, *chosen, hue, sat])) is None:
            print("the reaction's colour was not set", file=sys.stderr)
            return 1

    if buttons is not None:
        if exchange(node, bytes([CUSTOM_SET_VALUE, *react,
                                 1 if buttons == "react" else 0])) is None:
            print("the buttons' setting was not set", file=sys.stderr)
            return 1

    source_reply = exchange(node, bytes([CUSTOM_GET_VALUE, *source]))
    colour_reply = exchange(node, bytes([CUSTOM_GET_VALUE, *chosen]))
    react_reply = exchange(node, bytes([CUSTOM_GET_VALUE, *react]))
    if source_reply is None or colour_reply is None or react_reply is None:
        print("the reaction was not read", file=sys.stderr)
        return 1

    if source_reply[3] == REACTION_PAINTED:
        print("reaction the painted colours")
    else:
        print(f"reaction one colour, hue {colour_reply[3]}, saturation {colour_reply[4]}")

    print(f"buttons  the four above the keys "
          f"{'react' if react_reply[3] else 'stay lit'}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--vid", default=VENDOR, help="USB vendor id, hex")
    parser.add_argument("--pid", default=PRODUCT, help="USB product id, hex")
    parser.add_argument("--led", type=int, help="which LED, by index")
    parser.add_argument("--colour", type=colour, help="hue,saturation, each 0 to 255")
    parser.add_argument("--brightness", type=int,
                        help="0 to 255, what the next painted key is given; 0 is off")
    parser.add_argument("--dump", type=Path, metavar="FILE",
                        help="read every stored colour into a file")
    parser.add_argument("--load", type=Path, metavar="FILE",
                        help="write a dumped file back to the keyboard")
    parser.add_argument("--dim", type=int, metavar="VALUE",
                        help="0 to 255, how bright the zone is; 0 is off")
    parser.add_argument("--zone", metavar="NAME",
                        help="a zone by its name in the definition: topbar, leftbar, "
                             "rightbar, ctrlwheel, logo")
    parser.add_argument("--all", type=int, metavar="N", nargs="?", const=LEDS,
                        help="read the first N LEDs and report the ones that carry a "
                             "colour, which is what a stored picture looks like from here")
    parser.add_argument("--reaction", nargs="?", const="", metavar="painted|HUE,SAT",
                        help="what the reactive effects light a key in: the painted colours, "
                             "or one colour, which picking selects. Reads it when given "
                             "nothing")
    parser.add_argument("--top-buttons", choices=("react", "lit"), dest="top_buttons",
                        help="whether the four buttons above the keys react to typing, or "
                             "stay lit through it")
    args = parser.parse_args()

    if all(each is None for each in (args.led, args.brightness, args.all, args.zone,
                                     args.dump, args.load, args.reaction,
                                     args.top_buttons)):
        raise SystemExit("--led, --zone, --brightness, --all, --dump, --load, --reaction, "
                         "--top-buttons, or a combination")

    entry = device(args.vid, args.pid)
    if entry is None:
        print(f"no {args.vid}:{args.pid} on this machine", file=sys.stderr)
        return 1
    version, product = identity(entry)
    print(f"running  bcdDevice {version}  {product}")

    found = interface(entry)
    if found is None:
        print("no interface answered VIA", file=sys.stderr)
        return 1
    node, protocol = found
    print(f"VIA on {node}, protocol {protocol:#06x}")

    if args.reaction is not None or args.top_buttons is not None:
        failed = reaction(node, args.reaction or "", args.top_buttons)
        if failed:
            return failed

    if args.brightness is not None:
        if exchange(node, bytes([CUSTOM_SET_VALUE, K100_CHANNEL, PAINT_VALUE,
                                 args.brightness])) is None:
            print("the brush was not set", file=sys.stderr)
            return 1
        brush = exchange(node, bytes([CUSTOM_GET_VALUE, K100_CHANNEL, PAINT_VALUE]))
        print(f"brush: brightness {brush[3] if brush else '?'}")

    # The colours alone, which is what this tool is for. tools/k100-backup.py carries these
    # and the keymap, the macros and the reaction's settings in one file, which is what to
    # reach for before an update; a file written there holds its colours under the same key,
    # so --load reads either.
    if args.dump is not None:
        try:
            stored = read_colours(node)
        except Refused as refusal:
            print(refusal, file=sys.stderr)
            return 1

        args.dump.write_text(json.dumps({"leds": stored}, indent=1) + "\n")
        lit = sum(1 for hue, sat, val in stored if (hue, sat) != (0, 0) or val != 255)
        print(f"dumped   {len(stored)} colours to {args.dump}, {lit} of them set to "
              f"something other than plain white")

    if args.load is not None:
        stored = json.loads(args.load.read_text())["leds"]
        try:
            write_colours(node, stored, (K100_CHANNEL, PAINT_VALUE))
        except Refused as refusal:
            print(f"{refusal}; the keyboard may be half written", file=sys.stderr)
            return 1

        brightnesses = len({val for _, _, val in stored})
        print(f"loaded   {len(stored)} colours from {args.load}, in "
              f"{brightnesses} brightness group(s)")

    if args.zone is not None:
        if args.dim is not None:
            channel, value = zone_value(args.zone, "_value")
            if exchange(node, bytes([CUSTOM_SET_VALUE, channel, value, args.dim])) is None:
                print("the zone's brightness was not set", file=sys.stderr)
                return 1

        channel, value = zone_value(args.zone, "_value")
        reply = exchange(node, bytes([CUSTOM_GET_VALUE, channel, value]))
        brightness = reply[3] if reply else "?"

        channel, value = zone_value(args.zone)

        if args.colour is not None:
            hue, sat = args.colour
            if exchange(node, bytes([CUSTOM_SET_VALUE, channel, value, hue, sat])) is None:
                print("the zone was not set", file=sys.stderr)
                return 1

        reply = exchange(node, bytes([CUSTOM_GET_VALUE, channel, value]))
        if reply is None:
            print("the zone was not read", file=sys.stderr)
            return 1
        print(f"zone {args.zone}: hue {reply[3]}, saturation {reply[4]}, "
              f"brightness {brightness}")

    if args.all is not None:
        painted = []
        for led in range(args.all):
            reply = exchange(node, bytes([CUSTOM_GET_VALUE, K100_CHANNEL, PER_KEY_COLOUR,
                                          led, ONE]))
            if reply is None:
                print(f"LED {led}: no answer", file=sys.stderr)
                return 1
            if (reply[5], reply[6]) != (0, 0):
                painted.append((led, reply[5], reply[6]))

        print(f"{len(painted)} of {args.all} LEDs carry a colour")
        for led, hue, sat in painted:
            print(f"  LED {led:<3} hue {hue:<3} saturation {sat}")

    if args.led is None:
        return 0

    if args.colour is not None:
        hue, sat = args.colour
        written = exchange(node, bytes([CUSTOM_SET_VALUE, K100_CHANNEL, PER_KEY_COLOUR,
                                        args.led, ONE, hue, sat]))
        if written is None:
            print("the write was not answered", file=sys.stderr)
            return 1

    reply = exchange(node, bytes([CUSTOM_GET_VALUE, K100_CHANNEL, PER_KEY_COLOUR,
                                  args.led, ONE]))
    if reply is None:
        print("the read was not answered", file=sys.stderr)
        return 1

    print(f"LED {args.led}: hue {reply[5]}, saturation {reply[6]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
