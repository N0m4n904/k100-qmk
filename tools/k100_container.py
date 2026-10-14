# SPDX-License-Identifier: GPL-3.0-or-later
"""Corsair's firmware container, as the K100's bootloader expects it.

Every field was read out of the two published images rather than guessed, and
`tools/pack-firmware.py --verify` proves the writer by rebuilding both of them
from their own payloads and comparing byte for byte. See docs/firmware-format.md
for how each field was established.

The layout, 0x2c bytes, written into flash at 0x00010000 ahead of the code:

    0x00  u32   CRC-32/MPEG-2 over everything from 0x04 to the end
    0x04  u32   total length, header included
    0x08  u32   entry point, a Thumb address the bootloader branches to
    0x0c  u16   container format version, 1
    0x0e  12    build date, "Mmm DD YYYY", NUL terminated
    0x1a  9     build time, "HH:MM:SS", NUL terminated
    0x23  2     zero
    0x25  u16   USB product id
    0x27  u16   image type; 1 is firmware, 2 is bootloader
    0x29  u8    firmware version, minor
    0x2a  u16   firmware version, patch
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

HEADER_SIZE = 0x2C

#: The application's base in flash. The first 64 KB is Corsair's bootloader, and
#: the header itself occupies the first 0x2c bytes of what follows.
APPLICATION_BASE = 0x00010000

#: Container format version seen in both published images.
FORMAT_VERSION = 1

#: Image types. Writing anything but FIRMWARE risks the bootloader, which is the
#: one unrecoverable mistake available here -- see docs/recovery.md, rule 1.
IMAGE_TYPE_FIRMWARE = 1
IMAGE_TYPE_BOOTLOADER = 2

CRC32_MPEG2_POLY = 0x04C11DB7


def crc32_mpeg2(data: bytes) -> int:
    """CRC-32/MPEG-2: poly 0x04C11DB7, init all-ones, no reflection, no final XOR."""
    reg = 0xFFFFFFFF
    for byte in data:
        reg ^= byte << 24
        for _ in range(8):
            if reg & 0x80000000:
                reg = ((reg << 1) ^ CRC32_MPEG2_POLY) & 0xFFFFFFFF
            else:
                reg = (reg << 1) & 0xFFFFFFFF
    return reg


@dataclass
class Container:
    """A parsed container, enough of one to rebuild it exactly."""

    payload: bytes
    entry: int
    product_id: int
    build_date: str
    build_time: str
    version_minor: int
    version_patch: int
    image_type: int = IMAGE_TYPE_FIRMWARE
    format_version: int = FORMAT_VERSION

    declared_crc: int = 0
    declared_length: int = 0

    @property
    def crc_valid(self) -> bool:
        return self.declared_crc == crc32_mpeg2(self.pack()[4:])

    def pack(self) -> bytes:
        """The complete image, header and payload, with CRC and length filled in."""
        if self.image_type != IMAGE_TYPE_FIRMWARE:
            raise ValueError(
                f"refusing to build an image of type {self.image_type}; only "
                f"{IMAGE_TYPE_FIRMWARE} (firmware) is safe to write, see docs/recovery.md"
            )

        header = bytearray(HEADER_SIZE)
        total = HEADER_SIZE + len(self.payload)

        struct.pack_into("<I", header, 0x04, total)
        struct.pack_into("<I", header, 0x08, self.entry)
        struct.pack_into("<H", header, 0x0C, self.format_version)
        header[0x0E:0x0E + 12] = self.build_date.encode("ascii").ljust(12, b"\0")[:12]
        header[0x1A:0x1A + 9] = self.build_time.encode("ascii").ljust(9, b"\0")[:9]
        struct.pack_into("<H", header, 0x25, self.product_id)
        struct.pack_into("<H", header, 0x27, self.image_type)
        header[0x29] = self.version_minor & 0xFF
        struct.pack_into("<H", header, 0x2A, self.version_patch)

        body = bytes(header[4:]) + self.payload
        struct.pack_into("<I", header, 0x00, crc32_mpeg2(body))
        return bytes(header) + self.payload

    @classmethod
    def parse(cls, image: bytes) -> "Container":
        if len(image) < HEADER_SIZE:
            raise ValueError(f"too short to be a container: {len(image)} bytes")

        crc, length, entry = struct.unpack_from("<III", image, 0)
        (format_version,) = struct.unpack_from("<H", image, 0x0C)
        date = image[0x0E:0x0E + 12].split(b"\0")[0].decode("ascii", "replace")
        time = image[0x1A:0x1A + 9].split(b"\0")[0].decode("ascii", "replace")
        product_id, image_type = struct.unpack_from("<HH", image, 0x25)
        minor = image[0x29]
        (patch,) = struct.unpack_from("<H", image, 0x2A)

        return cls(
            payload=image[HEADER_SIZE:],
            entry=entry,
            product_id=product_id,
            build_date=date,
            build_time=time,
            version_minor=minor,
            version_patch=patch,
            image_type=image_type,
            format_version=format_version,
            declared_crc=crc,
            declared_length=length,
        )

    def describe(self) -> str:
        return (
            f"entry 0x{self.entry:08X}, pid 0x{self.product_id:04x}, "
            f"type {self.image_type}, version 0.{self.version_minor}.{self.version_patch}, "
            f"built {self.build_date} {self.build_time}, "
            f"{HEADER_SIZE + len(self.payload)} bytes"
        )
