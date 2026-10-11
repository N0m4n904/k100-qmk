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

### Linux transport: hidraw does not work

Established empirically against a real K100 (`1b1c:1b7c`), see `tools/probe-hidraw.py`.

The keyboard exposes three HID interfaces:

| hidraw node | Interface | Descriptor | Endpoints |
| --- | --- | --- | --- |
| `hidraw12` | 0 | Generic Desktop / Keyboard, report IDs 1-3 | EP `0x81` IN, 16 B |
| `hidraw13` | 1 | Vendor page `0xFF42` usage `0x01`, 1024 B In + Out, **unnumbered** | EP `0x82` IN, EP `0x01` **OUT**, 1024 B |
| `hidraw14` | 2 | Vendor page `0xFF42` usage `0x02`, 64 B In only | EP `0x83` IN, 64 B |

Interface 1 is the Bragi command interface, and its 1024-byte reports confirm
`BRAGI_JUMBO_SIZE`. The descriptor contains no Report ID item, so reports are **unnumbered**
and `0x08` is the first payload byte rather than a report ID — which is how
`k100-color-oxide` already had it.

**Every write to `hidraw13` fails with `ETIMEDOUT` (errno 110).** Tested at 1024, 512, 256,
128, 64, 32, 16, 8 and 4 bytes, with and without the leading report-id byte: all eighteen
combinations fail identically, so this is not a framing or length problem. Writes to
`hidraw12` and `hidraw14` are reported as successful but do nothing, because neither
interface has an OUT endpoint and the kernel silently falls back to a control `SET_REPORT`.
Responses do not arrive on a sibling interface either; that was tested too.

Ruled out: no other process holds any of the nodes, the kernel log is clean, and the user had
no control software running.

**ckb-next does not use hidraw.** `src/daemon/usb_linux.c` drives the device through usbdevfs
ioctls on `/dev/bus/usb/...` — `USBDEVFS_CONTROL`, `USBDEVFS_BULK`, and `USBDEVFS_SUBMITURB`
with `USBDEVFS_URB_TYPE_INTERRUPT` — detaching the kernel driver and claiming the interface
first. `bragi_usb_write()` in `usb_bragi.c` sends through
`os_usb_interrupt_out(kb, bragi_out_ep, out_ep_packet_size, ...)` with `bragi_out_ep = 0x01`,
and reads come from a URB reaper thread rather than a paired read.

So the command does go to interrupt OUT on EP `0x01` — the same endpoint hidraw targets — but
only works when the interface has been claimed through usbdevfs. Why hidraw's path is
rejected is not fully explained here; the actionable fact is that the mature implementation
that supports this exact product ID avoids hidraw, and hidraw demonstrably fails.

### hidapi's libusb backend does not work either

Tried next, as the smaller change. `tools/k100-bragi` was switched to
`hidapi = { default-features = false, features = ["linux-static-libusb"] }`, which should do
the detach-and-claim that hidraw does not.

It fails the same way: writes to interface 1 are rejected, while interfaces 0 and 2 accept
writes that do nothing. Worse, the libusb backend reports every failure as
`hid_error is not implemented yet`, so there is no errno and no diagnosis. Rebuilding hidapi
with `CFLAGS=-DDETACH_KERNEL_DRIVER` — the compile-time flag that gates its
`libusb_detach_kernel_driver` call, off by default — changed nothing, though with no error
text it is not possible to tell whether the flag even propagated.

**So neither hidraw nor hidapi-over-libusb can currently drive this keyboard, and the Rust
tool has no working transport.**

### Privileges: root is not needed

Worth knowing before anyone reaches for `sudo`:

| Path | Permissions |
| --- | --- |
| `/dev/hidraw12-14` | `crw-rw-rw-` plus an ACL — world writable |
| `/dev/bus/usb/010/004` | `crw-rw-r--` with ACL `user:noah:rw-` |

The usbfs node is writable by the logged-in user, which is how OpenRGB and ckb-next operate
unprivileged. Writing to `/sys/bus/usb/drivers/usbhid/unbind` *does* need root, so detaching
the kernel driver that way is not available, but `USBDEVFS_DISCONNECT` on the usbfs node is,
and that is the mechanism ckb-next actually uses.

