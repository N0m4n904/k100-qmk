# k100-bragi

A Bragi client for the Corsair K100, written to establish the firmware **recovery** path
before any custom firmware exists. It is deliberately staged: read-only first, then
reversible transitions, and flashing only once the framing has been confirmed against real
hardware.

Protocol provenance is in `docs/bragi-firmware-update.md`; the safety reasoning is in
`docs/recovery.md`.

## Build and run

```sh
nix develop            # from the repository root
cargo build --manifest-path tools/k100-bragi/Cargo.toml
```

## Transport

**usbdevfs, with no HID dependency at all.** Neither of hidapi's backends can reach this
keyboard — hidraw writes are rejected with `ETIMEDOUT`, and the libusb backend fails without
a reportable error. `transport.rs` instead does what ckb-next does: open
`/dev/bus/usb/<bus>/<dev>`, detach the kernel driver from all three interfaces, claim them,
and use `USBDEVFS_BULK` on endpoints `0x01` out and `0x82` in. Interfaces are released and
the driver reattached on drop, so a dropped handle always leaves the keyboard usable.

Two consequences worth knowing:

- **Typing pauses** while the tool holds the interfaces, because interface 0 is the keyboard.
  ckb-next claims all of them too, and claiming fewer was observed not to work.
- **No elevated privileges are needed.** The usbfs node carries an ACL granting the
  logged-in user access, as OpenRGB's and ckb-next's udev rules arrange.

The ioctl request numbers are computed from the struct sizes rather than hardcoded, and a
unit test pins them to the values the kernel defines. A wrong number would otherwise be
indistinguishable from a device fault at runtime:

```sh
cargo test --manifest-path tools/k100-bragi/Cargo.toml
```

## Commands

| Command | Touches the keyboard? | What it does |
| --- | --- | --- |
| `verify-image <path>` | No | Parses a firmware container and verifies its CRC32/MPEG-2. Fully offline. |
| `info` | Reads only | Identifies the device and reports firmware and bootloader versions, physical layout and operating mode. |
| `files` | Writes | Opens and closes each known Bragi file to report which exist. Needs host control, which it takes and restores. Never opens firmware. |
| `enter-bootloader` | Writes | `SET OPERATING_MODE 3`, then waits for re-enumeration and confirms the device landed in bootloader. |
| `exit-bootloader` | Writes | The vendor transition back: `Bootloader → SelfOperated → HostControlled`. |

Flashing is **not** implemented. See below.

## What `info` reports, and what it settled

Framing is no longer probed: it is settled for this device. `USB_VID` and `USB_PID` read
back as `0x1b1c` and `0x1b7c` at response offset 3, which proves the two-byte header rather
than inferring it. Corsair's updater selects a four-byte header at Bragi protocol version
1.5; this keyboard is the older generation.

A real unit reports firmware **0.32.6**, bootloader **0.10.45**, layout **Iso** and mode
**SelfOperated**. The versions corroborate externally: ckb-next's source records a K100 at
"fw 0.32.6, bld 0.10.45".

Note that **`SelfOperated` is the power-on state and most commands are refused in it** with
`InvalidState`. `files` therefore wraps its work in host control via
`Device::with_host_control`, which restores the previous mode afterwards even on failure.

## Why flashing is still a stub

The command set is known: `OPEN_FILE`, then `WRITE_BUFFER_BEGIN` carrying a 4-byte
little-endian total length plus the first chunk, then `WRITE_BUFFER` for the remainder, then
`APPLY_FIRMWARE_EXTENDED` (22) — the K100 manifest asks for `apply-extended` specifically.

Both of the things that used to be unknown are now settled: the firmware file ID is **3**,
from the `Ft` enum in Corsair's updater bundle, and the header layout is two-byte, confirmed
on hardware. Argument placement is settled too — arguments begin at body byte 2, not byte 3
as the updater bundle has them.

What remains unverified is the part that cannot be checked without doing it: whether the
bootloader validates anything beyond the CRC, and whether the upload sequence works as read.
`roundtrip::flash` therefore still refuses, and the next step is **rule 2** of
`docs/recovery.md` — prove the round trip with a verified official image before any custom
one. Note that this unit runs firmware 0.32.6 while the published packages offer 0.35.288 and
0.30.257, so flashing either is an upgrade rather than a restore to its current state.

## Safety properties worth knowing

- The firmware file (ID 3) is never opened on this tool's own initiative.
- `ImageType::ensure_recoverable` rejects `Bootloader` and `FirmwareWithBootloader` at the
  type level. The bootloader *is* the recovery path, and Corsair's own updater refuses to
  touch it too, logging "Chip needs isp update which is not supported for now".
- Every mode transition is confirmed by re-reading the mode after re-enumeration, rather than
  assumed from a successful write.
- `enter-bootloader` is a no-op if the device is already in bootloader, and `exit-bootloader`
  is a no-op if it is not — so neither compounds a bad state.
- Finding the keyboard in bootloader mode is reported as recoverable, because it is.
