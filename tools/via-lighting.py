#!/usr/bin/env python3
"""Reads or sets the keyboard's lighting over VIA, exactly as the app's menu does.

The VIA definition declares a lighting menu, and that menu is four values on one
channel: brightness, effect, effect speed and colour. This drives the same four,
so the menu can be checked against the firmware without the application -- if
these read and write here, the app's sliders reach the same code.

    python3 tools/via-lighting.py                       # read all four
    python3 tools/via-lighting.py --effect 2            # rainbow, until reboot
    python3 tools/via-lighting.py --colour 0,255 --brightness 200 --save

A write is kept in RAM unless --save is given, so anything set here is undone by
unplugging the keyboard. That is QMK's split, not this tool's: the app's sliders
set values live and save them separately.

Effect names come from the VIA definition rather than from a list of their own,
which makes a read a check of the definition too: the number the firmware
reports is looked up in the dropdown the app would show.

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
from k100_via import (  # noqa: E402
    CUSTOM_GET_VALUE, CUSTOM_SAVE, CUSTOM_SET_VALUE, UNHANDLED, exchange, interface,
)

#: QMK's channel for the RGB matrix, from `enum via_channel_id`.
CHANNEL = 3

#: The channel's values, from `enum via_qmk_rgb_matrix_value`, with how many
#: bytes each carries. Colour is the only one wider than a byte: hue then
#: saturation, with brightness held separately.
VALUES = {
    "brightness": (1, 1),
    "effect": (2, 1),
    "speed": (3, 1),
    "colour": (4, 2),
}

#: Where the app's menu is declared, read for the effect names.
DEFINITION = Path(__file__).resolve().parent.parent / "via/k100-iso-de.json"


#: The values a write lands in unchanged, and so can be checked by reading them
#: back. Brightness is not one of them: the firmware scales it by its own
#: ceiling on the way in and back out on the way out, and that ceiling --
#: RGB_MATRIX_MAXIMUM_BRIGHTNESS -- is a compile-time figure the host cannot
#: read. At its default of 255 the round trip is lossless, which is what this
#: keyboard measures; a firmware that lowered it would answer with less than
#: was asked for, and that is the firmware working rather than failing.
EXACT = ("effect", "speed", "colour")


def effect_names() -> dict[int, str]:
    """The effect numbers the app offers, by value, from the definition's menu.

    Empty if the definition is missing or declares no lighting menu, in which
    case the numbers are reported bare -- a missing file is not a reason to fail
    a read of the keyboard.
    """
    try:
        definition = json.loads(DEFINITION.read_text())
    except (OSError, ValueError):
        return {}

    for menu in definition.get("menus", []):
        # A menu is either one of VIA's own by name or a declaration of its own.
        # Only the second kind says anything about the numbers.
        if not isinstance(menu, dict):
            continue
        for submenu in menu.get("content", []):
            for item in submenu.get("content", []):
                if item.get("content", [])[:1] != ["id_qmk_rgb_matrix_effect"]:
                    continue
                options = item["options"]
                # VIA takes either plain labels, numbered by position, or
                # labels carrying their own value.
                if all(isinstance(option, str) for option in options):
                    return dict(enumerate(options))
                return {value: label for label, value, *_ in options}
    return {}


def read(node: Path, name: str) -> list[int] | None:
    """One value, as the bytes the firmware answers with."""
    value_id, size = VALUES[name]
    reply = exchange(node, bytes([CUSTOM_GET_VALUE, CHANNEL, value_id]))

    if reply is None:
        print(f"the read of {name} was not answered.", file=sys.stderr)
        return None
    if reply[0] == UNHANDLED:
        print(f"the firmware does not serve channel {CHANNEL}: it answered "
              f"{UNHANDLED:#04x} to a read of {name}. Without that channel the "
              f"app's lighting menu has nothing to talk to.", file=sys.stderr)
        return None

    # The reply comes back in the request's own shape, so the data sits where it
    # would have been written: [command, channel, value_id, data...].
    return list(reply[3:3 + size])


def write(node: Path, name: str, data: list[int]) -> None:
    value_id, _ = VALUES[name]
    exchange(node, bytes([CUSTOM_SET_VALUE, CHANNEL, value_id, *data]))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--vid", default=VENDOR, help="USB vendor id, hex")
    parser.add_argument("--pid", default=PRODUCT, help="USB product id, hex")
    parser.add_argument("--brightness", type=int, help="0 to 255")
    parser.add_argument("--effect", type=int,
                        help="an effect number; 0 turns the lighting off")
    parser.add_argument("--speed", type=int, help="0 to 255")
    parser.add_argument("--colour", help="hue,saturation, each 0 to 255")
    parser.add_argument("--save", action="store_true",
                        help="also write the result to the part's EEPROM")
    args = parser.parse_args()

    entry = device(args.vid, args.pid)
    if entry is None:
        print(f"no {args.vid}:{args.pid} on the bus.", file=sys.stderr)
        return 1

    version, product = identity(entry)
    print(f"running  bcdDevice {version}  {product}")

    found = interface(entry)
    if found is None:
        print("none of this device's HID interfaces answered VIA's protocol "
              "version request. Either VIA is not in this firmware, or it is "
              "not reachable.", file=sys.stderr)
        return 1

    node, protocol = found
    print(f"VIA on {node}, protocol {protocol:#06x}")

    # What was asked for, against what the firmware should report afterwards.
    # Effect, speed and colour are stored as given; brightness is scaled.
    asked: dict[str, list[int]] = {}
    if args.effect is not None:
        asked["effect"] = [args.effect]
    if args.speed is not None:
        asked["speed"] = [args.speed]
    if args.colour is not None:
        asked["colour"] = [int(part) for part in args.colour.split(",")]
    if args.brightness is not None:
        asked["brightness"] = [args.brightness]

    for name, data in asked.items():
        write(node, name, data)

    if args.save and asked:
        exchange(node, bytes([CUSTOM_SAVE, CHANNEL]))

    names = effect_names()
    failures = []
    scaled = []

    for name in VALUES:
        data = read(node, name)
        if data is None:
            return 1

        if name == "effect":
            reading = f"{data[0]} ({names.get(data[0], 'not in the definition')})"
        elif name == "colour":
            reading = f"hue {data[0]}, saturation {data[1]}"
        else:
            reading = str(data[0])
        print(f"{name:<11} {reading}")

        if name in asked and data != asked[name]:
            difference = f"{name}: asked for {asked[name]}, read back {data}"
            (failures if name in EXACT else scaled).append(difference)

    for note in scaled:
        print(f"\n{note}, which is the firmware's brightness ceiling scaling it "
              f"rather than a failed write.")

    if failures:
        print("\nthe write did not take:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    if args.save and asked:
        print("\nsaved to EEPROM, so it survives a reboot.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