The keyboard is `1b1c:1b7c` on bus 010 device 004, so its usbfs node is
`/dev/bus/usb/010/004`, and its interfaces appear as `10-3:1.0`, `10-3:1.1`, `10-3:1.2`.

### Resolved: a power cycle fixed it, and usbdevfs works

**Try replugging the keyboard first.** After unplugging and reconnecting the keyboard,
`tools/probe-usbdevfs.py` worked on the first attempt with no code change, and the device
answered every query.

The keyboard typed normally throughout, so its controller, matrix scanning and USB stack were
healthy the whole time; only the vendor command endpoint was unresponsive to usbdevfs. What
left it in that state is unknown — it was already failing before any interface of ours was
detached.

**The power cycle did not fix hidraw.** Retested afterwards, writes to the Bragi interface
still fail with `ETIMEDOUT` at every report length, exactly as before. So the hidraw result
below is **not** a transient fault state: it is a persistent property of this keyboard,
reproducible across a power cycle, and only the usbdevfs path works.

That has a practical consequence, and it was then confirmed directly. Chromium's WebHID uses
hidraw `write()` for output reports on Linux, so **Corsair's browser-based firmware updater
does not work on Linux** regardless of what their documentation says about root permissions.
Running it against this keyboard produces:

```
[ERROR] Error with retrying command {id: 1, cmd: 2, propId: 3, payload: Uint8Array(4), …}
        NotAllowedError: Failed to write the report.
```

The device opened successfully — the page got as far as issuing a command — and only the
write failed, which is the same wall as hidraw. (`cmd: 2` and `propId: 3` are `GET` and
`OPERATING_MODE`, incidentally corroborating those two values from an independent client. A
separate `iCUE WebSocket connection failed` line in the same log is unrelated: the page probes
for a local iCUE install as a fallback.)

**So a Windows host or VM is the route for an official firmware update.**

Three independent clients now fail to write over hidraw — this project's own probe, hidapi,
and Chromium's WebHID — while usbdevfs works. That is about as well established as it can be
without understanding the mechanism.

Working configuration, confirmed against hardware:

| Item | Value |
| --- | --- |
| Transport | usbdevfs on `/dev/bus/usb/<bus>/<dev>`, all three interfaces detached and claimed |
| Transfer | `USBDEVFS_BULK`, OUT `0x01`, IN `0x82`, 1024-byte reports |
| Request | `08 <cmd> <prop> …` zero padded (no HID report-id byte on usbdevfs) |
| Header layout | **two-byte** — payload begins at response offset 3 |

**Response layout**, which differs from the request and is easy to get wrong:

```
[0] 0x00            <- NOT the 0x08 magic
[1] command echo    (0x02 for GET)
[2] status          (0x00 = SUCCESS)
[3..] payload
```

Two-byte framing is not inferred here but proven: `USB_VID` returns `0x1b1c` and `USB_PID`
returns `0x1b7c` when read at offset 3, both independently known to be correct.

### Request layouts, verified on hardware

**Arguments start at byte 2, not byte 3.** Corsair's own updater bundle builds
`sendCommand(cmd, prop, payload)` with the payload after a property byte, and **that layout
is rejected by this keyboard**. ckb-next's convention is the correct one here.

| Command | Wire bytes | Status |
| --- | --- | --- |
| `PING` (0x12) | `08 12` | SUCCESS |
| `GET` (0x02) | `08 02 <prop16 LE>` | SUCCESS |
| `SET` (0x01) | `08 01 <prop16 LE> <value>` | SUCCESS |
| `OPEN_FILE` (0x0D) | `08 0D <handle> <fileId>` | SUCCESS |
| `UNBIND_BUFFERS` (0x05) | `08 05 <count> <handle>` | SUCCESS |

Each was confirmed by a status-0 reply with the observable effect it should have; the
rejected alternatives returned `INVALID_ARGUMENT_VALUE(1)`.

The decisive comparisons:

