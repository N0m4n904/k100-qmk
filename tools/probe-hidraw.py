#!/usr/bin/env python3
"""Read-only Bragi probe for the Corsair K100, over /dev/hidraw with no dependencies.

Answers three things that cannot be determined from static analysis, because they come from
the device at runtime:

  * which HID interface and report framing actually carry Bragi,
  * the physical layout (HWLAYOUT), which confirms which of the five firmware mask groups
    belongs to this keyboard, and
  * the firmware and bootloader versions and current operating mode.

Every request issued by default is a Bragi GET. Nothing is written to the device's
configuration, no handle is opened and no mode is changed. See docs/recovery.md.

Needs read/write on the hidraw node, so run as root or add a udev rule for 1b1c.

    sudo python3 tools/probe-hidraw.py

Deliberately prints raw response bytes rather than only its own interpretation: the framing
is what is in question, so the evidence matters more than the verdict.
"""

from __future__ import annotations

import argparse
import os
import select
import sys
from dataclasses import dataclass
from pathlib import Path

CORSAIR_VID = 0x1B1C
K100_PIDS = {0x1B7C: "K100 RGB (OPX optical)",
             0x1B7D: "K100 RGB (Cherry MX Speed)",
             0x1BC5: "K100 RGB (OPX optical, later variant)"}

BRAGI_MAGIC = 0x08

CMD_GET = 0x02
CMD_DESCRIBE_BUFFER = 0x09

# Properties worth reading. Values cross-checked between ckb-next's bragi_proto.h and
# Corsair's own web updater bundle.
PROPERTIES = [
    (0x11, "USB_VID"),
    (0x12, "USB_PID"),
    (0x13, "FIRMWARE_VERSION"),
    (0x14, "BOOTLOADER_VERSION"),
    (0x03, "OPERATING_MODE"),
    (0x41, "HWLAYOUT"),
]

OPERATING_MODES = {0: "RESERVED", 1: "SELF_OPERATED", 2: "HOST_CONTROLLED",
                   3: "BOOTLOADER", 4: "SLEEP"}

# ckb-next's physical layout enum, src/daemon/structures.h. The firmware reports zero-based,
# which ckb-next converts by adding one, so both readings are shown.
LAYOUTS = {1: "ANSI", 2: "ISO", 3: "ABNT", 4: "JIS", 5: "DUBEOLSIK"}

READ_TIMEOUT_S = 1.0


@dataclass(frozen=True)
class Framing:
    """How a request is laid out on the wire.

    `report_id` is the leading byte Linux hidraw requires: 0 for a device that does not use
    numbered reports, in which case BRAGI_MAGIC is the first payload byte instead.
    """

    report_id: int
    size: int

    def encode(self, command: int, prop: int, payload: bytes = b"") -> bytes:
        body = bytes([BRAGI_MAGIC, command, prop]) + payload
        return bytes([self.report_id]) + body.ljust(self.size, b"\0")

    def __str__(self) -> str:
        return f"report_id=0x{self.report_id:02x} size={self.size}"


CANDIDATES = [Framing(0x00, 1024), Framing(0x00, 64),
              Framing(0x08, 1024), Framing(0x08, 64)]


@dataclass
class Node:
    path: Path
    vid: int
    pid: int
    name: str

    @property
    def model(self) -> str:
        return K100_PIDS.get(self.pid, f"unknown product {self.pid:#06x}")


def discover() -> list[Node]:
    """Finds Corsair K100 hidraw nodes by reading HID_ID out of sysfs."""
    found = []
    for entry in sorted(Path("/sys/class/hidraw").glob("hidraw*")):
        uevent = entry / "device" / "uevent"
        try:
            fields = dict(
                line.split("=", 1) for line in uevent.read_text().splitlines() if "=" in line
            )
        except OSError:
            continue

        # HID_ID looks like "0003:00001B1C:00001B7C"
        parts = fields.get("HID_ID", "").split(":")
        if len(parts) != 3:
            continue
        try:
            vid, pid = int(parts[1], 16), int(parts[2], 16)
        except ValueError:
            continue

        if vid == CORSAIR_VID and pid in K100_PIDS:
            found.append(Node(Path("/dev") / entry.name, vid, pid,
                             fields.get("HID_NAME", "?")))
    return found


