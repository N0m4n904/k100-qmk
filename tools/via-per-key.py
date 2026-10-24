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

from k100_usb import PRODUCT, VENDOR, device, identity  # noqa: E402
from k100_via import CUSTOM_GET_VALUE, CUSTOM_SET_VALUE, exchange, interface  # noqa: E402

#: VIA's own channel for a keyboard's values, and the value the painter uses on
#: it. Neither is this keyboard's to choose: the application has the number 1
#: built in, which is why the keyboard's own values start at 2. See
#: qmk/keyboards/corsair/k100/per_key_colour.c.
K100_CHANNEL = 0
PER_KEY_COLOUR = 1

#: The brush's brightness, which *is* one of this keyboard's own numbers --
#: `K100_VIA_PAINT_VALUE` in k100.h, where the generated definition reads it
#: from. Written down again here because a test tool that generated its own
#: definition would be testing itself.
PAINT_VALUE = 5

#: A byte the application always sends as one, after the index. Nothing in the
#: firmware reads it; it is here because the reports have to match.
ONE = 1

#: Every LED the firmware stores a colour for, which is every LED it has.
#: K100_PER_KEY_LEDS in k100.h.
K100_PER_KEY_LEDS = 190


#: The definition the application loads, which is also the record of what it sends.
DEFINITION = Path(__file__).parent.parent / "via" / "k100-iso-de.json"


def colour(text: str) -> tuple[int, int]:
    hue, _, sat = text.partition(",")
    return (int(hue), int(sat))


def menu_value(wanted: str) -> tuple[int, int] | None:
    """The channel and value a named control is sent on, out of the definition.

    The definition is where to ask: the firmware picks these numbers and the definition only
    repeats them, so a tool that wrote its own copy down would be testing itself.
    """

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


def zone_value(family: str, what: str = "") -> tuple[int, int]:
    """The channel and value one of a zone's controls is sent on."""
    found = menu_value(f"id_k100_zone_{family}{what}")
    if found is None:
        raise SystemExit(f"{DEFINITION} has no zone called {family}")
    return found


#: What the reaction's source byte means, from `enum k100_reaction_source` in
#: per_key_colour.c.
REACTION_PAINTED = 0
REACTION_CHOSEN = 1


def reaction(node: Path, wanted: str) -> int:
    """Reads or sets what the reactive effects light a key in.

    One option for both halves, because the firmware treats them as one: picking a colour is
    what selects it, so `--reaction 10,255` says everything, and `--reaction painted` is the
    way back.
    """
    source = menu_value("id_k100_reaction_source")
    chosen = menu_value("id_k100_reaction_colour")
    if source is None or chosen is None:
        raise SystemExit(f"{DEFINITION} declares no reaction colour; regenerate it with "
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

    source_reply = exchange(node, bytes([CUSTOM_GET_VALUE, *source]))
    colour_reply = exchange(node, bytes([CUSTOM_GET_VALUE, *chosen]))
    if source_reply is None or colour_reply is None:
        print("the reaction was not read", file=sys.stderr)
        return 1

    if source_reply[3] == REACTION_PAINTED:
        print("reaction the painted colours")
    else:
        print(f"reaction one colour, hue {colour_reply[3]}, saturation {colour_reply[4]}")
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
    parser.add_argument("--all", type=int, metavar="N", nargs="?", const=190,
                        help="read the first N LEDs and report the ones that carry a "
                             "colour, which is what a stored picture looks like from here")
    parser.add_argument("--reaction", nargs="?", const="", metavar="painted|HUE,SAT",
                        help="what the reactive effects light a key in: the painted colours, "
                             "or one colour, which picking selects. Reads it when given "
                             "nothing")
    args = parser.parse_args()

    if all(each is None for each in (args.led, args.brightness, args.all, args.zone,
                                     args.dump, args.load, args.reaction)):
        raise SystemExit("--led, --zone, --brightness, --all, --dump, --load, --reaction, "
                         "or a combination")

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

    if args.reaction is not None:
        failed = reaction(node, args.reaction)
        if failed:
            return failed

    if args.brightness is not None:
        if exchange(node, bytes([CUSTOM_SET_VALUE, K100_CHANNEL, PAINT_VALUE,
                                 args.brightness])) is None:
            print("the brush was not set", file=sys.stderr)
            return 1
        brush = exchange(node, bytes([CUSTOM_GET_VALUE, K100_CHANNEL, PAINT_VALUE]))
        print(f"brush: brightness {brush[3] if brush else '?'}")

    if args.dump is not None:
        stored = []
        for led in range(K100_PER_KEY_LEDS):
            reply = exchange(node, bytes([CUSTOM_GET_VALUE, K100_CHANNEL, PER_KEY_COLOUR,
                                          led, ONE]))
            if reply is None:
                print(f"LED {led} did not answer; nothing was written", file=sys.stderr)
                return 1
            stored.append([reply[5], reply[6], reply[7]])

        args.dump.write_text(json.dumps({"leds": stored}, indent=1) + "\n")
        lit = sum(1 for hue, sat, val in stored if (hue, sat) != (0, 0) or val != 255)
        print(f"dumped   {len(stored)} colours to {args.dump}, {lit} of them set to "
              f"something other than plain white")

    if args.load is not None:
        stored = json.loads(args.load.read_text())["leds"]
        if len(stored) != K100_PER_KEY_LEDS:
            raise SystemExit(f"{args.load} holds {len(stored)} colours, and this keyboard "
                             f"has {K100_PER_KEY_LEDS}")

        # Grouped by brightness, because the brightness is the brush's and setting it is a
        # command of its own: one per distinct value instead of one per LED.
        by_value: dict[int, list[int]] = {}
        for led, (_, _, val) in enumerate(stored):
            by_value.setdefault(val, []).append(led)

        for val, leds in sorted(by_value.items()):
            if exchange(node, bytes([CUSTOM_SET_VALUE, K100_CHANNEL, PAINT_VALUE,
                                     val])) is None:
                print("the brush was not set; the keyboard is half written",
                      file=sys.stderr)
                return 1

            for led in leds:
                hue, sat, _ = stored[led]
                if exchange(node, bytes([CUSTOM_SET_VALUE, K100_CHANNEL, PER_KEY_COLOUR,
                                         led, ONE, hue, sat])) is None:
                    print(f"LED {led} was not written; the keyboard is half written",
                          file=sys.stderr)
                    return 1

        print(f"loaded   {len(stored)} colours from {args.load}, in "
              f"{len(by_value)} brightness group(s)")

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