```
SET OPERATING_MODE -> HOST_CONTROLLED
  08 01 03 02 00 00 00     (updater: value right after property)  INVALID_STATE(3)
  08 01 03 00 02 00 00     (value one byte later)                 SUCCESS(0), mode changed

OPEN_FILE handle 1, file 0x22
  08 0D 01 22 00           (ckb-next: args from byte 2)           SUCCESS(0)
  08 0D 00 01 22 00        (updater: args from byte 3)            INVALID_ARGUMENT_VALUE(1)
```

**Why SET appears to have a "gap" byte:** property IDs are almost certainly **16-bit little
endian at bytes 2–3**. That explains the zero byte with no special-casing, and it has to be
true anyway, since `BRAGI_PROTOCOL_VERSION` is 261 and does not fit in a byte. A GET is then
`08 02 <prop16>`, which is exactly what already works.

This also unifies the two-byte / four-byte header business: those are **two protocol
generations**, and the updater bundle documents the newer one. `getHeaderLayout()` selects
four-byte only at protocol version 1.5; this keyboard is the older generation, so every
reference in that bundle sits one byte further along than it should for us. Treat the bundle
as authoritative for *semantics* and ckb-next as authoritative for *byte positions*.

### Operating mode gates everything

Buffer and file commands are refused with `INVALID_STATE(3)` while the keyboard is
`SELF_OPERATED`, which is its power-on state. Switching to `HOST_CONTROLLED` first makes them
work; this is the ordinary software-control handshake iCUE, ckb-next and `k100-color-oxide`
all perform, and `probe-usbdevfs.py` restores `SELF_OPERATED` afterwards.

No session is needed. `startSession` / `HOST_SOFTWARE_SESSION_CONTROL` (27) exists, but
`isReady()` only requires a session when the protocol version is 1.5 — the newer generation.

### Which files exist on this keyboard

Opening `handle 1` and unbinding it again, in `HOST_CONTROLLED` mode:

| File ID | Name | Result |
| --- | --- | --- |
| `0x22` (34) | lighting, RGBRGB ordering | **SUCCESS** — this is the one this board uses |
| `0x01` (1) | lighting, RRRGGGBBB ordering | `INVALID_ARGUMENT_VALUE` |
| 36 | `PAIRED_SUBDEVICES_IDS` | `INVALID_ARGUMENT_VALUE` |
| 54 | `CONNECTED_SUBDEVICES_IDS` | `INVALID_ARGUMENT_VALUE` |

The three failures are all wireless or dongle concepts, so a wired keyboard not having them
is the expected answer rather than a protocol error. `0x22` matches
`BRAGI_RES_ALT_LIGHTING` in ckb-next, described there as being for devices that take
RGBRGBRGB packets, and is the resource `k100-color-oxide` already opens.

A leaked handle is worth knowing about: while handle 1 was left open by a faulty unbind,
every subsequent `OPEN_FILE` returned `INVALID_STATE(3)` instead. Always unbind.

### The complete file ID enum

From `Ft` in the updater bundle, so no sweep was needed to find the firmware file:

| ID | Name |
| --- | --- |
| **3** | **FIRMWARE** |
| 5 | `PAIRING_ID` |
| 6 | `WIRELESS_ENCRYPTION_KEY` |
| 18 | `SECONDARY_CHIP_FIRMWARE` |
| 20 | `BLUETOOTH_DEVICE_ADDRESS` |
| 36 | `PAIRED_SUBDEVICES_IDS` |
| 53 | `PAIRING_ID_V2` |
| 54 | `CONNECTED_SUBDEVICES_IDS` |
| 59 | `SCREEN_MODULE_FIRMWARE` |
| 60 | `DIAL_MODULE_FIRMWARE` |

`FIRMWARE = 3` closes the last unknown in gate 0. It is deliberately **not** opened by the
probe: the ID is known, and opening it may imply write intent.

### Corrected status codes

From the updater bundle's own enum. An earlier version of this document had `1 = BUSY`, which
was wrong:

`0 SUCCESS`, `1 INVALID_ARGUMENT_VALUE`, `2 INSUFFICIENT_BUFFER_SIZE`, `3 INVALID_STATE`,
`4 COMMAND_NOT_SUPPORTED`, `5 PROPERTY_NOT_SUPPORTED`, `6 INVALID_DEVICE_ADDRESS`,
`7 HARDWARE_ERROR`, `9 INVALID_OPERATION`, `10 INVALID_FIRMWARE_FILE`.

