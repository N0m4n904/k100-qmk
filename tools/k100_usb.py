"""Which of the three programs the keyboard is running, read from the USB bus.

Shared by `watch-input.py`, which attributes keystrokes, and
`watch-bootloader-route.py`, which watches the keyboard hand itself over. Both
need the same fact and it is the one that is easy to get wrong: the bootloader,
the stock application and this project's firmware all answer to `1b1c:1b7c`,
deliberately, so that udev rules and host tooling keep working across all three.

`bcdDevice` is the only field that separates them, and nothing else does --
Corsair's bootloader presents a working keyboard of its own, so even typing on
it proves nothing. A whole 104-key sweep was once collected from the bootloader
and credited to this firmware. See docs/recovery.md.
"""

from __future__ import annotations

import os
import time
from pathlib import Path

VENDOR = "1b1c"
PRODUCT = "1b7c"

# `bcdDevice`, as sysfs spells it: four hex digits, BCD, so 0.2.1 reads 0021.
# Ours is declared in tools/generate-qmk-layout.py and reaches the descriptor
# through keyboard.json; the other two belong to Corsair and are observed.
#
# The versions this project has already shipped are kept rather than replaced.
# A keyboard that has not been updated is still this firmware, and a tool that
# only knew the newest number would call it unrecognised and refuse to work with
# it -- which is the opposite of what a keyboard waiting for an update needs.
CURRENT = "0021"
EARLIER = ("0020", "0010", "0001")
OURS = (CURRENT, *EARLIER)

BOOTLOADER = "000a"
STOCK = "0023"

PROGRAMS = {
    CURRENT: "this project's firmware",
    **{version: "this project's firmware, an earlier build" for version in EARLIER},
    BOOTLOADER: "Corsair's bootloader",
    STOCK: "Corsair's application",
}


def is_ours(version: str) -> bool:
    """Whether that version is a build of this project, of any age."""
    return version in OURS

# What a reading means when the device is not on the bus at all. Kept here
# rather than spelled out at each call site, so that "gone" is one value
# everything compares against rather than a string each tool invents.
ABSENT = ""


def device(vid: str = VENDOR, pid: str = PRODUCT) -> Path | None:
    """The keyboard's sysfs directory, or None when it is not on the bus."""
    for entry in Path("/sys/bus/usb/devices").glob("*"):
        try:
            if ((entry / "idVendor").read_text().strip().lower() == vid.lower()
                    and (entry / "idProduct").read_text().strip().lower()
                    == pid.lower()):
                return entry
        except OSError:
            continue
    return None


def hidraw_nodes(entry: Path) -> list[Path]:
    """Every /dev/hidraw this device offers, in interface order.

    Which one carries which protocol is not written down anywhere the host can
    read, so this returns them all and leaves the caller to ask.
    """
    found = []
    for interface in sorted(entry.glob(f"{entry.name}:*")):
        for hidraw in sorted(interface.glob("*/hidraw/hidraw*")):
            found.append(Path("/dev") / hidraw.name)
    return found


def identity(entry: Path | None) -> tuple[str, str]:
    """The firmware version and product string the device reports right now."""
    if entry is None:
        return (ABSENT, "not on the bus")
    try:
        return ((entry / "bcdDevice").read_text().strip(),
                (entry / "product").read_text().strip())
    except OSError:
        return (ABSENT, "gone from the bus")


def program(version: str) -> str:
    """What that version means, in words, including when it means nothing."""
    if version == ABSENT:
        return "nothing on the bus"
    return PROGRAMS.get(version, f"an unrecognised program, bcdDevice {version}")


def usbfs_node(entry: Path | None) -> Path | None:
    """The /dev/bus/usb node for a device, which is what usbdevfs is opened on."""
    if entry is None:
        return None
    try:
        bus = int((entry / "busnum").read_text())
        address = int((entry / "devnum").read_text())
    except OSError:
        return None
    return Path(f"/dev/bus/usb/{bus:03d}/{address:03d}")


def wait_writable(vid: str = VENDOR, pid: str = PRODUCT,
                  timeout: float = 5.0, poll: float = 0.1) -> Path | None:
    """Waits until the device's node can be opened for writing.

    Being on the bus and being usable are not the same moment. A device that has
    just re-enumerated appears in sysfs before udev has finished applying the
    rule that grants access to it, so a command that starts the instant it is
    seen is refused -- which is a race a human never loses, because a human takes
    a second to type the next command, and a script loses every time.
    """
    deadline = time.monotonic() + timeout
    while True:
        node = usbfs_node(device(vid, pid))
        if node is not None and os.access(node, os.W_OK):
            return node
        if time.monotonic() >= deadline:
            return None
        time.sleep(poll)


def wait_for(version: str, vid: str = VENDOR, pid: str = PRODUCT,
             timeout: float = 10.0, poll: float = 0.25) -> str:
    """Waits for the device to come back reporting a given version.

    Returns what it reports at the end, which is the version asked for when the
    wait succeeded and whatever it is reporting instead when it did not -- a
    caller can then say *what* came back rather than only that something else
    did. Long enough by default for a reset, a re-enumeration and the
    bootloader's own startup.
    """
    deadline = time.monotonic() + timeout
    while True:
        reading, _ = identity(device(vid, pid))
        if reading == version or time.monotonic() >= deadline:
            return reading
        time.sleep(poll)