def exchange(fd: int, request: bytes) -> tuple[bytes | None, str | None]:
    """Writes one report and reads the reply.

    Returns (response, error). A failed write and a silent device are different problems and
    must not look alike, so the errno is reported rather than swallowed.
    """
    try:
        written = os.write(fd, request)
    except OSError as error:
        return None, f"write failed: {error.strerror} (errno {error.errno})"

    if written != len(request):
        return None, f"short write: {written} of {len(request)} bytes"

    ready, _, _ = select.select([fd], [], [], READ_TIMEOUT_S)
    if not ready:
        return None, "no reply within timeout"
    try:
        return os.read(fd, 1024), None
    except OSError as error:
        return None, f"read failed: {error.strerror} (errno {error.errno})"


def report_descriptor(node: Node) -> bytes | None:
    """The device's own declaration of its report IDs and sizes, from sysfs."""
    path = Path("/sys/class/hidraw") / node.path.name / "device" / "report_descriptor"
    try:
        return path.read_bytes()
    except OSError:
        return None


def describe_reports(descriptor: bytes) -> list[str]:
    """Minimal HID descriptor walk, enough to list report IDs and input/output sizes.

    Only the items needed here are decoded: Report ID (0x85), Report Size (0x75),
    Report Count (0x95), and the Input/Output/Feature main items.
    """
    out, i = [], 0
    report_id, size, count = 0, 0, 0
    kinds = {0x81: "Input", 0x91: "Output", 0xB1: "Feature"}

    while i < len(descriptor):
        item = descriptor[i]
        length = item & 0x03
        if length == 3:
            length = 4
        data = descriptor[i + 1: i + 1 + length]
        value = int.from_bytes(data, "little") if data else 0
        tag = item & 0xFC

        if tag == 0x84:
            report_id = value
        elif tag == 0x74:
            size = value
        elif tag == 0x94:
            count = value
        elif tag in (0x80, 0x90, 0xB0):
            bits = size * count
            out.append(f"report id {report_id:#04x}  {kinds.get(tag | 0x01, '?'):7} "
                       f"{count} x {size} bits = {bits // 8} bytes")
        i += 1 + length
    return out


def plausible(response: bytes, command: int) -> bool:
    """A coherent reply echoes the magic and the command in its first bytes."""
    return len(response) >= 4 and BRAGI_MAGIC in response[:2] and command in response[1:3]


def find_framing(fd: int) -> tuple[Framing | None, bytes | None, list[str]]:
    """Tries each candidate framing, returning the first coherent one plus a per-attempt log."""
    log = []
    for framing in CANDIDATES:
        response, error = exchange(fd, framing.encode(CMD_GET, 0x13))
        if error:
            log.append(f"{framing}: {error}")
        elif plausible(response, CMD_GET):
            return framing, response, log
        else:
            log.append(f"{framing}: replied but incoherent: {hexdump(response, 12)}")
    return None, None, log


def hexdump(data: bytes, count: int = 16) -> str:
    return " ".join(f"{b:02x}" for b in data[:count])


def interpret(response: bytes) -> str:
    """Shows the candidate value under both header layouts, since which applies is the
    question. TWO_BYTE puts the payload at index 3, FOUR_BYTE at index 5."""
    out = []
    for label, offset in (("two-byte", 3), ("four-byte", 5)):
        if len(response) >= offset + 2:
            value = response[offset] | (response[offset + 1] << 8)
            out.append(f"{label}={value:#06x} ({value})")
    return "  ".join(out)


