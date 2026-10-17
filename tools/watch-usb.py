#!/usr/bin/env python3
"""Watches for Corsair USB devices appearing, vanishing or changing identity.

Written for catching a device that enumerates only briefly — a reset or a bootloader mode
that hands straight back to the application would be invisible to a manually run `lsusb`.

Polls sysfs, which needs no privileges and no dependencies. The default interval catches
anything present for longer than one poll, and USB enumeration takes far longer than that.

    python3 tools/watch-usb.py --seconds 90
"""

from __future__ import annotations

import argparse
import time
from pathlib import Path

CORSAIR_VID = 0x1B1C

# Known identities, so a *new* one stands out as the interesting case.
KNOWN = {
    0x1B7C: "K100 RGB (OPX optical)",
    0x1B7D: "K100 RGB (Cherry MX Speed)",
    0x1BC5: "K100 RGB (OPX optical, later variant)",
}


def snapshot(vendor: int | None) -> dict[str, dict[str, str]]:
    """Every matching USB device right now, keyed by its sysfs name."""
    found = {}
    for entry in Path("/sys/bus/usb/devices").glob("*"):
        def read(name: str) -> str | None:
            try:
                return (entry / name).read_text().strip()
            except OSError:
                return None

        vid = read("idVendor")
        if vid is None:
            continue
        if vendor is not None and int(vid, 16) != vendor:
            continue

        found[entry.name] = {
            "vid": vid,
            "pid": read("idProduct") or "?",
            "product": read("product") or "",
            "manufacturer": read("manufacturer") or "",
            "serial": read("serial") or "",
            "class": read("bDeviceClass") or "",
            "interfaces": read("bNumInterfaces") or "",
            # Which program is running, with no command sent: the bootloader,
            # the stock application and this project's firmware each report a
            # different one under the same vendor and product id.
            "version": read("bcdDevice") or "",
        }
    return found


def describe(name: str, info: dict[str, str]) -> str:
    pid = int(info["pid"], 16) if info["pid"] != "?" else 0
    label = KNOWN.get(pid, "*** UNRECOGNISED IDENTITY ***")
    return (f"{name}  {info['vid']}:{info['pid']}  bcdDevice={info['version']}  {label}\n"
            f"        product={info['product']!r} class={info['class']} "
            f"interfaces={info['interfaces']} serial={info['serial']!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seconds", type=float, default=90.0,
                        help="how long to watch (default 90)")
    parser.add_argument("--interval", type=float, default=0.02,
                        help="poll interval in seconds (default 0.02)")
    parser.add_argument("--all-vendors", action="store_true",
                        help="watch every USB device, not only Corsair ones")
    args = parser.parse_args()

    vendor = None if args.all_vendors else CORSAIR_VID
    started = time.monotonic()

    previous = snapshot(vendor)
    print(f"watching for {args.seconds:g}s at {args.interval * 1000:g}ms intervals; "
          f"{len(previous)} device(s) present at start")
    for name, info in sorted(previous.items()):
        print(f"  {describe(name, info)}")
    print("\n--- changes ---", flush=True)

    changes = 0
    while time.monotonic() - started < args.seconds:
        current = snapshot(vendor)
        if current != previous:
            stamp = time.monotonic() - started
            for name in sorted(set(previous) - set(current)):
                print(f"[{stamp:7.3f}s] GONE     {describe(name, previous[name])}", flush=True)
            for name in sorted(set(current) - set(previous)):
                print(f"[{stamp:7.3f}s] APPEARED {describe(name, current[name])}", flush=True)
            for name in sorted(set(current) & set(previous)):
                if current[name] != previous[name]:
                    print(f"[{stamp:7.3f}s] CHANGED  {describe(name, current[name])}", flush=True)
            changes += 1
            previous = current
        time.sleep(args.interval)

    print(f"\n--- done, {changes} change event(s) ---")
    if not changes:
        print("Nothing appeared or vanished. Either the device never re-enumerated, or it")
        print("came back with an identical identity, which a reset that keeps the same")
        print("product ID would do.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
