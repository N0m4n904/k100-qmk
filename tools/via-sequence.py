#!/usr/bin/env python3
"""Sends a run of VIA commands over one open connection, and checks the order.

`via-keycode.py` and `via-lighting.py` open the hidraw node, exchange one
command and close it again, which hides a whole class of fault: the reply queue
starts empty every time, so a reply that arrives late is never seen by the
command after it. The VIA application keeps the connection open for its whole
session and sends hundreds of commands down it, and it compares every reply
against the command it sent. A reply that arrives one command late is, to the
application, a wrong answer to every question.

So this asks the question the other two cannot: **on one open connection, does
reply N belong to command N, and how long does it take?**

    python3 tools/via-sequence.py

Needs read and write on the hidraw node, so run it as root or install
udev/99-corsair-k100.rules.
"""

from __future__ import annotations

import argparse
import os
import select
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_usb import PRODUCT, VENDOR, device, identity  # noqa: E402
from k100_via import (CUSTOM_GET_VALUE, GET_PROTOCOL_VERSION, REPORT_SIZE,  # noqa: E402
                      interface)

#: The lighting channel and its four values, which is what the application asks
#: for on connecting -- and the run that exposed the fault.
RGB_MATRIX_CHANNEL = 3

COMMANDS: list[bytes] = [
    bytes([GET_PROTOCOL_VERSION]),
    *(bytes([CUSTOM_GET_VALUE, RGB_MATRIX_CHANNEL, value]) for value in (1, 2, 3, 4)),
    bytes([GET_PROTOCOL_VERSION]),
]


def pipeline(fd: int, timeout: float) -> int:
    """Every command first, then every reply.

    The receive path this keyboard uses -- SET_REPORT on the control endpoint,
    recorded for the main loop -- holds one report. A host that sends a second
    command before the loop has drained the first therefore loses one, and a
    lost command is what leaves an application one reply behind for the rest of
    its session. This is the test for that, and it is the one case the normal
    run above cannot produce.
    """
    for command in COMMANDS:
        os.write(fd, b"\x00" + command.ljust(REPORT_SIZE, b"\x00"))
    print(f"  sent {len(COMMANDS)} commands with no read in between")

    replies = []
    while True:
        ready, _, _ = select.select([fd], [], [], timeout)
        if not ready:
            break
        replies.append(os.read(fd, REPORT_SIZE))

    for reply in replies:
        print(f"  <- {reply[:6].hex(' ')}")

    print()
    print(f"  {len(COMMANDS)} commands, {len(replies)} replies")
    if len(replies) == len(COMMANDS):
        print("every command was answered, so nothing is lost by not waiting")
        return 0

    print(f"{len(COMMANDS) - len(replies)} command(s) went unanswered: the "
          f"receive path holds one report,\nand a host that does not wait "
          f"overwrites it")
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vid", default=VENDOR, help="USB vendor id, hex")
    parser.add_argument("--pid", default=PRODUCT, help="USB product id, hex")
    parser.add_argument("--timeout", type=float, default=1.0,
                        help="how long to wait for each reply, in seconds")
    parser.add_argument("--drain", action="store_true",
                        help="keep reading after the first reply, to catch a "
                             "command that is answered more than once")
    parser.add_argument("--pipeline", action="store_true",
                        help="send every command before reading any reply, "
                             "which is what a host does when it does not wait")
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
    node, _ = found

    # One open connection for the whole run, which is the point.
    fd = os.open(node, os.O_RDWR)
    try:
        if args.pipeline:
            return pipeline(fd, args.timeout)

        in_order = True
        for command in COMMANDS:
            padded = command.ljust(REPORT_SIZE, b"\x00")
            started = time.monotonic()
            os.write(fd, b"\x00" + padded)

            ready, _, _ = select.select([fd], [], [], args.timeout)
            if not ready:
                print(f"  {command.hex(' ')}  ->  nothing within {args.timeout}s")
                in_order = False
                continue

            reply = os.read(fd, REPORT_SIZE)
            waited = (time.monotonic() - started) * 1000
            echoed = reply[:len(command)] == command
            in_order &= echoed
            print(f"  {command.hex(' '):<12} -> {reply[:6].hex(' ')}  "
                  f"{waited:6.1f} ms  {'ok' if echoed else 'MISMATCH'}")

            # An answer nobody asked for is invisible to a caller that reads
            # one reply per command and then closes the node, and fatal to one
            # that keeps the connection open: every later reply is one behind.
            while args.drain:
                ready, _, _ = select.select([fd], [], [], 0.3)
                if not ready:
                    break
                extra = os.read(fd, REPORT_SIZE)
                print(f"{'':<16} -> {extra[:6].hex(' ')}  "
                      f"a second answer to the same command")
                in_order = False
    finally:
        os.close(fd)

    print()
    if in_order:
        print("every reply answered the command that asked for it")
        return 0

    print("replies do not line up with their commands, which is what the VIA\n"
          "application sees as a wrong answer to every question")
    return 1


if __name__ == "__main__":
    sys.exit(main())
