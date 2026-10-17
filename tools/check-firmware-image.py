#!/usr/bin/env python3
"""Checks properties of a linked LPC546xx image that no source file states.

Each of them fails *silently* on hardware: a bad checksum means the boot ROM
never starts the image, an interrupt vector in the wrong slot means the
peripheral's handler is simply never called, and a realtime counter taken from
an inaccessible DWT turns every microsecond delay into a hang. None is visible
in a source tree, because each is decided by the linker or by configuration
spread across several trees -- so they are checked on the artifact, by whatever
built it.

Used by chibios/test/build-demo.sh and qmk/test/build-k100.sh.

    tools/check-firmware-image.py <elf> [--allow-dwt]

Needs arm-none-eabi-objdump on PATH.
"""

from __future__ import annotations

import argparse
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


def section_words(elf: str, section: str | None = None) -> list[int]:
    """One section, or the whole image, as little-endian words.

    objdump prints raw bytes in memory order, so a word has to be reassembled
    rather than read off the line -- searching that output for an address as
    written finds nothing, whether or not the address is there.
    """
    where = ("-j", section) if section else ()
    words: list[int] = []
    for line in run("arm-none-eabi-objdump", "-s", *where, elf).splitlines():
        match = re.match(r"\s*([0-9a-f]{4,8})\s((?:[0-9a-f]{8} ){1,4})", line)
        if match:
            words += [
                int(bytes.fromhex(group)[::-1].hex(), 16)
                for group in match.group(2).split()
            ]
    return words


def check_boot_checksum(elf: str) -> bool:
    """The boot ROM sums the first eight vectors and wants zero."""
    words = section_words(elf, ".vectors")

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
    words = section_words(elf, ".vectors")

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


def check_realtime_counter(elf: str, allow_dwt: bool = False) -> bool:
    """The realtime counter comes from the RIT, and nothing reads the DWT.

    This part's debug block is not reachable from software, so every DWT
    register reads as a constant and the write that starts the cycle counter
    faults. ChibiOS's realtime counter is that cycle counter by default, and the
    one thing built on the realtime counter is chSysPolledDelayX() -- which waits
    for the counter to pass a value, and so against a constant counter does not
    delay but hangs. On this platform that function is QMK's wait_us(), which the
    matrix scan calls before USB is started, so the whole visible symptom was a
    keyboard that reached QMK's pre-init and vanished.

    Nothing about that is visible in either source tree, and the configuration
    that avoids it is spread across a keyboard chconf.h, two patches to ChibiOS
    proper and a driver. Any one of them quietly reverting puts the hang back, so
    the artifact is checked instead.

    Both halves are checked by the same means, and that is the point of checking
    the first one. An absence proves nothing unless the method can see a presence
    -- grepping objdump's `-s` output for these addresses finds neither, because
    that output is raw bytes and a little-endian word of 0x4002D000 is printed as
    `00d00240`. So the RIT reference is *required* to be found: it is the control
    that keeps the DWT half from passing vacuously.

    What this cannot see is an address the compiler builds up with arithmetic
    instead of loading whole, which lpcRitInit() is itself an example of. The
    accessor the counter actually goes through loads it whole, so the control
    holds.

    @p allow_dwt is for an image built for a board whose debug block works,
    where the stock cycle counter is the better choice and the RIT driver is
    compiled only to keep it building.
    """
    rit_counter_base = 0x4002D000
    dwt_registers = range(0xE0001000, 0xE0001020, 4)

    words = set(section_words(elf))

    found_dwt = sorted(word for word in dwt_registers if word in words)
    found_rit = rit_counter_base in words

    problems = []
    if not found_rit:
        problems.append(f"no reference to the RIT at 0x{rit_counter_base:08x}")
    if found_dwt and not allow_dwt:
        problems.append("DWT at " + ", ".join(f"0x{word:08x}" for word in found_dwt))

    detail = "RIT" if found_rit else "no RIT"
    detail += ", DWT too" if found_dwt else ", no DWT"

    verdict = "ok" if not problems else "FAIL " + "; ".join(problems)
    print(f"realtime counter         {verdict}   {detail}")
    return not problems


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("elf", help="the linked image to check")
    parser.add_argument(
        "--allow-dwt",
        action="store_true",
        help="permit DWT references, for a board whose debug block is accessible",
    )
    args = parser.parse_args(argv[1:])

    if not Path(args.elf).is_file():
        print(f"no such file: {args.elf}", file=sys.stderr)
        return 2

    results = [
        check_boot_checksum(args.elf),
        check_required_handlers(args.elf),
        check_realtime_counter(args.elf, allow_dwt=args.allow_dwt),
    ]
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
