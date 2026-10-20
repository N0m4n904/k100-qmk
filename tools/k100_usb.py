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

from pathlib import Path

VENDOR = "1b1c"
PRODUCT = "1b7c"

OURS = "0001"
BOOTLOADER = "000a"
STOCK = "0023"

PROGRAMS = {
    OURS: "this project's firmware",
    BOOTLOADER: "Corsair's bootloader",
    STOCK: "Corsair's application",
}

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
