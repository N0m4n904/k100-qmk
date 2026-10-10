#!/usr/bin/env python3
"""Read-only Bragi probe for the Corsair K100 over usbdevfs, with no dependencies.

hidraw cannot drive this keyboard and neither can hidapi's libusb backend; both have their
writes to the Bragi interface rejected. ckb-next talks to it through usbdevfs ioctls on
/dev/bus/usb/..., detaching the kernel driver and claiming the interface first, and that is
what this reproduces. See the Linux transport section of docs/bragi-firmware-update.md.

No elevated privileges are needed: the usbfs node carries an ACL granting the logged-in user
read/write, which is how OpenRGB and ckb-next operate unprivileged.

Only Bragi GETs are issued. No handle is opened, no mode is changed, nothing is written to
the keyboard's configuration. The kernel driver is detached from the *vendor* interface only,
never from the typing interface, and is reattached before exit even on failure.
"""

from __future__ import annotations

import argparse
import ctypes
import fcntl
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

CORSAIR_VID = 0x1B1C
K100_PIDS = {0x1B7C: "K100 RGB (OPX optical)",
             0x1B7D: "K100 RGB (Cherry MX Speed)",
             0x1BC5: "K100 RGB (OPX optical, later variant)"}

BRAGI_MAGIC = 0x08
CMD_GET = 0x02

PROPERTIES = [(0x11, "USB_VID"), (0x12, "USB_PID"), (0x13, "FIRMWARE_VERSION"),
              (0x14, "BOOTLOADER_VERSION"), (0x03, "OPERATING_MODE"), (0x41, "HWLAYOUT")]

OPERATING_MODES = {0: "RESERVED", 1: "SELF_OPERATED", 2: "HOST_CONTROLLED",
                   3: "BOOTLOADER", 4: "SLEEP"}

# ckb-next's physical layout enum (src/daemon/structures.h). The firmware's own numbering is
# not known to match, so both the raw value and value+1 are shown.
LAYOUTS = {1: "ANSI", 2: "ISO", 3: "ABNT", 4: "JIS", 5: "DUBEOLSIK"}

# Endpoints from ckb-next's usb_bragi.c for the K100, corroborated by lsusb.
BRAGI_INTERFACE = 1
BRAGI_EP_OUT = 0x01
BRAGI_EP_IN = 0x82
BRAGI_REPORT_SIZE = 1024

TIMEOUT_MS = 5000  # matches ckb-next


# ioctl encoding, from asm-generic/ioctl.h. Computed rather than hardcoded so the sizes come
# from the structs themselves.
_IOC_NRBITS, _IOC_TYPEBITS, _IOC_SIZEBITS = 8, 8, 14
_IOC_NONE, _IOC_WRITE, _IOC_READ = 0, 1, 2


def _ioc(direction: int, kind: str, number: int, size: int) -> int:
    return ((direction << (_IOC_NRBITS + _IOC_TYPEBITS + _IOC_SIZEBITS))
            | (size << (_IOC_NRBITS + _IOC_TYPEBITS))
            | (ord(kind) << _IOC_NRBITS)
            | number)


# struct usbdevfs_bulktransfer { unsigned ep, len, timeout; void *data; }
BULK_LAYOUT = "IIIP"
BULK_SIZE = struct.calcsize(BULK_LAYOUT)

# struct usbdevfs_ioctl { int ifno, ioctl_code; void *data; }
IOCTL_LAYOUT = "iiP"
IOCTL_SIZE = struct.calcsize(IOCTL_LAYOUT)

# struct usbdevfs_getdriver { unsigned interface; char driver[256]; }
GETDRIVER_LAYOUT = "I256s"
GETDRIVER_SIZE = struct.calcsize(GETDRIVER_LAYOUT)

USBDEVFS_BULK = _ioc(_IOC_READ | _IOC_WRITE, "U", 2, BULK_SIZE)
USBDEVFS_GETDRIVER = _ioc(_IOC_WRITE, "U", 8, GETDRIVER_SIZE)
USBDEVFS_CLAIMINTERFACE = _ioc(_IOC_READ, "U", 15, 4)
USBDEVFS_RELEASEINTERFACE = _ioc(_IOC_READ, "U", 16, 4)
USBDEVFS_IOCTL = _ioc(_IOC_READ | _IOC_WRITE, "U", 18, IOCTL_SIZE)
USBDEVFS_CLEAR_HALT = _ioc(_IOC_READ, "U", 21, 4)

# These two are passed as the ioctl_code inside a usbdevfs_ioctl, not used directly.
USBDEVFS_DISCONNECT = _ioc(_IOC_NONE, "U", 22, 0)
USBDEVFS_CONNECT = _ioc(_IOC_NONE, "U", 23, 0)


