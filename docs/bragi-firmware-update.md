# Bragi: operating modes and firmware update

Recovered by reading Corsair's own web firmware updater bundle
(`corsair.com/firmware-updater/assets/index.*.js`), cross-checked against ckb-next's
independently reverse-engineered constants. Every value below is quoted from one of those two
sources; the one remaining inference is marked as such.

The bundle is unminified enough to read: it ships a complete, structured Bragi client with
named enums. This is a far better source than a USB capture, because it gives names and
bounds, not just bytes.

## Command opcodes

From the bundle's command enum, and identical to ckb-next's `bragi_proto.h`:

ckb-next had guessed the semantics of most of these from observation; the bundle gives
Corsair's own names, and every numeric value agrees:

| Corsair name | Value | ckb-next's name for it |
| --- | --- | --- |
| `SET` | `1` | `BRAGI_SET 0x01` |
| `GET` | `2` | `BRAGI_GET 0x02` |
| `UNBIND_BUFFERS` | `5` | `BRAGI_CLOSE_HANDLE 0x05` |
| `WRITE_BUFFER_BEGIN` | `6` | `BRAGI_WRITE_DATA 0x06` |
| `WRITE_BUFFER` | `7` | `BRAGI_CONTINUE_WRITE 0x07` |
| `READ_BUFFER` | `8` | `BRAGI_READ_DATA 0x08` |
| `DESCRIBE_BUFFER` | `9` | `BRAGI_PROBE_HANDLE 0x09` |
| `OPEN_FILE` | `13` | `BRAGI_OPEN_HANDLE 0x0d` |
| `RESET_TO_DEFAULTS` | `15` | — |
| `APPLY_FIRMWARE` | `16` | — |
| `APPLY_FIRMWARE_EXTENDED` | `22` | — |

`PING`, `PAIRING_CONTROL` and `HOST_SOFTWARE_SESSION_CONTROL` also exist; their values were
not captured.

The K100's manifest asks for `apply-extended`, so this board uses
**`APPLY_FIRMWARE_EXTENDED` (22)**, not the plain `APPLY_FIRMWARE` (16).

### Framing

`0x08` is best understood as the **HID report ID** Bragi rides on, not a magic byte. The
updater keeps `outputReportId` separate from the body it builds, while ckb-next writes to the
interrupt endpoint raw and therefore sees `0x08` as the first payload byte. Both describe the
same wire bytes.

Counting the report ID as index 0, a request is:

```
[0] 0x08        report ID
[1] command
[2] property    (0 for every command other than GET and SET)
[3..] payload
```

