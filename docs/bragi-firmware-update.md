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

### usbdevfs does not work either, and the transport is not the fault

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
