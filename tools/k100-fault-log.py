#!/usr/bin/env python3
"""Prints what the keyboard recorded before it stopped, and can empty the record.

The keyboard stores its USB events and its crashes in EEPROM, where neither a watchdog reset nor
a power cycle clears them; see docs/qmk-layout.md. VIA's Diagnostics menu shows the newest of
each on one line, because a label cannot page. This prints all of it, oldest first, with each
crash after the event it came after.

    python3 tools/k100-fault-log.py                 # the whole log
    python3 tools/k100-fault-log.py --clear         # empty it
    python3 tools/k100-fault-log.py --hang locked   # needs K100_DIAGNOSTIC_HANG in the build

A time is seconds since the most recent boot event before that entry, and it wraps after 49.7
days of uptime. A crash records the time of the last pass through the main loop, which can be
earlier than the crash. To find the source line of a PC, run addr2line on the image that was
running:

    arm-none-eabi-addr2line -fe corsair_k100_default.elf 0x00012a3c

Needs read and write on the hidraw node, so run it as root or install
udev/99-corsair-k100.rules.
"""

from __future__ import annotations

import argparse
import enum
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

from k100_stored import K100_CHANNEL, Refused, ask
from k100_usb import PRODUCT, VENDOR, device, identity
from k100_via import (
    CUSTOM_GET_VALUE, CUSTOM_SET_VALUE, UNHANDLED, exchange, header_enum, interface, via_value,
)

ROOT = Path(__file__).resolve().parent.parent
K100_HEADER = ROOT / "qmk/keyboards/corsair/k100/k100.h"
LOG_HEADER = ROOT / "qmk/keyboards/corsair/k100/fault_log.h"
VIA_HEADER = ROOT / "qmk/keyboards/corsair/k100/via_fault_log.h"
CRASH_HEADER = ROOT / "chibios/os/hal/ports/LPC/LPC546xx/lpc546xx_crash.h"

#: Where a position in the ring is sent, and so how many there can be.
POSITIONS = 256

#: Where an entry starts in a reply: the command, the channel, the value and the position.
REPLY_PAYLOAD = 4

#: The bits of xPSR that give the exception that was running, with zero for a thread.
PSR_EXCEPTION_MASK = 0x1FF

#: A crash whose event has been overwritten. K100_LOG_NO_EVENT in fault_log.h.
NO_EVENT = 0xFFFF


def members(header: Path, enum_name: str, prefix: str) -> dict[str, int]:
    """A C enum by the names its members have without the prefix, in lower case."""
    return {name.removeprefix(prefix).lower(): number
            for name, number in header_enum(header, enum_name).items()}


EventKind = enum.Enum("EventKind", members(LOG_HEADER, "k100_log_event_kind", "K100_EVENT_"))
CrashCause = enum.Enum("CrashCause", members(CRASH_HEADER, "lpc_crash_cause", "LPC_CRASH_"))
Hang = enum.Enum("Hang", members(LOG_HEADER, "k100_hang_how", "K100_HANG_"))
ReplyStatus = enum.Enum("ReplyStatus", members(VIA_HEADER, "k100_log_reply_status", "K100_LOG_REPLY_"))


@dataclass(frozen=True)
class UnknownCode:
    """A code a newer firmware wrote and these headers do not have."""

    code: int

    @property
    def name(self) -> str:
        return f"unknown ({self.code})"


def text_of(member: enum.Enum | UnknownCode) -> str:
    return member.name.replace("_", " ")


def converted(kind: type[enum.Enum], code: int) -> enum.Enum | UnknownCode:
    try:
        return kind(code)
    except ValueError:
        return UnknownCode(code)


@dataclass(frozen=True)
class Event:
    kind: EventKind | UnknownCode
    dropped: int
    sequence: int
    time_ms: int

    @classmethod
    def parse(cls, reply: bytes) -> Event:
        code, dropped, sequence, time_ms = struct.unpack_from(">BBHI", reply, REPLY_PAYLOAD)
        return cls(converted(EventKind, code), dropped, sequence, time_ms)

    def describe(self) -> str:
        line = f"+{self.time_ms / 1000:.3f} s  {text_of(self.kind)}"
        return f"{line}  ({self.dropped} not written before this)" if self.dropped else line


