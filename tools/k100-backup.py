#!/usr/bin/env python3
"""Takes everything the keyboard has stored off it, and puts it back.

The keymap, the macros, every LED's colour and what the reactive effects are set to -- the
four things in this keyboard's EEPROM that somebody chose rather than compiled in.

    python3 tools/k100-backup.py --save before.json
    python3 tools/k100-backup.py --restore before.json

**What this is for.** One change clears all four: `EECONFIG_MAGIC_NUMBER` moving, which
happens when something new needs room in the stored layout and is the one case where what is
in there cannot be read back across the change. It is deliberate, it is written down in
`config.h` beside every move that has happened, and it is survivable exactly once per
keyboard -- by saving before the update and restoring after it. Run it before every update
that the release notes say reshapes the storage, and there is nothing to lose by running it
before the others.

**A restore is checked, not hoped about.** Everything is written and then read back and
compared, and the tool says which of the four match. A backup nobody has restored is a file,
not a backup.

A saved file also carries the matrix and the layer count it was taken from, and a restore
refuses a file that does not match the keyboard in front of it: a keymap buffer written at the
wrong shape is not a wrong keymap, it is a scrambled one.

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
    DEFINITION, Refused, fit_macros, layers, macro_buffer_size, macro_content, menu_value,
    read_colours, read_keymap, read_macros, read_settings, write_colours, write_keymap,
    write_macros, write_settings,
)
from k100_usb import PRODUCT, VENDOR, device, identity  # noqa: E402
from k100_via import interface  # noqa: E402


def matrix() -> tuple[int, int]:
    """How many rows and columns, out of the definition the application loads."""
    declared = json.loads(DEFINITION.read_text())["matrix"]
    return declared["rows"], declared["cols"]


#: The four things a backup carries, in the order they are reported.
PARTS = ("keymap", "macros", "leds", "settings")


def holds(part: str, saved, now) -> bool:
    """Whether the keyboard now holds what the file does, for one of the four.

    The macros are the one that is not a plain comparison. Their buffer is whatever the 2 KB
    EEPROM has left, so growing the stored lighting shrinks it -- and 269 bytes of nothing and
    237 bytes of nothing hold the same macros. What is compared is the content; the length is
    the keyboard's business.
    """
    if part == "macros":
        return macro_content(bytes.fromhex(now)) == macro_content(bytes.fromhex(saved))
    return now == saved


def read_all(node: Path, rows: int, cols: int) -> dict:
    return {
        "matrix": {"rows": rows, "cols": cols},
        "layers": layers(node),
        "keymap": read_keymap(node, rows, cols).hex(),
        "macros": read_macros(node).hex(),
        "leds": read_colours(node),
        "settings": read_settings(node),
    }


def describe(state: dict) -> None:
    painted = sum(1 for hue, sat, val in state["leds"]
                  if (hue, sat) != (0, 0) or val != 255)
    macros = bytes.fromhex(state["macros"])

    print(f"keymap   {len(state['keymap']) // 2} bytes, {state['layers']} layers of "
          f"{state['matrix']['rows']} by {state['matrix']['cols']}")
    print(f"macros   {len(macro_content(macros))} bytes of macros in a "
          f"{len(macros)}-byte buffer")
    print(f"colours  {len(state['leds'])} LEDs, {painted} set to something other than "
          f"plain white")
    print(f"settings {', '.join(f'{name} {value}' for name, value in state['settings'].items())}")


def restore(node: Path, saved: dict, rows: int, cols: int) -> int:
    """Writes all four back, then reads them again and says which of them took.

    The keymap goes last. It is the one whose half-written state is worth least -- a keyboard
    with the wrong keycodes on some keys -- and putting it at the end means an interrupted
    restore has already put the rest back, with the file still on disk to try again with.
    """
    here = saved["matrix"]
    if (here["rows"], here["cols"]) != (rows, cols):
        print(f"this file was taken from a {here['rows']} by {here['cols']} matrix and this "
              f"keyboard is {rows} by {cols}", file=sys.stderr)
        return 1

    found = layers(node)
    if saved["layers"] != found:
        print(f"this file holds {saved['layers']} layers and this keyboard has {found}",
              file=sys.stderr)
        return 1

    paint_value = menu_value("id_k100_paint_value")
    if paint_value is None:
        print(f"{DEFINITION} declares no id_k100_paint_value", file=sys.stderr)
        return 1

    write_colours(node, saved["leds"], paint_value)
    write_settings(node, saved["settings"])
    write_macros(node, fit_macros(bytes.fromhex(saved["macros"]), macro_buffer_size(node)))
    write_keymap(node, bytes.fromhex(saved["keymap"]))

    print("written, and now read back:")

    now = read_all(node, rows, cols)
    wrong = [part for part in PARTS if not holds(part, saved[part], now[part])]

    for part in PARTS:
        print(f"  {part:<9}{'identical' if part not in wrong else 'DOES NOT MATCH'}")

    if wrong:
        print(f"\nthe keyboard does not hold what the file does: {', '.join(wrong)}. "
              f"Nothing here can say which of the two is right, so the file is left alone.",
              file=sys.stderr)
        return 1

    print("\nthe keyboard reads back byte for byte identical to the file.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--vid", default=VENDOR, help="USB vendor id, hex")
    parser.add_argument("--pid", default=PRODUCT, help="USB product id, hex")
    parser.add_argument("--save", type=Path, metavar="FILE",
                        help="read everything the keyboard stores into a file")
    parser.add_argument("--restore", type=Path, metavar="FILE",
                        help="write a saved file back, and check that it took")
    args = parser.parse_args()

    if (args.save is None) == (args.restore is None):
        raise SystemExit("--save or --restore, one of the two")

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

    rows, cols = matrix()

    try:
        if args.save is not None:
            state = read_all(node, rows, cols)
            state["firmware"] = version
            args.save.write_text(json.dumps(state, indent=1) + "\n")
            describe(state)
            print(f"\nsaved to {args.save}")
            return 0

        saved = json.loads(args.restore.read_text())
        # Backups written before these settings outgrew the reaction they started as called
        # them something narrower. A backup tool that cannot read its own older files is not
        # one.
        saved.setdefault("settings", saved.pop("reaction", {}))
        describe(saved)
        print()
        return restore(node, saved, rows, cols)
    except Refused as refusal:
        print(f"\n{refusal}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
