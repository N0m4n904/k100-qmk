# K100 firmware distribution and image format

Everything here was derived from Corsair's own public firmware storage and verified by
computation, not inferred. Reproduce it with `tools/fetch-firmware.sh`.

## Where official firmware lives

Corsair's web firmware updater (`corsair.com/firmware-updater`) is a Vite SPA whose
`config.js` names its backend:

```js
API_URL: "https://www.corsair.com/firmware-storage/firmware/public/"
```

Three endpoints matter, all plain unauthenticated HTTP GETs:

| Endpoint | Returns |
| --- | --- |
| `mapping.json` | Master index of every supported device (82 entries). |
| `manifests/<slug>_<vid>_<pid>_manifest.json` | Per-device capabilities. |
| `fw/<package>.json` and `fw/<file>.zip` | Firmware descriptors and packages. |

The index entry for our board:

```json
{ "usb-vid": 6940, "usb-pid": 7036, "type": "keyboard", "model-name": "K100 RGB",
  "manifest-url":  "/manifests/k100-rgb_1b1c_1b7c_manifest.json",
  "firmware-url":  "/fw/k100-rgb_1b1c_1b7c.json" }
```

`6940`/`7036` are `0x1b1c`/`0x1b7c` — the OPX optical K100. The mechanical SKU (`0x1b7d`)
and the later optical variant (`0x1bc5`) are listed too.

## The device manifest — the key document

`manifests/k100-rgb_1b1c_1b7c_manifest.json`, in full:

```json
{
  "usb-vid": 6940, "usb-pid": 7036,
  "type": "keyboard", "model-name": "K100 RGB",
  "proto": "bragi",
  "bragi-props": {
    "fwupd": {
      "apply-fw-timeout": 8000,
      "update-time-estimate": 300,
      "img-fw-options": [{
        "type": "firmware",
        "command": "apply-extended",
        "checksum-validation": "crc32-mpeg2",
        "reboot": true,
        "operating-modes": ["host-controlled", "bootloader"]
      }]
    }
  }
}
```

Four things this settles:

1. **The K100 is officially flashable.** `bragi-props.fwupd` is present, which is exactly the
   condition the updater requires before it will write firmware.
2. **`"operating-modes": [..., "bootloader"]`** — a bootloader mode is reachable over Bragi.
   This is the capability ckb-next never implemented and that we assumed was missing.
3. **`"command": "apply-extended"`** names the Bragi apply operation.
4. **`"checksum-validation": "crc32-mpeg2"`** — images are validated by CRC, and the manifest
   mentions no signature. Strong evidence that a self-built image can be made acceptable.

## Available packages

From `fw/k100-rgb_1b1c_1b7c.json`:

| Package | Application version | File |
| --- | --- | --- |
| `K100RGB_v1b1c-p1b7c-0.32.262` | 0.35.288 | `K100RGB_v1b1c-p1b7c_0.35.288.zip` |
| `K100RGB_v1b1c-p1b7c-0.30.257` | 0.30.257 | `K100RGB_v1b1c-p1b7c_0.30.257.zip` |

Each carries an SRI `sha512-…` integrity hash. The 0.35.288 package downloads as 101,835
bytes and its hash **matches exactly**, so we have a verified-authentic official image —
which is the recovery image gate 0 depends on.

Contents of the ZIP:

```
    486  Heracles_Application_Firmware_v0.35.288.json
 150908  Heracles_Application_Firmware_v0.35.288.bin
```

"Heracles" is Corsair's internal codename for the K100. The inner JSON repeats
`"protocol": "bragi"`, `vid 1b1c`, `pid 1b7c`, and adds `"protocol-version": "137"`.

## `1bc5` is a different platform, and its container proves it

The container below is the **Heracles** one, which `1b7c` and `1b7d` share. `1bc5` does not use
it, and the difference is not a revision.

Fetching `--pid 1bc5` returns `Splinter_App_v1.15.61.bin`, where `1b7c` returns
`Heracles_Application_Firmware_v0.35.288.bin`. Different codename, different version scheme, and
a container `tools/fetch-firmware.py` reports as `INVALID` — correctly, because it is not this
format at all.

What the Splinter image actually is:

| Offset | Contents | Meaning |
| --- | --- | --- |
| `0x0000` | `46 43 46 42` then `00 04 01 56` | FlexSPI Configuration Block: tag `0x42464346` "FCFB", version `0x56010400` |
| `0x1000` | `d1 00 20 41` | i.MX **Image Vector Table**: header tag `0xD1`, length 32, version `0x41` |
| `0x2000` | initial SP `0x20010000`, reset `0x60022415` | the application's vector table |

The IVT's own fields agree with that reading: `self = 0x60021000` and `entry = 0x60022000`, which
place the image base at **`0x60020000`** — so the vector table at file offset `0x2000` lands at
`0x60022000`, and the first 128 KB of the flash is the bootloader.

