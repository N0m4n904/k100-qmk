#!/usr/bin/env python3
"""Checks properties of a linked LPC546xx image that only the linker decides.

Neither property can be asserted from source, and both fail *silently* on
hardware -- a bad checksum means the boot ROM never starts the image, and
misplaced endpoint memory means USB quietly does not work. So they are checked
on the artifact, by whatever built it.

Used by chibios/test/build-demo.sh and qmk/test/build-k100.sh.

    tools/check-firmware-image.py <elf>

Needs arm-none-eabi-objdump and arm-none-eabi-nm on PATH.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

# EPLISTSTART ignores the low eight address bits.
EPLIST_ALIGNMENT = 256

# DATABUFSTART supplies only the high address bits, so the endpoint list and
# every buffer have to share one 4 MB region.
DATABUF_REGION_SHIFT = 22

# The SRAM the USB controller can reach as a bus master. SRAMX at 0x04000000 is
# tightly coupled to the core and is not reachable.
REACHABLE_SRAM = range(0x20000000, 0x20028000)


def run(*args: str) -> str:
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout


def vector_words(elf: str) -> list[int]:
    """The .vectors section, as little-endian words."""
    words: list[int] = []
    for line in run("arm-none-eabi-objdump", "-s", "-j", ".vectors", elf).splitlines():
        match = re.match(r"\s*([0-9a-f]{4,8})\s((?:[0-9a-f]{8} ){1,4})", line)
        if match:
            words += [
                int(bytes.fromhex(group)[::-1].hex(), 16)
                for group in match.group(2).split()
            ]
    return words


def check_boot_checksum(elf: str) -> bool:
    """The boot ROM sums the first eight vectors and wants zero."""
    words = vector_words(elf)

    # Without this the check would be worthless: a parse that matched nothing
    # leaves an empty list, whose sum is zero, which is the pass condition.
    if len(words) < 8:
        print(f"vector checksum         FAIL   parsed {len(words)} vectors, so this "
              "check proves nothing -- has objdump's output format changed?")
        return False

    total = sum(words[:8]) & 0xFFFFFFFF
    print(f"vector checksum          {'ok' if total == 0 else 'FAIL'}   "
          f"{len(words)} vectors, first 8 sum to 0x{total:08x}")
    return total == 0


def check_endpoint_memory(elf: str) -> bool:
    """Alignment, region and reachability of the USB endpoint memory."""
    symbols = run("arm-none-eabi-nm", "-S", elf)
    match = re.search(r"^([0-9a-f]+) ([0-9a-f]+) . lpc_usb_sram$", symbols, re.M)
    if match is None:
        print("usb endpoint memory     FAIL   lpc_usb_sram is not in the image")
        return False

    address, size = int(match.group(1), 16), int(match.group(2), 16)
    checks = {
        f"{EPLIST_ALIGNMENT}-byte aligned": address % EPLIST_ALIGNMENT == 0,
        "single 4MB window": (address >> DATABUF_REGION_SHIFT)
        == ((address + size - 1) >> DATABUF_REGION_SHIFT),
        "in reachable SRAM": address in REACHABLE_SRAM,
    }

    failed = [name for name, passed in checks.items() if not passed]
    verdict = "ok" if not failed else "FAIL " + ", ".join(failed)
    print(f"usb endpoint memory      {verdict}   0x{address:08x}, {size} bytes, "
          + ", ".join(checks))
    return not failed


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2

    elf = argv[1]
    if not Path(elf).is_file():
        print(f"no such file: {elf}", file=sys.stderr)
        return 2

    results = [check_boot_checksum(elf), check_endpoint_memory(elf)]
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