zero-padded to the report length. The K100 is a `USES_BRAGI_JUMBO` device, so that length is
**1024** bytes (ckb-next's `BRAGI_JUMBO_SIZE`; the non-jumbo size is 64).

**The header width is not fixed.** `getHeaderLayout()` returns `FOUR_BYTE` when the device
reports Bragi protocol version 1.5 and `TWO_BYTE` otherwise, which shifts where response
payloads begin — index 3 for two-byte, index 5 for four-byte. The version is read from the
device at runtime, so this must be determined empirically; `k100-bragi info` does that by
probing both and keeping whichever answers coherently.

### Status codes

`SUCCESS = 0`, `BUSY = 1`, `INSUFFICIENT_BUFFER_SIZE = 2`, `INVALID_STATE = 3`,
`COMMAND_NOT_SUPPORTED = 4`, `INVALID_DEVICE_ADDRESS = 6`, `HARDWARE_ERROR = 7`,
`INVALID_OPERATION = 9`, `INVALID_FIRMWARE_FILE = 10`, `MALFORMED_REPORT = 15`.

### The upload sequence

From `writeFile` in the bundle:

1. `openFile({bufferIndex, fileId})` → `sendCommand(OPEN_FILE, 0, [bufferIndex, ...fileId])`;
   a non-zero status aborts.
2. `writeBufferBegin({bufferIndex, data, size})` →
   `sendCommand(WRITE_BUFFER_BEGIN, 0, [bufferIndex, ...size, ...firstChunk])`, where `size`
   is the **total** length as 4 little-endian bytes.
3. `writeBuffer({bufferIndex, data})` → `sendCommand(WRITE_BUFFER, 0, [bufferIndex, ...chunk])`
   for the remainder.
4. `APPLY_FIRMWARE_EXTENDED`.

First-chunk capacity is `reportLength - 4 - 1 - sizeWidth`, with `sizeWidth` 4 on protocol 1.5
and 2 otherwise.

The missing piece is the **file ID for the firmware buffer**. `DESCRIBE_BUFFER` returns a
buffer's length, so enumerating file IDs and describing them is a read-only way to find it.

## Properties

`OPERATING_MODE = 3` (bundle), which is the same field ckb-next calls `BRAGI_MODE 0x03`.

Other properties the bundle names: `USB_PID`, `FIRMWARE_VERSION`, `BOOTLOADER_VERSION`,
`SECONDARY_CHIP_FIRMWARE_VERSION`, `SCREEN_MODULE_VERSION`, `DIAL_MODULE_VERSION`.

## Operating modes — the important enum

```js
RESERVED = 0, SELF_OPERATED = 1, HOST_CONTROLLED = 2, BOOTLOADER = 3, SLEEP = 4
```

This is the find that matters. ckb-next only ever documented two values of this field, as
`BRAGI_MODE_HARDWARE = 0x01` and `BRAGI_MODE_SOFTWARE = 0x02` — which are exactly
`SELF_OPERATED` and `HOST_CONTROLLED`. **`BOOTLOADER = 3` is the third value, and it is the
bootloader entry that was assumed not to exist.**

## How the updater enters the bootloader

The bundle's implementation, lightly deminified:

```js
async setOperatingMode({mode, ...opts}) {
  if (mode < 1 || mode > 7) throw new Error("Operating mode must be between 1 and 7");
  return (await this.sendCommand(SET, OPERATING_MODE, [mode, 0, 0, 0], opts))?.payload ?? null;
}
```

Substituting the confirmed constants gives the packet, padded to the report size:

```
08 01 03 03 00 00 00 ...
│  │  │  └──── BOOTLOADER        (operating mode 3)
│  │  └─────── OPERATING_MODE    (property 3)
│  └────────── SET               (command 1)
└───────────── Bragi magic
```

**This is one byte away from code that already works on this keyboard.**
`k100-color-oxide` opens with `0x08, 0x01, 0x03, 0x02` — that is `SET OPERATING_MODE
HOST_CONTROLLED`, the same command with mode 2 instead of 3.

That last step is the only inference in this document: the constants are all quoted, but that
this exact packet enters bootloader mode on our unit is unconfirmed until it is sent.

Note the updater sends the bootloader transition with `expectResponse: false` and then waits
for the device to re-enumerate — the device drops off the bus rather than replying.

## Getting back out — the recovery path

The bundle logs *"Transitioning from bootloader to host controlled via self-operated mode"*,
and the corresponding code path goes `BOOTLOADER → SELF_OPERATED → HOST_CONTROLLED`, waiting
for re-enumeration between steps, rather than straight back to `HOST_CONTROLLED`.

Worth following exactly, since this is the documented escape from bootloader mode.

Observed retry parameters: `retries: 3`, `waitRequest: 50`, `waitResponse: 200`,
`waitRepeat: 300` for mode changes, and a 20 s `disconnectWaitTimeMs` after an
`apply-extended` reboot.

## Image types and checksums

```js
// image type
RESERVED = 0, FIRMWARE = 1, BOOTLOADER = 2, FIRMWARE_WITH_BOOTLOADER = 3,
SECONDARY_CHIP_FIRMWARE = 4

// checksum algorithm
RESERVED = 0, CRC32_MPEG2 = 1
```

`CRC32_MPEG2 = 1` is the only non-reserved checksum algorithm, matching the K100 manifest's
`"checksum-validation": "crc32-mpeg2"` and the container format verified in
`docs/firmware-format.md`. There is no signature algorithm enum anywhere in the bundle.

Note that `BOOTLOADER = 2` and `FIRMWARE_WITH_BOOTLOADER = 3` exist as image types, meaning
the protocol can in principle replace the bootloader itself. We should not go near that: it
removes the recovery path that makes this project survivable.

## Client API surface worth reimplementing

The bundle's device object, which our Rust tooling should mirror:

- `getOperatingMode({retries, waitRequest, waitResponse, waitRepeat})`
- `setOperatingMode({mode, expectResponse})`
- `readFile({fileId, bufferIndex})`
- `waitForDeviceReconnection(device, config)`
- `updateProtocolVersion(...)`

The K100's inner firmware JSON declares `"protocol-version": "137"`, which is presumably what
`updateProtocolVersion` tracks.

## Still unknown

- The `apply-extended` packet sequence itself: how the image is streamed (presumably
  `OPEN_HANDLE` / `WRITE_DATA` / `CONTINUE_WRITE` against a firmware resource) and how the
  apply is triggered. The bundle has it; this document has not traced it yet.
- Whether the bootloader validates anything beyond the CRC.
