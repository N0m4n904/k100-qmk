"""Talking to the keyboard over VIA's protocol.

Shared by `via-keycode.py`, which reads and writes one keymap entry, and
`via-lighting.py`, which drives the lighting. Both need the same two things
first: which of the three HID interfaces answers VIA at all, and a way to send
one 32-byte command and read its reply.

Neither is guessable. Nothing the host can read says which interface carries
VIA, and this keyboard's interface has **no OUT endpoint** -- the one it would
normally use takes a packet and answers it but never acknowledges it in a way
the host accepts, so the interface declares none and the host sends commands as
`SET_REPORT` on the control endpoint instead. That is invisible from here: an
ordinary write to the hidraw node, which the kernel routes the only way it can.
See docs/usb-bringup.md.
"""

from __future__ import annotations

import os
import select
import sys
import time
from pathlib import Path

from k100_usb import hidraw_nodes

#: VIA's report is 32 bytes each way.
REPORT_SIZE = 32

#: Long enough that a reply which is merely late is not read as absent, short
#: enough that an interface which will never answer does not hold up the next.
TIMEOUT_S = 1.0

GET_PROTOCOL_VERSION = 0x01
GET_KEYCODE = 0x04
SET_KEYCODE = 0x05
CUSTOM_SET_VALUE = 0x07
CUSTOM_GET_VALUE = 0x08
CUSTOM_SAVE = 0x09

#: What an unsupported command comes back as, which is how an interface that is
#: not VIA's answers one.
UNHANDLED = 0xFF

#: The one report this keyboard sends without being asked for it: VIA's UI sync
#: request, which tells the application to read a menu value again after a key
#: or the wheel has changed it. No command carries this number, so a report that
#: starts with it is never a reply -- and a reader that took it for one would be
#: one reply behind for the rest of the connection. See via_ui_sync.c.
UI_SYNC_REQUEST = 0x16


def exchange(node: Path, request: bytes) -> bytes | None:
    """One command and its reply, or None when the node does not answer.

    The request is padded to the full report and prefixed with a report number
    of zero, which is what hidraw wants in front of what it sends and what
    these reports are: unnumbered.
    """
    padded = request.ljust(REPORT_SIZE, b"\x00")
    try:
        fd = os.open(node, os.O_RDWR)
    except OSError as error:
        print(f"  {node.name}: cannot be opened: {error}", file=sys.stderr)
        return None

    try:
        # Blocking, and the error is reported rather than swallowed. A write
        # the device never takes and a reply that never comes are different
        # faults -- one says the host could not deliver the command, the other
        # that the firmware did not answer it -- and a probe that cannot tell
        # them apart sends the reader looking in the wrong half.
        try:
            os.write(fd, b"\x00" + padded)
        except OSError as error:
            print(f"  {node.name}: the write was not accepted: {error}",
                  file=sys.stderr)
            return None

        # The keyboard's own reports are passed over rather than waited on
        # separately: one can land between the command and its reply, and the
        # reply is still the next thing after it.
        deadline = time.monotonic() + TIMEOUT_S
        while True:
            ready, _, _ = select.select([fd], [], [], max(deadline - time.monotonic(), 0))
            if not ready:
                return None
            reply = os.read(fd, REPORT_SIZE)
            if reply[0] != UI_SYNC_REQUEST:
                return reply
    except OSError as error:
        print(f"  {node.name}: {error}", file=sys.stderr)
        return None
    finally:
        os.close(fd)


def interface(entry: Path) -> tuple[Path, int] | None:
    """The interface that speaks VIA and its protocol version, by asking each.

    Nothing the host can read says which interface carries VIA, and guessing by
    number breaks the moment a feature is added or removed in front of it.
    """
    nodes = hidraw_nodes(entry)
    # Said out loud, because "nothing answered" and "there was nothing to ask"
    # are different faults and look identical from the outside.
    print(f"asking {len(nodes)} HID interface(s): "
          f"{' '.join(node.name for node in nodes) or 'none'}")

    for node in nodes:
        reply = exchange(node, bytes([GET_PROTOCOL_VERSION]))
        if reply:
            print(f"  {node.name} answered {reply[0]:#04x}")
        if reply and reply[0] == GET_PROTOCOL_VERSION:
            return (node, int.from_bytes(reply[1:3], "big"))
    return None