### Values read from the keyboard

| Property | Raw | Meaning |
| --- | --- | --- |
| `USB_VID` (0x11) | `00 02 00 1c 1b` | `0x1b1c` |
| `USB_PID` (0x12) | `00 02 00 7c 1b` | `0x1b7c` |
| `FIRMWARE_VERSION` (0x13) | `00 02 00 00 20 06` | **0.32.6** |
| `BOOTLOADER_VERSION` (0x14) | `00 02 00 00 0a 2d` | **0.10.45** |
| `OPERATING_MODE` (0x03) | `00 02 00 01` | **1 = SELF_OPERATED** |
| `HWLAYOUT` (0x41) | `00 02 00 02` | **2 = ISO** |

Versions are three payload bytes read as major, minor, patch — so `00 0a 2d` is 0.10.45.
That decoding is corroborated from outside: ckb-next's `device_bragi.c` records a K100 at
"fw 0.32.6, bld 0.10.45", which matches this keyboard exactly.

`HWLAYOUT` returning 2 matches `LAYOUT_ISO = 2` in ckb-next's enum, and this board is ISO DE,
so the device reports that enum **directly rather than zero-based**. (ckb-next adds one to
the layout byte for the *legacy* protocol; that adjustment does not apply here.)

Note the installed firmware is 0.32.6 while the published packages offer application
0.35.288 and 0.30.257, so this unit is not running either of the images in
`firmware-format.md`.

### What follows below is the earlier diagnosis

Its conclusion about hidraw still holds, confirmed by retest after the power cycle. Only the
usbdevfs section's verdict was overturned. Kept in full because it rules out a long list of
causes, and because the ioctl and endpoint details were verified against ckb-next's source.

### usbdevfs appeared not to work, and the transport was not the fault

`tools/probe-usbdevfs.py` replicates ckb-next's access method with no dependencies, and the
mechanics all succeed: the kernel driver detaches from every interface, each is claimed, and
everything is released and reattached on exit. **Only the transfers fail**, with `ETIMEDOUT`
on the OUT to EP `0x01`.

Verified correct rather than assumed:

- All eight usbdevfs ioctl numbers are computed from the struct sizes and match the kernel's
  values exactly (`USBDEVFS_BULK` `0xC0185502`, `USBDEVFS_IOCTL` `0xC0105512`, and so on).
- `os_usb_interrupt_out()` in ckb-next uses **`USBDEVFS_BULK`** with a
  `usbdevfs_bulktransfer`, which is precisely what the probe does.
- `bragi_fill_input_eps()` puts `P_K100_OPTICAL` in the case group setting
  `bragi_out_ep = 0x1`, `bragi_in_ep = 0x82`. The probe uses those.
- ckb-next's `usbclaim()` claims **every** interface, not just the vendor one. The probe does
  the same.

Ruled out with evidence:

| Hypothesis | Finding |
| --- | --- |
| Wrong report length | hidraw fails identically at 4, 8, 16, 32, 64, 128, 256, 512 and 1024 bytes |
| Wrong report framing | Found and fixed a real bug (magic byte in the report-id slot); behaviour unchanged |
| Wrong endpoint | `0x01`/`0x82` confirmed from ckb-next's source for this exact PID |
| Wrong ioctl or transfer type | Matches ckb-next; all endpoints are Interrupt, which the kernel's BULK ioctl handles |
| Insufficient ownership | All three interfaces detached and claimed |
| Missing privileges | usbfs ACL grants the user `rw`; detach, claim, release and reattach all succeed |
| Another process holding it | Nothing holds any node; no control software was running |
| Runtime power management | `runtime_status` is `active`, `control` is `on` |
| Timeout too short | 1 s and 5 s both fail; ckb-next uses 5 s |

