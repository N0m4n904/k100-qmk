#!/usr/bin/env python3
"""Prints what the keyboard recorded before it stopped, and can empty the record.

The keyboard keeps its USB events and its crashes in EEPROM, where neither a watchdog reset nor
a power cycle reaches them; see qmk/keyboards/corsair/k100/fault_log.c. VIA's Diagnostics menu
shows the newest of each on one line, because a label cannot page. This prints all of it, oldest
first, with each crash after the event it came after.

    python3 tools/k100-fault-log.py                 # the whole log
    python3 tools/k100-fault-log.py --clear         # empty it
    python3 tools/k100-fault-log.py --hang locked   # needs K100_DIAGNOSTIC_HANG in the build

A time is seconds since that entry's boot, and it comes round after 49.7 days of uptime. A
crash's is the last time the main loop ran rather than the moment of the crash. To put a PC in
the source, ask the image that was running:

    arm-none-eabi-addr2line -fe corsair_k100_default.elf 0x00012a3c

Needs read and write on the hidraw node, so run it as root or install
udev/99-corsair-k100.rules.
"""

from __future__ import annotations

import argparse
import enum
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_stored import K100_CHANNEL, Refused, ask  # noqa: E402
from k100_usb import PRODUCT, VENDOR, device, identity  # noqa: E402
from k100_via import (  # noqa: E402
    CUSTOM_GET_VALUE, CUSTOM_SET_VALUE, UNHANDLED, exchange, interface, via_value,
)

#: The header that says which value each of these is answered on.
HEADER = Path(__file__).resolve().parent.parent / "qmk/keyboards/corsair/k100/k100.h"

#: Enough for a clear to finish, a slot per trip round a loop of a few milliseconds.
CLEAR_TIMEOUT_S = 10.0

#: What `--hang` can ask for, by the number the firmware takes.
HANGS = {"unlocked": 1, "locked": 2, "fault": 3}


class EventKind(enum.Enum):
    """`enum k100_log_event_kind` in k100.h. Stored numbers, so they do not move."""

    BOOT = 1
    RESET = 2
    CONFIGURED = 3
    SUSPEND = 4
    WAKEUP = 5
    HOST_GONE = 6
    HOST_BACK = 7
    BOOTLOADER_JUMP = 8


class CrashCause(enum.Enum):
    """`lpc_crash_cause_t` in the port: the number of the exception taken."""

    HARD_FAULT = 3
    MEM_MANAGE = 4
    BUS_FAULT = 5
    USAGE_FAULT = 6
    STALL = 16


def named(kind: type[enum.Enum], code: int) -> str:
    """A code as its name, or as itself when a newer firmware wrote one this does not know."""
    try:
        return kind(code).name.lower().replace("_", " ")
    except ValueError:
        return f"unknown ({code})"


@dataclass(frozen=True)
class Event:
    code: int
    dropped: int
    sequence: int
    time_ms: int

    def describe(self) -> str:
        line = f"+{self.time_ms / 1000:.3f} s  {named(EventKind, self.code)}"
        return f"{line}  ({self.dropped} refused before this)" if self.dropped else line


@dataclass(frozen=True)
class Crash:
    code: int
    after_sequence: int
    time_ms: int
    pc: int
    lr: int
    psr: int
    basepri: int
    cfsr: int
    fault_address: int

    def describe(self) -> str:
        kernel = "kernel locked" if self.basepri else "kernel unlocked"
        return (f"+{self.time_ms / 1000:.3f} s  {named(CrashCause, self.code).upper()}  "
                f"pc {self.pc:#010x}  lr {self.lr:#010x}  psr {self.psr:#010x}  "
                f"exception {self.psr & 0x1FF}  {kernel}  cfsr {self.cfsr:#010x}  "
                f"address {self.fault_address:#010x}")


def read_entries(node: Path, value: int, layout: str, entry: type) -> list:
    """A ring from its newest entry to its oldest, leaving out what holds nothing.

    The keyboard refuses a position past the end of the ring, which is how this knows it has
    seen the whole of it. An empty position is not the end: a write cut short leaves one.
    """
    entries = []
    for position in range(256):
        request = bytes([CUSTOM_GET_VALUE, K100_CHANNEL, value, position])
        reply = exchange(node, request)
        if reply is None:
            raise Refused(f"the keyboard did not answer for position {position}")
        if reply[0] == UNHANDLED:
            return entries
        if reply[4] != 0:
            entries.append(entry(*struct.unpack_from(layout, reply, 4)))
    return entries


def read_events(node: Path) -> list[Event]:
    return read_entries(node, via_value(HEADER, "K100_VIA_LOG_EVENT"), ">BBHI", Event)


def read_crashes(node: Path) -> list[Crash]:
    return read_entries(node, via_value(HEADER, "K100_VIA_LOG_CRASH"), ">BHIIIIBII", Crash)


def print_log(events: list[Event], crashes: list[Crash]) -> None:
    """Oldest first, with a crash after the event it came after.

    A crash whose event has been written over comes first, since nothing still held is older.
    """
    sequences = {event.sequence for event in events}
    placed = [crash for crash in crashes if crash.after_sequence in sequences]

    for crash in reversed([crash for crash in crashes if crash not in placed]):
        print(f"  {crash.describe()}")

    for event in reversed(events):
        if EventKind.BOOT.value == event.code:
            print("boot")
        print(f"  {event.describe()}")
        for crash in reversed([crash for crash in placed
                               if crash.after_sequence == event.sequence]):
            print(f"  {crash.describe()}")

    if not events and not crashes:
        print("the log is empty")


def clear(node: Path) -> bool:
    """Empties the log and reads it back, since the keyboard does it a slot per loop."""
    ask(node, bytes([CUSTOM_SET_VALUE, K100_CHANNEL, via_value(HEADER, "K100_VIA_LOG_CLEAR")]))

    deadline = time.monotonic() + CLEAR_TIMEOUT_S
    while time.monotonic() < deadline:
        if not read_events(node) and not read_crashes(node):
            return True
    return False


def hang(node: Path, how: str) -> bool:
    """Asks the main loop to stop. A build without the option refuses, and says so here."""
    reply = exchange(node, bytes([CUSTOM_SET_VALUE, K100_CHANNEL,
                                  via_value(HEADER, "K100_VIA_DIAGNOSTIC_HANG"), HANGS[how]]))
    return reply is not None and reply[0] != UNHANDLED


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--vid", default=VENDOR, help="USB vendor id, hex")
    parser.add_argument("--pid", default=PRODUCT, help="USB product id, hex")
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--clear", action="store_true", help="empty the log")
    action.add_argument("--hang", choices=HANGS,
                        help="stop the main loop, in a build with K100_DIAGNOSTIC_HANG")
    args = parser.parse_args()

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
    node, _protocol = found

    if args.hang:
        if not hang(node, args.hang):
            print("this build refused: it was not built with K100_DIAGNOSTIC_HANG",
                  file=sys.stderr)
            return 1
        print("asked. The keyboard should now stop, and then come back as the bootloader")
        return 0

    try:
        if args.clear:
            if not clear(node):
                print("the log was still not empty when this stopped looking", file=sys.stderr)
                return 1
            print("the log is empty")
            return 0

        print_log(read_events(node), read_crashes(node))
    except Refused as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