@dataclass(frozen=True)
class Crash:
    cause: CrashCause | UnknownCode
    after_sequence: int
    time_ms: int
    pc: int
    lr: int
    psr: int
    basepri: int
    cfsr: int
    fault_address: int

    @classmethod
    def parse(cls, reply: bytes) -> Crash:
        code, *fields = struct.unpack_from(">BHIIIIBII", reply, REPLY_PAYLOAD)
        return cls(converted(CrashCause, code), *fields)

    def describe(self) -> str:
        kernel = "kernel locked" if self.basepri else "kernel unlocked"
        return (f"+{self.time_ms / 1000:.3f} s  {text_of(self.cause).upper()}  "
                f"pc {self.pc:#010x}  lr {self.lr:#010x}  psr {self.psr:#010x}  "
                f"exception {self.psr & PSR_EXCEPTION_MASK}  {kernel}  cfsr {self.cfsr:#010x}  "
                f"address {self.fault_address:#010x}")


def read_entries(node: Path, value: int, parse) -> list:
    """A ring from its newest entry to its oldest, leaving out empty positions.

    The reply says when a position is past the end of the ring. An empty position is not the
    end: a write cut short leaves one.
    """
    entries = []
    for position in range(POSITIONS):
        reply = ask(node, bytes([CUSTOM_GET_VALUE, K100_CHANNEL, value, position]))
        status = reply[REPLY_PAYLOAD]
        if status == ReplyStatus.past_end.value:
            break
        if status != ReplyStatus.empty.value:
            entries.append(parse(reply))
    return entries


def read_events(node: Path) -> list[Event]:
    return read_entries(node, via_value(K100_HEADER, "K100_VIA_LOG_EVENT"), Event.parse)


def read_crashes(node: Path) -> list[Crash]:
    return read_entries(node, via_value(K100_HEADER, "K100_VIA_LOG_CRASH"), Crash.parse)


def print_log(events: list[Event], crashes: list[Crash]) -> None:
    """Oldest first, with a crash after the event it came after.

    A crash whose event has been written over comes first, since nothing still in the event ring
    is older.
    """
    sequences = {event.sequence for event in events}

    for crash in reversed(crashes):
        if crash.after_sequence == NO_EVENT or crash.after_sequence not in sequences:
            print(f"  {crash.describe()}")

    for event in reversed(events):
        if event.kind == EventKind.boot:
            print("boot")
        print(f"  {event.describe()}")
        for crash in reversed(crashes):
            if crash.after_sequence == event.sequence:
                print(f"  {crash.describe()}")

    if not events and not crashes:
        print("the log is empty")


def clear(node: Path) -> None:
    """Empties the log and waits for the keyboard to say it has, a slot per main loop pass."""
    value = via_value(K100_HEADER, "K100_VIA_LOG_CLEAR")

    ask(node, bytes([CUSTOM_SET_VALUE, K100_CHANNEL, value]))
    while ask(node, bytes([CUSTOM_GET_VALUE, K100_CHANNEL, value]))[3] != 0:
        pass


class HangOutcome(enum.Enum):
    ASKED = enum.auto()
    NOT_BUILT_IN = enum.auto()
    NO_ANSWER = enum.auto()


def hang(node: Path, how: Hang) -> HangOutcome:
    """Asks the main loop to stop."""
    reply = exchange(node, bytes([CUSTOM_SET_VALUE, K100_CHANNEL,
                                  via_value(K100_HEADER, "K100_VIA_DIAGNOSTIC_HANG"), how.value]))
    if reply is None:
        return HangOutcome.NO_ANSWER
    return HangOutcome.NOT_BUILT_IN if reply[0] == UNHANDLED else HangOutcome.ASKED


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--vid", default=VENDOR, help="USB vendor id, hex")
    parser.add_argument("--pid", default=PRODUCT, help="USB product id, hex")
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--clear", action="store_true", help="empty the log")
    action.add_argument("--hang", choices=[how.name for how in Hang],
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
        outcome = hang(node, Hang[args.hang])
        if outcome is HangOutcome.NOT_BUILT_IN:
            print("this build does not support it: it was not built with K100_DIAGNOSTIC_HANG",
                  file=sys.stderr)
        elif outcome is HangOutcome.NO_ANSWER:
            print("the keyboard did not answer the request", file=sys.stderr)
        else:
            print("request sent. The keyboard should stop, then come back as the bootloader")
        return 0 if outcome is HangOutcome.ASKED else 1

    try:
        if args.clear:
            clear(node)
            print("the log is empty")
            return 0

        print_log(read_events(node), read_crashes(node))
    except Refused as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