**The one structural difference left** is that ckb-next keeps interrupt IN URBs permanently
submitted on every input endpoint, through `USBDEVFS_SUBMITURB` with
`USBDEVFS_URB_TYPE_INTERRUPT` and a reaper thread, rather than doing a synchronous OUT
followed by a synchronous IN. If the device will not accept a command until the host has an
IN transfer armed to receive the reply, that would produce exactly this symptom.

That remains untested, and it is a hypothesis rather than a diagnosis.

### OpenRGB reaches the same node but also reports no capabilities

OpenRGB 1.0rc3 run with `--list-devices` finds the keyboard on the very node our writes fail
on:

```
2: Corsair K100 RGB Optical
  Description:  Corsair Peripheral V2 HW Device
  Location:     HID: /dev/hidraw13
  Serial:       F50019045EA4FBE0AB70D8010C01D003
  Modes: [Direct] Static
  Zones: Keyboard
  LEDs: 'Keyboard 0' ... 'Keyboard 5'
```

Note "Corsair Peripheral V2", which is OpenRGB's name for Bragi. But it also prints
`[Corsair K100 RGB Optical] device capabilities not found. Please creata a new device
request.` and exposes only six generic LEDs instead of the K100's 193.

This is **ambiguous evidence and should not be read as confirmation either way.** Reading the
serial is a descriptor read, not an OUT transfer, so enumeration succeeding says nothing
about whether commands get through. "Capabilities not found" could mean its queries to the
device failed — matching our results — or simply that this product ID is missing from
OpenRGB's static capability table, in which case it never tried.

**Resolved by reading OpenRGB's source: that run never talked to the keyboard.**
`IsDeviceSupported()` is `device_index < CORSAIR_V2_DEVICE_COUNT` — a pure table-index check
with no device I/O — and the detector deletes the controller before issuing anything when it
misses. The accompanying comment says so: *"A device whose PID is not in the device list has
no zone or LED data to build an RGBController from."* The 1.0rc3 build simply lacks this PID.
So the OpenRGB run is no evidence either way, and the colour test with that build could not
have worked.

### OpenRGB's constants confirm our framing byte-for-byte

Current OpenRGB master *does* list `Corsair K100 RGB Optical V1 1B1C:1B7C`, and its
`CorsairPeripheralV2Controller` gives the authoritative packet shape:

```c
#define CORSAIR_V2_WRITE_WIRED_ID   8      /* the "magic" byte          */
#define CORSAIR_V2_WRITE_SIZE       65     /* 1 report-id byte + 64     */
#define CORSAIR_V2_PACKET_SIZE      1024   /* read buffer               */
#define CORSAIR_V2_TIMEOUT          50     /* milliseconds              */
CORSAIR_V2_CMD_SET = 0x01, CORSAIR_V2_CMD_GET = 0x02

buffer[1] = write_cmd;            /* buffer[0] left 0 as the report id */
buffer[2] = CORSAIR_V2_CMD_GET;
buffer[3] = property;
hid_write(dev, buffer, CORSAIR_V2_WRITE_SIZE);
```

Two consequences. First, every constant matches what this project already uses: magic `8`,
`GET` `0x02`, `SET` `0x01`, report id in byte 0. Second, **OpenRGB writes 65 bytes over plain
hidraw** — which is byte-for-byte what `probe-hidraw.py` sent in its 64-byte case, and that
was rejected with `ETIMEDOUT`.

One detail differs and is worth noting for later: OpenRGB places a SET's value at `buffer[5]`,
leaving byte 4 unused, whereas `k100-bragi` packs the payload from byte 4. GETs are
unaffected, since they carry no payload.

**So the framing has now been independently confirmed against two reference implementations,
and the device still refuses every command.** That points at the state of this particular
keyboard rather than at the host code.

### Also found: a second source for the LED map

`CorsairPeripheralV2Devices.cpp` in OpenRGB master carries a zone definition and a
`corsair_k100_layout` keymap overlay for `1B1C:1B7C`. That is an independent cross-check on
the 174-versus-193 LED discrepancy noted in `qmk-layout.md`, and worth mining regardless of
how the transport question resolves.

### Status codes

Superseded — see "Corrected status codes" above; `1` is `INVALID_ARGUMENT_VALUE`, not `BUSY`.

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