@dataclass
class Found:
    node: Path
    vid: int
    pid: int
    sysfs: Path

    @property
    def model(self) -> str:
        return K100_PIDS.get(self.pid, f"unknown product {self.pid:#06x}")


def discover() -> list[Found]:
    """Locates the keyboard's usbfs node via sysfs bus/dev numbers."""
    found = []
    for entry in sorted(Path("/sys/bus/usb/devices").glob("*")):
        try:
            vid = int((entry / "idVendor").read_text().strip(), 16)
            pid = int((entry / "idProduct").read_text().strip(), 16)
            bus = int((entry / "busnum").read_text().strip())
            dev = int((entry / "devnum").read_text().strip())
        except (OSError, ValueError):
            continue

        if vid == CORSAIR_VID and pid in K100_PIDS:
            found.append(Found(Path(f"/dev/bus/usb/{bus:03d}/{dev:03d}"), vid, pid, entry))
    return found


class UsbDevice:
    """A usbfs handle owning a set of interfaces.

    ckb-next claims *every* interface, not only the vendor one (`usbclaim()` loops over all
    of them issuing DISCONNECT then CLAIMINTERFACE), so this does the same. Detaching
    interface 0 stops the keyboard typing for the duration; everything is restored on exit.
    """

    def __init__(self, node: Path, interfaces: list[int]):
        self.node = node
        self.interfaces = interfaces
        self.fd = None
        self.detached: list[int] = []
        self.claimed: list[int] = []

    def __enter__(self) -> "UsbDevice":
        self.fd = open(self.node, "rb+", buffering=0)
        return self

    def __exit__(self, *_) -> None:
        # Always put every interface back the way it was found.
        for interface in reversed(self.claimed):
            try:
                fcntl.ioctl(self.fd, USBDEVFS_RELEASEINTERFACE,
                            struct.pack("I", interface), True)
            except OSError as error:
                print(f"  releasing interface {interface} failed: {error}")

        for interface in reversed(self.detached):
            try:
                self._driver_ioctl(interface, USBDEVFS_CONNECT)
            except OSError as error:
                print(f"  reattaching interface {interface} failed: {error}")

        if self.detached:
            print(f"  kernel driver reattached to interface(s) "
                  f"{', '.join(map(str, self.detached))}")
        if self.fd:
            self.fd.close()

    def _driver_ioctl(self, interface: int, code: int) -> None:
        request = bytearray(struct.pack(IOCTL_LAYOUT, interface, code, 0))
        fcntl.ioctl(self.fd, USBDEVFS_IOCTL, request, True)

    def current_driver(self, interface: int) -> str | None:
        request = bytearray(struct.pack(GETDRIVER_LAYOUT, interface, b""))
        try:
            fcntl.ioctl(self.fd, USBDEVFS_GETDRIVER, request, True)
        except OSError:
            return None
        _, name = struct.unpack(GETDRIVER_LAYOUT, bytes(request))
        return name.split(b"\0", 1)[0].decode(errors="replace") or None

    def take(self) -> None:
        """Detaches the kernel driver from each interface and claims it."""
        for interface in self.interfaces:
            driver = self.current_driver(interface)
            if driver:
                self._driver_ioctl(interface, USBDEVFS_DISCONNECT)
                self.detached.append(interface)
            fcntl.ioctl(self.fd, USBDEVFS_CLAIMINTERFACE,
                        struct.pack("I", interface), True)
            self.claimed.append(interface)
            print(f"  interface {interface}: driver {driver or 'none'}"
                  f"{' detached,' if driver else ','} claimed")

    def transfer(self, endpoint: int, data: bytes | None, length: int) -> bytes:
        """One bulk/interrupt transfer. usbdevfs routes by endpoint type, so the same ioctl
        serves the interrupt endpoints this device uses."""
        buffer = ctypes.create_string_buffer(data if data is not None else b"", length)
        request = bytearray(struct.pack(BULK_LAYOUT, endpoint, length, TIMEOUT_MS,
                                        ctypes.addressof(buffer)))
        transferred = fcntl.ioctl(self.fd, USBDEVFS_BULK, request, True)
        return buffer.raw[:transferred]

    def clear_halt(self, endpoint: int) -> None:
        fcntl.ioctl(self.fd, USBDEVFS_CLEAR_HALT, struct.pack("I", endpoint), True)


def bragi_request(command: int, prop: int, payload: bytes = b"") -> bytes:
    body = bytes([BRAGI_MAGIC, command, prop]) + payload
    return body.ljust(BRAGI_REPORT_SIZE, b"\0")


CMD_DESCRIBE_BUFFER = 0x09
CMD_PING = 0x12
CMD_OPEN_FILE = 0x0D
CMD_UNBIND_BUFFERS = 0x05

