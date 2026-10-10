#!/usr/bin/env python3
"""Fetch and verify official Corsair firmware from the public firmware storage.

Downloads the device manifest and every published firmware package for a device,
checks each package against the SRI hash Corsair publishes for it, and validates
the CRC32/MPEG-2 header of the inner image. See docs/firmware-format.md.

Keeping this as a script rather than committing the images avoids redistributing
Corsair's copyrighted firmware while still making the recovery image reproducible.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import zipfile
from dataclasses import dataclass
from io import BytesIO
from pathlib import Path
from urllib.request import Request, urlopen

API = "https://www.corsair.com/firmware-storage/firmware/public"

K100_OPTICAL = (0x1B1C, 0x1B7C)

CRC32_MPEG2_POLY = 0x04C11DB7


def crc32_mpeg2(data: bytes) -> int:
    """CRC-32/MPEG-2: poly 0x04C11DB7, init all-ones, no reflection, no final XOR."""
    table = []
    for byte in range(256):
        reg = byte << 24
        for _ in range(8):
            reg = ((reg << 1) ^ CRC32_MPEG2_POLY) & 0xFFFFFFFF if reg & 0x80000000 else (reg << 1) & 0xFFFFFFFF
        table.append(reg)

    crc = 0xFFFFFFFF
    for byte in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ table[((crc >> 24) ^ byte) & 0xFF]
    return crc


# The CDN rejects urllib's default User-Agent with HTTP 403.
HEADERS = {
    "Accept": "application/json, application/zip, application/octet-stream, */*",
    "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36",
}


def fetch(path: str) -> bytes:
    with urlopen(Request(f"{API}/{path.lstrip('/')}", headers=HEADERS)) as response:
        return response.read()


def sri_matches(data: bytes, integrity: str) -> bool:
    algorithm, _, expected = integrity.partition("-")
    digest = hashlib.new(algorithm, data).digest()
    return base64.b64encode(digest).decode() == expected


@dataclass(frozen=True)
class Image:
    """One firmware image extracted from a published package."""

    name: str
    data: bytes

    @property
    def declared_crc(self) -> int:
        return int.from_bytes(self.data[0:4], "little")

    @property
    def declared_length(self) -> int:
        return int.from_bytes(self.data[4:8], "little")

    @property
    def actual_crc(self) -> int:
        return crc32_mpeg2(self.data[4:])

    @property
    def build_stamp(self) -> str:
        return self.data[0x0E:0x23].replace(b"\0", b" ").decode("ascii", "replace").strip()

    @property
    def valid(self) -> bool:
        return self.declared_crc == self.actual_crc and self.declared_length == len(self.data)


def device_entries(vid: int, pid: int) -> list[dict]:
    mapping = json.loads(fetch("mapping.json"))
    return [d for d in mapping["devices"] if d["usb-vid"] == vid and d["usb-pid"] == pid]


def images_in(package: bytes) -> list[Image]:
    with zipfile.ZipFile(BytesIO(package)) as zf:
        return [Image(info.filename, zf.read(info)) for info in zf.infolist() if info.filename.endswith(".bin")]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--vid", type=lambda s: int(s, 16), default=K100_OPTICAL[0], help="USB vendor ID in hex")
    parser.add_argument("--pid", type=lambda s: int(s, 16), default=K100_OPTICAL[1], help="USB product ID in hex")
    parser.add_argument("--out", type=Path, help="directory to write verified images into")
    args = parser.parse_args()

    entries = device_entries(args.vid, args.pid)
    if not entries:
        print(f"No device published for {args.vid:04x}:{args.pid:04x}")
        return 1

    for entry in entries:
        print(f"{entry['model-name']}  {args.vid:04x}:{args.pid:04x}  ({entry['type']})")

        manifest = json.loads(fetch(entry["manifest-url"]))
        fwupd = manifest.get("bragi-props", {}).get("fwupd")
        print(f"  protocol            {manifest.get('proto')}")
        print(f"  firmware update     {'supported' if fwupd else 'NOT SUPPORTED'}")
        for option in (fwupd or {}).get("img-fw-options", []):
            print(f"    command           {option.get('command')}")
            print(f"    checksum          {option.get('checksum-validation')}")
            print(f"    operating modes   {', '.join(option.get('operating-modes', []))}")

        descriptor = json.loads(fetch(entry["firmware-url"]))
        for name, package in descriptor.get("packages", {}).items():
            source = package["source"]
            blob = fetch(f"/fw/{source}")
            ok = sri_matches(blob, package["integrity"])
            print(f"  {source}  ({len(blob)} bytes)  integrity {'OK' if ok else 'FAILED'}")
            if not ok:
                print("    refusing to use a package whose published hash does not match")
                continue

            for image in images_in(blob):
                state = "valid" if image.valid else "INVALID"
                print(f"    {image.name}")
                print(f"      built           {image.build_stamp}")
                print(f"      length          {len(image.data)} (header declares {image.declared_length})")
                print(f"      crc32-mpeg2     0x{image.actual_crc:08x} (header declares 0x{image.declared_crc:08x}) {state}")

                if args.out:
                    args.out.mkdir(parents=True, exist_ok=True)
                    destination = args.out / image.name
                    destination.write_bytes(image.data)
                    print(f"      written to      {destination}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