def probe(node: Node, sweep: bool, write_test: bool) -> bool:
    print(f"{node.path}  {node.model}")
    print(f"  HID_NAME            {node.name}")

    descriptor = report_descriptor(node)
    if descriptor:
        print(f"  descriptor          {len(descriptor)} bytes: {hexdump(descriptor, 24)}")
        for line in describe_reports(descriptor):
            print(f"  {'':20} {line}")
    else:
        print("  descriptor          unreadable")

    try:
        fd = os.open(node.path, os.O_RDWR | os.O_NONBLOCK)
    except PermissionError:
        print("  cannot open         permission denied; run as root\n")
        return False
    except OSError as error:
        print(f"  cannot open         {error}\n")
        return False

    try:
        if write_test:
            print("  write-size test (GET FIRMWARE_VERSION at each length):")
            for size in (1024, 512, 256, 128, 64, 32, 16, 8, 4):
                body = bytes([BRAGI_MAGIC, CMD_GET, 0x13]).ljust(size, b"\0")
                for prefix, label in ((b"\x00", "with 0x00 report-id prefix"),
                                      (b"", "no prefix")):
                    response, error = exchange(fd, prefix + body)
                    outcome = error or f"reply {hexdump(response, 10)}"
                    print(f"    {size:4} bytes {label:28} {outcome}")
            print()

        framing, first, log = find_framing(fd)
        if not framing:
            print("  no Bragi response under any candidate framing:")
            for line in log:
                print(f"    {line}")
            print()
            return False

        print(f"  framing             {framing}")
        print(f"  first reply         {hexdump(first)}")
        print()

        for prop, label in PROPERTIES:
            response, error = exchange(fd, framing.encode(CMD_GET, prop))
            if error:
                print(f"  {label:20} {error}")
                continue

            print(f"  {label:20} {hexdump(response, 10)}")
            print(f"  {'':20} {interpret(response)}")

            if label == "OPERATING_MODE":
                for offset in (3, 5):
                    if len(response) > offset and response[offset] in OPERATING_MODES:
                        print(f"  {'':20} -> {OPERATING_MODES[response[offset]]} "
                              f"(payload at {offset})")
            if label == "HWLAYOUT":
                for offset in (3, 5):
                    if len(response) > offset:
                        raw = response[offset]
                        print(f"  {'':20} -> raw {raw} at offset {offset}: "
                              f"as-is {LAYOUTS.get(raw, '?')}, "
                              f"+1 {LAYOUTS.get(raw + 1, '?')}")

        if sweep:
            print()
            print("  DESCRIBE_BUFFER sweep (buffer indices 0..7):")
            for index in range(8):
                response, error = exchange(fd, framing.encode(CMD_DESCRIBE_BUFFER, 0x00,
                                                              bytes([index])))
                print(f"    buffer {index}  {error or hexdump(response, 12)}")
        print()
        return True
    finally:
        os.close(fd)


def cross_probe(nodes: list[Node]) -> bool:
    """Writes a query on one interface and watches all of them for the answer.

    ckb-next notes that a Bragi response can arrive on a different endpoint than the one the
    command was sent to, so testing each interface in isolation can miss a working path.
    """
    handles: dict[str, int] = {}
    for node in nodes:
        try:
            handles[node.path.name] = os.open(node.path, os.O_RDWR | os.O_NONBLOCK)
        except OSError as error:
            print(f"  {node.path.name}: cannot open ({error})")

    if not handles:
        return False

    # Drain anything already queued so a stale report is not mistaken for a reply.
    for fd in handles.values():
        while select.select([fd], [], [], 0)[0]:
            try:
                os.read(fd, 1024)
            except OSError:
                break

    answered = False
    try:
        for name, fd in handles.items():
            for framing in CANDIDATES:
                request = framing.encode(CMD_GET, 0x13)
                try:
                    os.write(fd, request)
                except OSError as error:
                    print(f"  write on {name} ({framing}): {error.strerror}")
                    continue

                waiting = list(handles.values())
                ready, _, _ = select.select(waiting, [], [], READ_TIMEOUT_S)
                if not ready:
                    print(f"  write on {name} ({framing}): accepted, no reply anywhere")
                    continue

                for reply_fd in ready:
                    reply_name = next(n for n, f in handles.items() if f == reply_fd)
                    try:
                        data = os.read(reply_fd, 1024)
                    except OSError:
                        continue
                    print(f"  write on {name} ({framing}) -> reply on {reply_name}: "
                          f"{hexdump(data, 12)}")
                    print(f"  {'':20} {interpret(data)}")
                    answered = True
    finally:
        for fd in handles.values():
            os.close(fd)
    return answered


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cross", action="store_true",
                        help="send a query on each interface and watch every interface for "
                             "the reply, since Corsair routes responses to a different "
                             "endpoint than the command went out on")
    parser.add_argument("--write-test", action="store_true",
                        help="write a GET at several report lengths and report the errno for "
                             "each, to isolate whether a rejected write is size-dependent")
    parser.add_argument("--sweep", action="store_true",
                        help="also issue DESCRIBE_BUFFER per buffer index. Still a read, but "
                             "it asks about state we have not characterised, so it is opt-in.")
    args = parser.parse_args()

    nodes = discover()
    if not nodes:
        print("No K100 hidraw node found.", file=sys.stderr)
        print(f"Expected Corsair VID {CORSAIR_VID:#06x} with one of "
              + ", ".join(f"{p:#06x}" for p in K100_PIDS), file=sys.stderr)
        return 1

    print(f"{len(nodes)} candidate interface(s)\n")

    if args.cross:
        print("cross-interface probe: query on one node, watch all nodes for the reply\n")
        return 0 if cross_probe(nodes) else 1

    return 0 if any(probe(node, args.sweep, args.write_test) for node in nodes) else 1


if __name__ == "__main__":
    raise SystemExit(main())