# File IDs, from the `Ft` enum in Corsair's web updater bundle. FIRMWARE is deliberately
# never opened here: the ID is already known and opening it may imply write intent.
FILE_IDS = {0x01: "LIGHTING", 0x22: "ALT_LIGHTING", 3: "FIRMWARE", 5: "PAIRING_ID", 6: "WIRELESS_ENCRYPTION_KEY",
            18: "SECONDARY_CHIP_FIRMWARE", 20: "BLUETOOTH_DEVICE_ADDRESS",
            36: "PAIRED_SUBDEVICES_IDS", 53: "PAIRING_ID_V2",
            54: "CONNECTED_SUBDEVICES_IDS", 59: "SCREEN_MODULE_FIRMWARE",
            60: "DIAL_MODULE_FIRMWARE"}

STATUSES = {0: "SUCCESS", 1: "INVALID_ARGUMENT_VALUE", 2: "INSUFFICIENT_BUFFER_SIZE",
            3: "INVALID_STATE", 4: "COMMAND_NOT_SUPPORTED", 5: "PROPERTY_NOT_SUPPORTED",
            6: "INVALID_DEVICE_ADDRESS", 7: "HARDWARE_ERROR", 9: "INVALID_OPERATION",
            10: "INVALID_FIRMWARE_FILE"}


def status_name(response: bytes) -> str:
    code = response[2] if len(response) > 2 else 0xFF
    return f"{STATUSES.get(code, 'UNKNOWN')}({code})"


CMD_SET = 0x01
PROP_OPERATING_MODE = 0x03
MODE_SELF_OPERATED = 1
MODE_HOST_CONTROLLED = 2


def set_operating_mode(device: UsbDevice, mode: int) -> str:
    """Switches operating mode, trying both known value positions.

    The two reference implementations disagree: Corsair's updater sends `[mode, 0, 0, 0]`
    immediately after the property, while OpenRGB writes the value to `buffer[5]`, one byte
    later, leaving a zero gap. This tries the updater's layout first and falls back to
    OpenRGB's, reporting what each returned.

    This is the ordinary software-control handshake that iCUE, ckb-next and k100-color-oxide
    all perform; it is not a firmware operation.
    """
    attempts = (
        ("updater", bytes([mode, 0, 0, 0])),
        ("openrgb", bytes([0, mode, 0, 0])),
    )
    outcomes = []
    for label, payload in attempts:
        response, error = exchange(device, bragi_request(CMD_SET, PROP_OPERATING_MODE, payload))
        outcomes.append(f"{label}={error or status_name(response)}")
        if not error and current_mode(device) == mode:
            return f"{', '.join(outcomes)} <- took effect"
    return ", ".join(outcomes)


def current_mode(device: UsbDevice) -> int | None:
    response, error = exchange(device, bragi_request(CMD_GET, PROP_OPERATING_MODE))
    if error or len(response) < 4:
        return None
    return response[3]


def sweep(device: UsbDevice) -> None:
    """Read-only exploration: PING, then DESCRIBE_BUFFER under both argument conventions.

    ckb-next builds `{MAGIC, DESCRIBE_BUFFER, handle, 0}`, putting the handle where a GET
    keeps its property, while the web updater calls `sendCommand(DESCRIBE_BUFFER, 0, [index])`
    which puts it one byte later. Which is right has been an open question; the device can
    settle it.
    """
    print("  PING:")
    response, error = exchange(device, bragi_request(CMD_PING, 0x00))
    print(f"    {error or hexdump(response, 10) + '  ' + status_name(response)}")

    # File and buffer operations appear to need host control: the updater always switches
    # mode before them, and DESCRIBE_BUFFER answers INVALID_STATE while self-operated.
    entry_mode = current_mode(device)
    print(f"  mode on entry        {OPERATING_MODES.get(entry_mode, entry_mode)}")

    switched = False
    if entry_mode != MODE_HOST_CONTROLLED:
        result = set_operating_mode(device, MODE_HOST_CONTROLLED)
        now = current_mode(device)
        print(f"  -> HOST_CONTROLLED   {result}, now "
              f"{OPERATING_MODES.get(now, now)}")
        switched = now == MODE_HOST_CONTROLLED

    try:
        buffer_probes(device)
        open_file_framing_test(device)
    finally:
        if switched:
            result = set_operating_mode(device, MODE_SELF_OPERATED)
            now = current_mode(device)
            print(f"  restored mode        {result}, now "
                  f"{OPERATING_MODES.get(now, now)}")


def buffer_probes(device: UsbDevice) -> None:
    for label, build in (
        ("arg in byte 2 (ckb-next)", lambda i: bragi_request(CMD_DESCRIBE_BUFFER, i)),
        ("arg in byte 3 (updater)", lambda i: bragi_request(CMD_DESCRIBE_BUFFER, 0, bytes([i]))),
    ):
        print(f"  DESCRIBE_BUFFER, {label}:")
        for index in range(8):
            response, error = exchange(device, build(index))
            if error:
                print(f"    buffer {index}  {error}")
                continue
            length = int.from_bytes(response[5:9], "little") if len(response) >= 9 else 0
            print(f"    buffer {index}  {hexdump(response, 10)}  "
                  f"{status_name(response)}  len={length}")


