#!/usr/bin/env python3
"""Installs a firmware image in one command, with no second one to type.

Writing a new build takes two things: the keyboard has to be in Corsair's
bootloader, and then the image has to be written. Doing those as two commands
works perfectly well on a desk with a spare keyboard and not at all on a desk
with one -- the first command takes the keyboard away, and the second has to be
typed on it. So this does both, in the order that keeps the keyboard reachable:

    python3 tools/k100-update.py dist/k100-qmk-0.2.0.bin

**The image is checked before the keyboard is touched.** A container that turns
out to be unusable after the jump leaves someone in the bootloader with nothing
to type on; checked first, the same fault is a message and a keyboard that still
works.

The way into the bootloader depends on what is running, and all three are
handled rather than assumed: this project's firmware answers VIA's bootloader
jump, Corsair's own application speaks Bragi and `k100-bragi enter-bootloader`
asks it, and a keyboard already in the bootloader needs nothing. An unrecognised
version is refused instead of guessed at.

What it cannot do is rescue a keyboard whose firmware does not run: nothing on
the host can ask that keyboard anything. Hold `B` and `L` while plugging it in,
which the bootloader reads for itself, and this command then finds it already
where it needs to be. See docs/recovery.md.

Needs read and write on the hidraw node and on the USB device, so run it as root
or install udev/99-corsair-k100.rules.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_usb import (BOOTLOADER, OURS, PRODUCT, STOCK, VENDOR,  # noqa: E402
                      device, identity, program, wait_for, wait_writable)
from k100_via import ask_to_jump, interface  # noqa: E402

#: Where the writer is, if it was built where the README says to build it.
BRAGI = Path(__file__).parent / "k100-bragi" / "target" / "release" / "k100-bragi"


def run(command: list[str]) -> bool:
    """Runs one of the writer's commands, letting it do its own talking.

    The writer prints straight to the terminal, so this one's own output has to
    be flushed before handing over or the two arrive in the wrong order the
    moment anything is piped -- which is every time this is read from a log.
    """
    print(f"\n$ {' '.join(command)}", flush=True)
    return subprocess.run(command, check=False).returncode == 0


def by_via(bragi: Path, pid: str) -> bool:
    """This firmware's own way back, which is the command VIA's button sends."""
    entry = device(VENDOR, pid)
    found = interface(entry) if entry else None
    if found is None:
        print("no interface answered VIA, so there is nothing to ask", file=sys.stderr)
        return False

    ask_to_jump(found[0])
    print("asked it to jump")
    return True


def by_bragi(bragi: Path, pid: str) -> bool:
    """Corsair's own way back, for a keyboard still running Corsair's firmware."""
    return run([str(bragi), "enter-bootloader", "--device", f"0x{pid}"])


#: Which route each program answers to. A version that is not here is one this
#: command does not know how to ask, which is a refusal rather than an attempt.
#:
#: Every version of this firmware goes by VIA, including the ones that shipped
#: before it could answer that command: trying and saying the jump did not happen
#: is more use than refusing a keyboard that may well answer.
ROUTES = {**{version: by_via for version in OURS}, STOCK: by_bragi}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("image", type=Path, help="a container from tools/pack-firmware.py")
    parser.add_argument("--pid", default=PRODUCT,
                        help="the product id to act on, and the only one opened")
    parser.add_argument("--bragi", type=Path, default=BRAGI,
                        help="path to the k100-bragi binary")
    parser.add_argument("--dry-run", action="store_true",
                        help="check the image and say which route it would take")
    args = parser.parse_args()

    # For the same reason as in run(): this talks and so do the commands it
    # starts, and a reader needs them in the order they happened.
    sys.stdout.reconfigure(line_buffering=True)

    if not args.bragi.exists():
        print(f"no writer at {args.bragi}; build it or pass --bragi", file=sys.stderr)
        return 1

    # First, and before anything that changes the keyboard's state.
    if not run([str(args.bragi), "verify-image", str(args.image)]):
        print("\nthe image was not written: it did not survive its own checks",
              file=sys.stderr)
        return 1

    version, _ = identity(device(VENDOR, args.pid))
    print(f"\nrunning  bcdDevice {version}  {program(version)}")

    if version == BOOTLOADER:
        route = None
    elif version in ROUTES:
        route = ROUTES[version]
    else:
        print(f"nothing here knows how to ask {program(version)} for the bootloader; "
              f"hold B and L while plugging the keyboard in and run this again",
              file=sys.stderr)
        return 1

    if args.dry_run:
        print("would " + ("write from here, which is where it needs to be" if route is None
                          else f"reach the bootloader with {route.__name__}, then write"))
        return 0

    if route is not None:
        if not route(args.bragi, args.pid):
            return 1

        version = wait_for(BOOTLOADER, VENDOR, args.pid)
        if version != BOOTLOADER:
            print(f"it came back as bcdDevice {version} ({program(version)}), so the "
                  f"jump did not happen and nothing was written", file=sys.stderr)
            return 1
        print(f"back as bcdDevice {version}  {program(version)}")

    # Seen is not the same as usable: the device that has just appeared is the
    # bootloader's, a new one as far as the host is concerned, and the rule that
    # grants access to it is applied after it shows up rather than with it.
    if wait_writable(VENDOR, args.pid) is None:
        print("the bootloader is on the bus but its node cannot be opened for writing; "
              "install udev/99-corsair-k100.rules or run this as root", file=sys.stderr)
        return 1

    if not run([str(args.bragi), "flash", str(args.image),
                "--device", f"0x{args.pid}", "--commit"]):
        return 1

    print("\nup to date, and nothing else to run")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
