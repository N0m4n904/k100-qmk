#!/usr/bin/env python3
"""Reads what the keyboard holds for VIA's macros, and can write one.

The six keys marked G1 to G6 are `MC_0` to `MC_5`, the first six of VIA's
macros: the keycode names a macro and the macro itself lives in the keyboard's
EEPROM, recorded by the application. Whether that storage is there at all is a
property of this board rather than of QMK -- the EEPROM region is 2 KB and the
dynamic keymap for an 8 by 18 matrix takes a little over 1200 bytes of it, so
what is left for macros is what is left. This asks the keyboard.

    python3 tools/via-macros.py                       # what it holds
    python3 tools/via-macros.py --set 0 --text k100   # write one, to test a key

A macro is a sequence of bytes ending in NUL, and the buffer holds them one
after another: macro *n* is what lies between the *n*th and the *n*+1th NUL.
Plain text is its own bytes, and anything that is not typing is an escape --
`\\x01` and one more byte for tapping a keycode, among others. Only text is
written here, which is enough to see a key work; the application records the
rest.

Needs read and write on the hidraw node, so run it as root or install
udev/99-corsair-k100.rules.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_stored import (  # noqa: E402
    CHUNK, MACRO_GET_BUFFER, MACRO_GET_BUFFER_SIZE, MACRO_GET_COUNT, MACRO_SET_BUFFER, ask,
    read_buffer,
)
from k100_usb import PRODUCT, VENDOR, device, identity  # noqa: E402
from k100_via import interface  # noqa: E402


def write_buffer(node: Path, offset: int, payload: bytes) -> None:
    """From an offset, which a whole-buffer write does not need and this does.

    Only the macros are written a part at a time: everything up to the macro being changed
    stays where it is, so the write starts there rather than at nothing.
    """
    for at in range(0, len(payload), CHUNK):
        chunk = payload[at:at + CHUNK]
        here = offset + at
        ask(node, bytes([MACRO_SET_BUFFER, here >> 8, here & 0xFF, len(chunk)]) + chunk)


def describe(macro: bytes) -> str:
    """One macro, as text where it is text and as bytes where it is not."""
    if not macro:
        return "empty"
    printable = all(0x20 <= byte < 0x7F for byte in macro)
    return repr(macro.decode("ascii")) if printable else macro.hex(" ")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--vid", default=VENDOR, help="USB vendor id, hex")
    parser.add_argument("--pid", default=PRODUCT, help="USB product id, hex")
    parser.add_argument("--set", type=int, metavar="N",
                        help="write macro N, which must be the first empty one or lower")
    parser.add_argument("--text", help="the text that macro types")
    args = parser.parse_args()

    if (args.set is None) != (args.text is None):
        raise SystemExit("--set and --text go together")

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

    count = ask(node, bytes([MACRO_GET_COUNT]))[1]
    size = int.from_bytes(ask(node, bytes([MACRO_GET_BUFFER_SIZE]))[1:3], "big")
    print(f"macros   {count}")
    print(f"buffer   {size} bytes")

    if args.set is not None:
        if not 0 <= args.set < count:
            raise SystemExit(f"this keyboard has macros 0 to {count - 1}")

        # Everything up to the macro being written is kept, and everything
        # after it is dropped: the macros are packed one after another, so a
        # longer one would have to push the rest along and that is the
        # application's job, not this tool's.
        before = read_buffer(node, MACRO_GET_BUFFER, size).split(b"\x00")[:args.set]
        kept = b"".join(part + b"\x00" for part in before)
        payload = kept + args.text.encode("ascii") + b"\x00"
        if len(payload) > size:
            raise SystemExit(f"{len(payload)} bytes does not fit in {size}")

        write_buffer(node, 0, payload + bytes(min(CHUNK, size - len(payload))))
        print(f"wrote    macro {args.set} as {args.text!r}, "
              f"and cleared any macro after it")

    buffer = read_buffer(node, MACRO_GET_BUFFER, size)
    macros = buffer.split(b"\x00")[:count]
    used = len(b"".join(macros)) + len(macros)
    print(f"used     {used} of {size} bytes")
    for number, macro in enumerate(macros):
        print(f"  M{number:<2} {describe(macro)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