# Benign read-only data files, used to settle which byte carries a command's argument.
# FIRMWARE (3) is deliberately excluded: its ID is already known from the updater bundle,
# and opening it may imply write intent.
# 0x01 and 0x22 are the lighting resources every RGB tool opens; k100-color-oxide opens
# 0x22 on this very keyboard, so one of them existing is a good control for the others.
SAFE_FILE_IDS = [0x01, 0x22, 36, 54]


def open_file_framing_test(device: UsbDevice) -> None:
    """Opens a harmless data file under each argument convention and compares the statuses.

    Unlike DESCRIBE_BUFFER on nothing, this can discriminate: a wrong argument position
    should produce a different status than a right one. Every handle opened is unbound again.
    """
    handle = 1
    for file_id in SAFE_FILE_IDS:
        name = FILE_IDS.get(file_id, "?")
        print(f"  OPEN_FILE {file_id} ({name}):")
        for label, request in (
            ("arg in byte 2 (ckb-next)",
             bragi_request(CMD_OPEN_FILE, handle, bytes([file_id, 0]))),
            ("arg in byte 3 (updater)",
             bragi_request(CMD_OPEN_FILE, 0, bytes([handle, file_id, 0]))),
        ):
            response, error = exchange(device, request)
            print(f"    {label:26} {error or hexdump(response, 8) + '  ' + status_name(response)}")

            # Always release, whether or not the open appeared to succeed. Arguments start
            # at byte 2, as OPEN_FILE proved, so the count goes there and the handle next.
            unbind, unbind_error = exchange(
                device, bragi_request(CMD_UNBIND_BUFFERS, 1, bytes([handle])))
            state = unbind_error or status_name(unbind)
            print(f"    {'':26} unbind: {state}")


def exchange(device: UsbDevice, request: bytes) -> tuple[bytes | None, str | None]:
    try:
        device.transfer(BRAGI_EP_OUT, request, BRAGI_REPORT_SIZE)
    except OSError as error:
        return None, f"OUT failed: {error.strerror} (errno {error.errno})"
    try:
        return device.transfer(BRAGI_EP_IN, None, BRAGI_REPORT_SIZE), None
    except OSError as error:
        return None, f"IN failed: {error.strerror} (errno {error.errno})"


def hexdump(data: bytes, count: int = 16) -> str:
    return " ".join(f"{b:02x}" for b in data[:count])


def interpret(response: bytes) -> str:
    """Candidate value under each header layout; which one applies is still open."""
    parts = []
    for label, offset in (("two-byte", 3), ("four-byte", 5)):
        if len(response) >= offset + 2:
            value = response[offset] | (response[offset + 1] << 8)
            parts.append(f"{label}={value:#06x} ({value})")
    return "  ".join(parts)


def query(device: UsbDevice, prop: int) -> tuple[bytes | None, str | None]:
    return exchange(device, bragi_request(CMD_GET, prop))


def probe(target: Found, interfaces: list[int], do_sweep: bool = False) -> bool:
    print(f"{target.node}  {target.model}  (sysfs {target.sysfs.name})")

    with UsbDevice(target.node, interfaces) as device:
        try:
            device.take()
        except OSError as error:
            print(f"  taking interfaces failed: {error.strerror} (errno {error.errno})")
            return False

        print()
        answered = False
        for prop, label in PROPERTIES:
            response, error = query(device, prop)
            if error:
                print(f"  {label:20} {error}")
                continue

            answered = True
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
        print()

        if do_sweep:
            sweep(device)
            print()
        return answered


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--interfaces", default="0,1,2",
                        help="comma-separated interfaces to detach and claim. Default 0,1,2, "
                             "matching ckb-next, which claims all of them; this stops the "
                             "keyboard typing until the probe exits")
    parser.add_argument("--sweep", action="store_true",
                        help="also PING and DESCRIBE_BUFFER each buffer index under both "
                             "argument conventions. Read-only; it never opens a file, and "
                             "in particular never opens the firmware file")
    args = parser.parse_args()
    interfaces = [int(part) for part in args.interfaces.split(",") if part.strip()]

    targets = discover()
    if not targets:
        print("No K100 found.", file=sys.stderr)
        print(f"Expected Corsair VID {CORSAIR_VID:#06x} with one of "
              + ", ".join(f"{p:#06x}" for p in K100_PIDS), file=sys.stderr)
        return 1

    return 0 if any(probe(target, interfaces, args.sweep) for target in targets) else 1


if __name__ == "__main__":
    raise SystemExit(main())
