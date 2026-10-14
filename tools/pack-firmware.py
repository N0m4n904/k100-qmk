#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wraps a built firmware image in Corsair's container, and proves it can.

The K100's bootloader does not start an application from a vector table. It
reads an entry address out of a 0x2c-byte header and branches there, so a custom
image has to carry that header with its own entry address in it. This writes it.

    # Prove the writer against Corsair's own images first
    tools/pack-firmware.py --verify firmware/*.bin

    # Then wrap ours
    tools/pack-firmware.py build/ch.bin --elf build/ch.elf --out k100.bin

`--verify` is the part worth trusting. It parses each official image, rebuilds
the container from that image's own payload and fields, and compares the result
byte for byte with the original. A writer that reproduces both published images
exactly has the layout, the CRC and the length right.
"""

from __future__ import annotations

import argparse
import re
import struct
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_container import (  # noqa: E402
    APPLICATION_BASE,
    HEADER_SIZE,
    IMAGE_TYPE_FIRMWARE,
    Container,
)

#: The K100 RGB with OPX optical switches.
DEFAULT_PRODUCT_ID = 0x1B7C

#: Where LPC546xx_app_at_64k.ld puts the vector table, relative to the image's
#: first byte. The table needs 1 KB alignment and the header occupies the first
#: 0x2c bytes, so the first aligned address past the header is this one.
VECTOR_TABLE_OFFSET = 0x400

#: Flash available past Corsair's bootloader.
APPLICATION_SIZE = 448 * 1024


def verify(images: list[Path]) -> int:
    """Rebuild each official image from its own payload and compare."""
    failures = 0
    for path in images:
        original = path.read_bytes()
        try:
            container = Container.parse(original)
        except ValueError as error:
            print(f"FAIL {path.name}: {error}")
            failures += 1
            continue

        rebuilt = container.pack()

        if rebuilt == original:
            print(f"ok   {path.name}: rebuilt byte for byte ({container.describe()})")
            continue

        failures += 1
        differing = [i for i, (a, b) in enumerate(zip(rebuilt, original)) if a != b]
        print(f"FAIL {path.name}: rebuilt image differs at {len(differing)} byte(s)"
              + (f", first at 0x{differing[0]:X}" if differing else "")
              + (f"; lengths {len(rebuilt)} vs {len(original)}"
                 if len(rebuilt) != len(original) else ""))
    return failures


def check(path: Path) -> bool:
    """Everything that can be established about a container without writing it.

    Each of these has a way of being wrong that the keyboard would not survive
    or would not report, so they are checked on the artifact that would actually
    be written rather than on the inputs that produced it.
    """
    image = path.read_bytes()
    container = Container.parse(image)
    results: list[tuple[bool, str]] = []

    results.append((container.image_type == IMAGE_TYPE_FIRMWARE,
                    f"image type is {container.image_type}, firmware is "
                    f"{IMAGE_TYPE_FIRMWARE} -- writing a bootloader image is the one "
                    f"unrecoverable mistake"))

    results.append((container.declared_length == len(image),
                    f"declared length {container.declared_length} against actual "
                    f"{len(image)}"))

    results.append((container.crc_valid,
                    f"CRC-32/MPEG-2 over 0x04 onward matches the stored "
                    f"0x{container.declared_crc:08X}"))

    results.append((container.product_id == DEFAULT_PRODUCT_ID,
                    f"product id 0x{container.product_id:04x}, the OPX K100 is "
                    f"0x{DEFAULT_PRODUCT_ID:04x}"))

    results.append((len(image) <= APPLICATION_SIZE,
                    f"{len(image)} bytes fits the {APPLICATION_SIZE // 1024} KB "
                    f"application region"))

    results.append((container.entry % 2 == 1,
                    f"entry 0x{container.entry:08X} is a Thumb address"))

    entry_offset = (container.entry & ~1) - APPLICATION_BASE
    results.append((0 <= entry_offset < len(image),
                    f"entry 0x{container.entry:08X} lands inside the image, at "
                    f"offset 0x{entry_offset:X}"))

    # The bootloader branches to the header's entry. The vector table carries its
    # own idea of the reset handler. If those two disagree the image has two
    # answers to where it starts, and only one of them is used.
    #
    # Only an image linked by LPC546xx_app_at_64k.ld has a table there, so this
    # pair is skipped otherwise; it does not apply to Corsair's own images, whose
    # code starts well before 0x400.
    table_offset = VECTOR_TABLE_OFFSET
    if table_offset + 8 <= len(image):
        stack, reset = struct.unpack_from("<II", image, table_offset)
        results.append((container.entry == reset,
                        f"header entry 0x{container.entry:08X} agrees with the vector "
                        f"table's reset handler 0x{reset:08X}"))
        results.append((0x20000000 <= stack < 0x20028000,
                        f"vector table's initial stack pointer 0x{stack:08X} is in "
                        f"reachable SRAM"))

    for passed, text in results:
        print(f"  {'ok  ' if passed else 'FAIL'}  {text}")

    failed = [t for p, t in results if not p]
    print()
    if failed:
        print(f"{len(failed)} of {len(results)} checks failed; this image should not "
              f"be written")
        return False
    print(f"all {len(results)} checks pass. That means the container is well formed "
          f"and self-consistent.")
    print("It does not mean the firmware runs: nothing in it has ever executed.")
    return True


def entry_from_elf(elf: Path) -> int:
    """The Thumb entry address the bootloader should branch to.

    From the ELF header's own entry point, which the linker already sets to the
    reset handler with the Thumb bit on. Reading it from the symbol table
    instead looks equivalent and is not: ChibiOS declares Reset_Handler weak, so
    it appears as `W` rather than `T` and a search for a text symbol misses it.
    """
    out = subprocess.run(["arm-none-eabi-readelf", "-h", str(elf)],
                         capture_output=True, text=True, check=True).stdout
    match = re.search(r"Entry point address:\s*0x([0-9a-fA-F]+)", out)
    if match is None:
        raise SystemExit(f"no entry point in the ELF header of {elf}")

    entry = int(match.group(1), 16)
    if entry % 2 == 0:
        raise SystemExit(
            f"entry 0x{entry:08X} in {elf} is even, so it is not a Thumb address; "
            f"the bootloader branches to it and an even address would fault"
        )
    return entry


def load_address_from_elf(elf: Path) -> int:
    """Where the image's first byte belongs in flash.

    Read from the ELF so the padding between the header and the code cannot
    disagree with what the linker actually did. objdump prints a section's
    numbers on one line and its flags on the next, so the two are read together.
    """
    out = subprocess.run(["arm-none-eabi-objdump", "-h", str(elf)],
                         capture_output=True, text=True, check=True).stdout
    lines = out.splitlines()
    section = re.compile(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)")

    loaded = []
    for index, line in enumerate(lines):
        match = section.match(line)
        if match is None:
            continue
        size, lma = int(match.group(2), 16), int(match.group(4), 16)
        flags = lines[index + 1] if index + 1 < len(lines) else ""
        if size and "LOAD" in flags and "ALLOC" in flags:
            loaded.append(lma)

    if not loaded:
        raise SystemExit(f"could not find a loadable section in {elf}")
    return min(loaded)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("image", type=Path, nargs="*",
                        help="raw .bin to wrap, or images to --verify")
    parser.add_argument("--verify", action="store_true",
                        help="rebuild official images from their own payloads and compare")
    parser.add_argument("--check", action="store_true",
                        help="validate a built container as far as is possible without flashing")
    parser.add_argument("--elf", type=Path,
                        help="the matching ELF, to read the entry point from")
    parser.add_argument("--entry", type=lambda v: int(v, 0),
                        help="entry point, if not taking it from an ELF")
    parser.add_argument("--pid", type=lambda v: int(v, 0), default=DEFAULT_PRODUCT_ID)
    parser.add_argument("--version", default="0.99.0",
                        help="version to declare, as 0.minor.patch")
    parser.add_argument("--out", type=Path, help="where to write the container")
    args = parser.parse_args(argv[1:])

    if args.check:
        if len(args.image) != 1:
            parser.error("--check takes exactly one image")
        return 0 if check(args.image[0]) else 1

    if args.verify:
        if not args.image:
            parser.error("--verify needs at least one image")
        failures = verify(args.image)
        if failures:
            print(f"\n{failures} image(s) could not be reproduced; the container "
                  f"writer does not match Corsair's")
            return 1
        print(f"\nok: all {len(args.image)} official images rebuilt exactly, so the "
              f"layout, the CRC and the length are right")
        return 0

    if len(args.image) != 1 or args.out is None:
        parser.error("wrapping takes exactly one image and an --out")

    if args.entry is not None:
        entry = args.entry
    elif args.elf is not None:
        entry = entry_from_elf(args.elf)
    else:
        parser.error("need --elf or --entry to know where the bootloader should branch")

    code = args.image[0].read_bytes()

    # The whole container is written starting at the application base, so the
    # code has to be preceded by the header and by enough padding to land on the
    # address it was linked for. Taking that address from the ELF rather than
    # assuming it is what stops the two from drifting apart.
    if args.elf is not None:
        load = load_address_from_elf(args.elf)
    else:
        load = APPLICATION_BASE + VECTOR_TABLE_OFFSET
        print(f"note: no --elf, assuming the image is linked at 0x{load:08X}")

    gap = load - APPLICATION_BASE
    if gap < HEADER_SIZE:
        print(f"refusing: the image is linked at 0x{load:08X}, which leaves "
              f"{gap} bytes before it -- the container header needs "
              f"0x{HEADER_SIZE:X}. Link it at "
              f"0x{APPLICATION_BASE + VECTOR_TABLE_OFFSET:08X}; "
              f"see LPC546xx_app_at_64k.ld", file=sys.stderr)
        return 1

    padding = bytes(gap - HEADER_SIZE)
    payload = padding + code

    _, minor, patch = (int(p) for p in args.version.split("."))
    now = datetime.now(timezone.utc)

    container = Container(
        payload=payload,
        entry=entry,
        product_id=args.pid,
        build_date=now.strftime("%b %d %Y"),
        build_time=now.strftime("%H:%M:%S"),
        version_minor=minor,
        version_patch=patch,
        image_type=IMAGE_TYPE_FIRMWARE,
    )

    low = APPLICATION_BASE + HEADER_SIZE
    high = APPLICATION_BASE + APPLICATION_SIZE
    if not low <= (entry & ~1) < high:
        print(f"refusing: entry 0x{entry:08X} is outside the application region "
              f"0x{low:05X}..0x{high:05X}", file=sys.stderr)
        return 1

    args.out.write_bytes(container.pack())
    print(f"wrote {args.out}")
    print(f"  {container.describe()}")
    print(f"  header 0x00..0x{HEADER_SIZE:02X}, then {len(padding)} bytes of padding, "
          f"then {len(code)} bytes of code at 0x{load:08X}")
    if args.elf:
        print(f"  entry and load address both read from {args.elf.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
