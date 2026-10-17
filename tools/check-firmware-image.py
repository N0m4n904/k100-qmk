#!/usr/bin/env python3
"""Checks properties of a linked LPC546xx image that only the linker decides.

Neither can be asserted from source, and both fail *silently* on hardware -- a
bad checksum means the boot ROM never starts the image, and an interrupt vector
in the wrong slot means the peripheral's handler is simply never called. So they
are checked on the artifact, by whatever built it.

Used by chibios/test/build-demo.sh and qmk/test/build-k100.sh.

    tools/check-firmware-image.py <elf>

Needs arm-none-eabi-objdump and arm-none-eabi-nm on PATH.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

# Cortex-M vector table index of an IRQ: sixteen system entries come first.
FIRST_IRQ_INDEX = 16

# Peripherals whose handler must be wired up, by NXP IRQn. Getting one of these
# wrong is not hypothetical: FLEXCOMM0 to FLEXCOMM2 were each one slot low in an
# earlier version of this port, which no build or test noticed.
REQUIRED_HANDLERS = {
    47: "USB1 (USBHSD, the controller the K100 is wired to)",
}


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


def handler_is_stub(elf: str, target: int) -> bool:
    """Does this vector target ChibiOS's do-nothing handler?

    Not an address comparison, which is the obvious approach and does not work:
    ChibiOS gives every unused vector its *own* weak symbol, each a single
    branch to the shared @p _unhandled_exception. So the slots differ while all
    meaning the same thing. Reading the one instruction they contain is what
    actually distinguishes a wired handler from an unwired one.
    """
    start = target & ~1
    listing = run("arm-none-eabi-objdump", "-d",
                  f"--start-address={start}", f"--stop-address={start + 4}", elf)
    return "_unhandled_exception" in listing


def check_required_handlers(elf: str) -> bool:
    """Each peripheral this port needs has a real handler in its vector slot.

    ChibiOS names a vector by its byte offset into the table, so a one-slot
    error is a plausible typo that compiles, links and produces an image whose
    interrupt goes to the unhandled-exception stub -- where it spins forever,
    looking exactly like a dead peripheral. Nothing else in the build catches
    it.
    """
    words = vector_words(elf)

    failed, reported = [], []
    for irq, name in sorted(REQUIRED_HANDLERS.items()):
        index = FIRST_IRQ_INDEX + irq
        if index >= len(words):
            failed.append(f"{name}: slot {index} is past the end of the table")
            continue

        target = words[index]
        reported.append(f"IRQ {irq} -> 0x{target:08x}")
        if handler_is_stub(elf, target):
            failed.append(f"{name}: slot {index} is the unhandled-exception stub")

    verdict = "ok" if not failed else "FAIL " + "; ".join(failed)
    print(f"interrupt handlers       {verdict}   " + ", ".join(reported))
    return not failed


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2

    elf = argv[1]
    if not Path(elf).is_file():
        print(f"no such file: {elf}", file=sys.stderr)
        return 2

    results = [check_boot_checksum(elf), check_required_handlers(elf)]
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