`0x60000000` is where NXP's i.MX RT parts memory-map FlexSPI serial NOR, and FlexSPI block at zero
plus IVT at `0x1000` plus application at `0x2000` is NXP's documented XIP boot layout. So
**`1bc5` is an i.MX RT running execute-in-place from external QSPI flash.**

Two further differences point the same way. The Heracles image is full of NXP **LPC** SDK paths —
`..\lib\sdk\drivers\fsl_flexcomm.c`, `fsl_spi.c`, `fsl_spifi.c`, `fsl_ctimer.c`, `fsl_pint.c` —
and the Splinter image contains no `fsl_` string at all. And `1bc5`'s manifest lists only the
`bootloader` operating mode where `1b7c`'s lists `host-controlled, bootloader`.

Neither image is encrypted: both measure about 7 bits of entropy per byte, which is ordinary ARM
code. Splinter simply carries no C string literals.

Which i.MX RT is not pinned down, but the family is. Its vector table is about 158 entries, so
roughly 142 device interrupts, which puts it with the RT105x/RT106x parts rather than the RT1010
(~80) or RT1020 (~110). The initial stack pointer of `0x20010000` is 64 KB of DTCM.

That matters for anyone weighing the two variants, because **ChibiOS-Contrib already has a
`MIMXRT1062` HAL port and QMK already has an `IC_TEENSY_4_1` board using it** — Teensy 4.x is
that part. On an i.MX RT target, everything `chibios/` exists to provide would come for free.

The trade runs the other way for the reverse engineering, though, and that is the harder half. The
Heracles image carries 561 strings, among them `is31fl3743bSetPwmChannelValue` and SDK paths like
`..\lib\sdk\drivers\fsl_spi.c`, plus assert filenames and line numbers. **Every finding in
`docs/pin-mapping.md` is anchored on one of those.** The Splinter image has none: no symbol names,
no `fsl_` paths, nothing. The matrix pins, the LED driver part number, the channel map and the
`LedInfoMap` equivalent would all have to be found with no symbol to start from, in a blob that
also omits its own bootloader.

**Nothing in this repository applies to `1bc5`** — not the ChibiOS LPC546xx port, not the pin
mapping, not one firmware address. ckb-next's "later variant" label is its own, and nothing in it
suggested the silicon had changed.

One incidental corroboration for the LPC54605 side: `fsl_spifi.c` and `fsl_spifi_dma.c` appear in
the Heracles image, so that firmware really does drive an external quad-SPI flash. That is
independent support for `P0_23`-`P0_28` being the SPIFI bus, which `docs/pin-mapping.md` reached
from the pin function tables alone.

## The `.bin` container format — verified

**The image is not encrypted.** Entropy is 7.03 bits/byte with 9.5% zero bytes; encrypted
data would sit at ~8.0 with ~0.4% zeros. ARM Thumb code begins at `0x2c` (`10 b5` =
`push {r4, lr}`).

Header layout:

| Offset | Size | Field |
| --- | --- | --- |
| `0x00` | u32 | **CRC32/MPEG-2 over bytes `[0x04 .. EOF]`** |
| `0x04` | u32 | Total file length, including the header (150908) |
| `0x08` | u32 | `0x00012569` (75113) — purpose not yet known |
| `0x0c` | u16 | `0x0001` — format or version field |
| `0x0e` | — | ASCII build date, `"Aug 22 2023\0"` |
| `0x1a` | — | ASCII build time, `"14:39:00\0"` |
| `0x25` | — | `7c 1b` — the PID, little-endian |
| `0x2c` | … | ARM Thumb payload |

The CRC was confirmed by computation, not assumed:

```
CRC32/MPEG-2 (poly 0x04C11DB7, init 0xFFFFFFFF, no reflection, no final XOR)
  over bytes [0x04 .. EOF]  (150,904 bytes)
  = 0xb8fc58e7
stored at offset 0x00       = 0xb8fc58e7   ✓
```

No other start offset or length combination matched, so the covered range is exact.

### Why this matters

Building a valid custom image is now a solved problem: lay out the payload, write the total
length at `0x04`, compute CRC32/MPEG-2 over everything from `0x04` onward, store it at
`0x00`. No encryption and no signature in the container.

### Still open

- Does the **bootloader itself** additionally verify a signature, beyond the CRC the manifest
  describes? The manifest's wording suggests not, but only a real write proves it.
- What is the field at `0x08` (`75113`)? It is neither the file length nor the payload length.
- What is the load address, and does the payload start with a vector table at some offset?
  The image begins with code at `0x2c` rather than a Cortex-M vector table, so the bootloader
  likely places it at a fixed flash offset.
