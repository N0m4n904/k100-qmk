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
