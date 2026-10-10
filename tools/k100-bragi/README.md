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

**This tool cannot currently talk to the keyboard.** It is built on hidapi's default hidraw
backend, and hidraw writes to the K100's Bragi interface are rejected with `ETIMEDOUT`; see the
Linux transport section of `docs/bragi-firmware-update.md`. It needs hidapi's libusb backend
or a usbdevfs transport before `info` will work against real hardware.

Whichever transport replaces it will need privileged access to the device, so expect to run
as root or add a udev rule for Corsair's vendor ID `1b1c`.

## Commands

| Command | Touches the keyboard? | What it does |
| --- | --- | --- |
| `verify-image <path>` | No | Parses a firmware container and verifies its CRC32/MPEG-2. Fully offline. |
| `info` | Reads only | Identifies the device, reads firmware and bootloader versions, and reports the operating mode. |
| `enter-bootloader` | Writes | `SET OPERATING_MODE 3`, then waits for re-enumeration and confirms the device landed in bootloader. |
| `exit-bootloader` | Writes | The vendor transition back: `Bootloader → SelfOperated → HostControlled`. |

Flashing is **not** implemented. See below.

## Run `info` first

`info` is not just a status dump — it resolves something the protocol cannot tell us
statically. Corsair's updater chooses its header layout from the Bragi protocol version the
*device* reports (`FOUR_BYTE` at version 1.5, `TWO_BYTE` otherwise), so the framing has to be
discovered at runtime. This tool probes both layouts and both report lengths against a known
property and keeps whichever answers coherently, then prints what it settled on.

That result is the input the flash path needs, which is why flashing is gated behind it.

## Why flashing is still a stub

The command set is known: `OPEN_FILE`, then `WRITE_BUFFER_BEGIN` carrying a 4-byte
little-endian total length plus the first chunk, then `WRITE_BUFFER` for the remainder, then
`APPLY_FIRMWARE_EXTENDED` (22) — the K100 manifest asks for `apply-extended` specifically.

Two things are not yet pinned down from static reading:

- the file ID that selects the firmware buffer, and
- which header layout this device uses, which `info` answers.

Writing firmware on a guess at either is the one mistake that could cost the keyboard, so
`roundtrip::flash` refuses rather than improvising. Confirming both is cheap; guessing is not.

## Safety properties worth knowing

- `ImageType::ensure_recoverable` rejects `Bootloader` and `FirmwareWithBootloader` at the
  type level. The bootloader *is* the recovery path, and Corsair's own updater refuses to
  touch it too, logging "Chip needs isp update which is not supported for now".
- Every mode transition is confirmed by re-reading the mode after re-enumeration, rather than
  assumed from a successful write.
- `enter-bootloader` is a no-op if the device is already in bootloader, and `exit-bootloader`
  is a no-op if it is not — so neither compounds a bad state.
- Finding the keyboard in bootloader mode is reported as recoverable, because it is.
