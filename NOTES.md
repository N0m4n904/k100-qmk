# K100 → QMK/VIA — research notes

Scoping notes for replacing the Corsair K100's stock firmware/configuration stack with
QMK and VIA. Everything below is sourced; unverified items are marked as open questions.

## Hardware facts

| Item | Finding | Source |
| --- | --- | --- |
| Main MCU (K100 RGB, wired) | NXP **LPC54605J512** — ARM Cortex-M4, 180 MHz, 512 KB flash, 200 KB SRAM | AnandTech teardown |
| Sibling boards | K65 RGB Mini and K70 RGB PRO use the same LPC54605 family; K70 RGB PRO also carries a discrete 8 MB flash for stored profiles | TechPowerUp teardowns |
| Switch variants | Two SKUs share the chassis: **OPX optical-mechanical** (IR beam, no electrical contacts, 1.0 mm actuation) and **Cherry MX Speed Silver** (1.2 mm) | Corsair launch PR, Corsair support docs |
| Scan/report rate | "AXON Hyper-Processing": 4000 Hz key scan and 4000 Hz HID report rate | Corsair tech specs |
| Other peripherals | 44-zone three-sided RGB LightEdge, iCUE control wheel, 6 macro keys, per-key RGB | Corsair product page |

## Can QMK run on the stock MCU?

Not as-is — but the starting position is much better than "write a HAL from scratch".

QMK reaches ARM parts through the ChibiOS HAL, and new MCU families have to land in ChibiOS
or ChibiOS-Contrib first; QMK does not maintain MCU ports in-tree. ChibiOS-Contrib does have
an **LPC port**, and it is structured as a shared driver pool plus thin per-family
directories:

```
os/hal/ports/LPC/
├── LLD/{GPIO,SPI,STM,UART,USB}/   shared low-level drivers
└── LPC11Uxx/                      hal_lld.c/h + lpc_registry.h + platform.mk  (clocks only)
```

`LPC11Uxx/platform.mk` contributes nothing but `hal_lld.c` and then `include`s the shared
drivers. **Adding LPC546xx means adding one such family directory**, not a whole HAL.

The decisive piece is USB, normally the hardest part of any ChibiOS port, and it looks
reusable. `LLD/USB/hal_usb_lld.c` (682 lines, written for LPC11Uxx) drives the NXP USB
device IP through `DEVCMDSTAT` (`DCON`/`DSUP` bits), `INFO.FRAME_NR` and an endpoint
command/status list in SRAM, with `USB_MAX_ENDPOINTS 5`. The LPC546xx errata describe USB0
device as "10 physical (5 logical) endpoints" and document the same command/status list with
its `T` and `RF/TV` bits. Same IP family, almost certainly.

Per-driver estimate for an LPC546xx family port:

| Driver | Effort | Why |
| --- | --- | --- |
| `hal_lld` (clock tree) | **Write new** | FRO/PLL to 180 MHz plus the 48 MHz USB clock. Reference: NXP SDK `fsl_clock.c`. |
| ST (system timer) | **Trivial** | Plain Cortex-M SysTick. |
| USB device | **Adapt** | Same IP; mostly CMSIS register-name mapping plus checking the LPC546xx USB0 errata. |
| PAL (GPIO) | **Adapt** | IOCON + byte/word pin registers, close to LPC11Uxx. |
| SPI / I²C | **Write new** | LPC546xx uses Flexcomm, unrelated to the LPC11Uxx peripherals. Only needed for RGB drivers and the external flash. |
| UART | **Skip** | Not needed by QMK; use SWO/ITM for debug output. |

Caveat: the shared LLDs are written against LPC11Uxx CMSIS names (`LPC_USB`, `LPC_GPIO`),
while NXP's LPC546xx SDK uses `USB0`, `GPIO`, `SYSCON`. A mapping layer is unavoidable.

QMK's docs still warn that non-STM32 support "is likely out of date, and only supports
ancient MCUs", and QMK "reserves the right to deprecate and/or remove keyboards utilising
support packages that aren't kept up to date with upstream ChibiOS" — so the port should be
written to be upstreamable into ChibiOS-Contrib rather than carried as a private patch.

## Is the stock MCU even flashable?

The MCU has ROM routes in principle:

- The LPC54xxx boot ROM supports **ISP over USB as an MSC device**; NXP documents the procedure.
- The LPC546xx datasheet says boot source / ISP entry is selected by pin state at reset
  (the ISP pin together with PIO0_4 and PIO0_5), so hardware ISP entry or SWD is possible
  if the pins are reachable.

The K100 chassis has **no external ISP pinhole** (verified physically), so hardware ISP entry
means opening the case. The Corsair forum recovery procedure that uses a pinhole was written
for their older boards and does not apply here.

Risks found in NXP community threads: devices can ship with SWD access prohibited and
ECRP set, in which case a failed MSC programming attempt **permanently bricks the part**.
Nothing public documents custom firmware on a Corsair LPC54605 board, so we would be first.

## The K100 speaks Bragi, not the legacy NXP protocol

This matters more than anything else for getting firmware onto the board: Corsair has two
generations of HID protocol, and the published bootloader-entry trick belongs to the old one.

ckb-next supports the K100 and identifies it by these product IDs (`src/daemon/usb.h`):

| PID | Device |
| --- | --- |
| `0x1b7c` | K100 optical (OPX) — **our target** |
| `0x1b7d` | K100 mechanical (Cherry MX Speed) |
| `0x1bc5` | K100 optical, later variant |

All three are listed under `USES_BRAGI` and `USES_BRAGI_JUMBO`. The **Bragi** protocol's magic
byte is `0x08` (`src/daemon/bragi_proto.h`) — exactly the prefix `k100-color-oxide` already
sends, which confirms our board is on the newer protocol.

The legacy protocol (`src/daemon/nxp_proto.h`) is a separate command set with magic `0x07`,
and it is the one with a documented bootloader entry:

```c
#define CMD_SET      0x07   // Write a single field
#define FIELD_RESET  0x02   // Reset the device
#define RESET_BLD    0xaa   // Reboot to bootloader; mounts as a virtual FAT12 device
#define RESET_SLOW   0xf0   // Slow reset sent after firmware update; also exits BLD mode
```

`nxp_reset()` in `src/daemon/device.c` sends `07 02 aa` padded to 64 bytes for `"isp"`, and
`07 02 03` for `"bld"` ("reboots to bootloader and forces an eeprom wipe"). **None of this
applies to the K100** — it is the legacy path, not Bragi.

### What Bragi does and does not expose

`bragi_proto.h` has **no reboot-to-bootloader and no firmware-update command**. ckb-next's
`device_vtable.c` confirms this is deliberate — `vtable_bragi_keyboard` sets

```c
.fwupdate = cmd_io_none,   // no-op
.reset    = cmd_none,      // no-op
```

while the legacy NXP vtables set `.fwupdate = cmd_fwupdate` and `.reset = nxp_reset`. So the
most mature Corsair reverse-engineering project has **not** cracked firmware update on Bragi
devices.

What Bragi *does* give us is encouraging:

- `BRAGI_BLD_VER (0x14)` reads the **bootloader version**, so a bootloader exists and is
  queryable. A comment in `device_bragi.c` records a real K100 at `fw 0.32.6, bld 0.10.45`.
- A general handle/resource model: `BRAGI_OPEN_HANDLE 0x0d`, `BRAGI_PROBE_HANDLE 0x09`,
  `BRAGI_READ_DATA 0x08`, `BRAGI_WRITE_DATA 0x06`, `BRAGI_CONTINUE_WRITE 0x07`,
  `BRAGI_CLOSE_HANDLE 0x05`.
- `bragi_probe_handle()` returns the **length** of the resource behind a handle, and
  `bragi_read_from_handle()` streams it out — so resources are readable, not just writable.
- `k100-color-oxide` already drives this model: its `0x08 0x0D 0x01 0x22` is open-handle on
  resource `0x22` (`BRAGI_RES_ALT_LIGHTING`).

Known resource IDs cover only lighting, pairing ID and encryption key. **If a firmware
resource ID exists, reading it would hand us a recovery image for free**, which makes
resource-ID enumeration the cheapest high-value experiment available.

## Corsair's web firmware updater is the way in

Corsair shipped a **web-based Firmware Update Utility** (`corsair.com/firmware-updater`,
announced April 2025), and it is the best lead in this entire document.

- Corsair's own "everything you need to know" page, last updated 15 January 2026, lists
  **K100 RGB** among supported devices.
- It runs in **Chromium-based browsers only** — Chrome, Edge, Opera; not Firefox or Safari.
- Corsair states it was tested on Windows and macOS, and that "with root permissions [it] may
  also work in Linux, though it has not been thoroughly tested."

The Chromium-only requirement plus direct device access points strongly at **WebHID**. That
is an inference, not a sourced fact, but it is trivially verifiable by opening the page and
checking for `navigator.hid`.

Why this matters more than the iCUE route:

1. **The update logic is JavaScript, not a compiled binary.** The Bragi firmware-update
   sequence can be *read* from the page bundle instead of recovered by disassembly or USB
   capture. No Ghidra, no USBPcap, no Windows.
2. **The firmware image URL is visible in the network tab.** That gets us an official K100
   image — the recovery image gate 0 hinges on — by downloading a file.
3. **A structured capture is a few lines of devtools.** Wrapping `navigator.hid`'s
   `sendReport` / `receiveFeatureReport` logs every exchange already framed by report ID,
   which is cleaner than a raw USB capture.
4. **It runs on NixOS.** Chromium plus hidraw permissions, which this machine needs anyway
   for `k100-color-oxide`.

Steps 1 and 2 are entirely passive — reading a bundle and downloading a file, with nothing
written to the keyboard. Only step 3 involves letting the tool perform a real update, and
that is Corsair's own officially supported operation on its own hardware, so it carries the
same risk as any normal firmware update rather than a novel one.

### What the bundle actually contains (read 2026-10-10)

The tool is a Vite SPA. Two files hold everything:

- `https://www.corsair.com/firmware-updater/config.js`
- `https://www.corsair.com/firmware-updater/assets/index.BZMlYzc5.js` (~1.0 MB)

`config.js` is small and names the backend outright:

```js
window.APP_CONFIG = {
  APP_NAME: "Web FW Updater",
  API_URL:  "https://www.corsair.com/firmware-storage/firmware/public/",
  VERSION:  "2.0.232",
};
```

**WebHID is confirmed**, not inferred: the bundle references `navigator.hid` (8 times),
`requestDevice` and `sendReport`. It uses no `sendFeatureReport` / `receiveFeatureReport`, so
transfers go over plain input/output reports. `6940` appears 21 times — that is `0x1b1c`,
Corsair's vendor ID, in decimal, so expect PIDs as decimal literals too (the K100's `0x1b7c`
is `7036`).

The tool knows two protocol families:

```js
BRAGI = "bragi", SI_XPAND = "sixpand"
```

Most importantly, **devices are described by server-side manifests, not hardcoded logic**.
Each device's manifest carries a `"bragi-props"` object, and the code gates behaviour on its
sub-objects:

| `bragi-props` key | Purpose |
| --- | --- |
| `fwupd` | Firmware update support. Contains `update-time-estimate` and `img-fw-options`. |
| `pairing` | Dongle pairing, with `id-file-ver`. |
| `native-subdevices` | Composite devices (wireless/dial/screen modules). |

Firmware update is refused unless `manifestData["bragi-props"].fwupd` exists — the bundle
throws `"Device does not support firmware updates"` otherwise.

**Consequence:** the K100's firmware-update parameters and image location are *data* we can
fetch from `API_URL`, not logic we have to reverse. Pulling the K100 manifest (VID `6940`,
PID `7036`) is the next concrete action, and it is a plain HTTP GET.

## Approaches considered

**Chosen: QMK on the stock MCU.** Add an LPC546xx family port to ChibiOS-Contrib,
reverse-engineer the optical scan, the RGB drivers and the wheel. Highest fidelity, by far
the largest effort, and a real brick risk that was accepted knowingly.

Rejected alternatives, recorded so the reasoning is not lost:

- **Controller transplant** — keep plate, switches and flex, drive the matrix from an
  RP2040/STM32. Reasonable for the Cherry MX SKU, but OPX switches have no electrical
  contacts at all, so this would mean designing an IR emitter/receiver front-end from
  scratch: a different and harder problem than the port itself.
- **Host-side VIA-compatible layer** — remap on the host over Bragi and expose a VIA-speaking
  virtual device. No brick risk, builds directly on `k100-color-oxide`, but it is not
  firmware, needs a running daemon, and is OS-specific.

## Prior art to mine

- **`ckb-next`** — the richest source. `src/daemon/bragi_proto.h`, `bragi_common.c`,
  `device_bragi.c`, `led_bragi.c` and `usb.h` carry the K100's PIDs, its 193-LED count and the
  whole Bragi command set. `nxp_proto.h` documents the legacy protocol for contrast.
- OpenRGB issue #1519 — K100 RGB USB interface reverse engineering.
- `Ravenslofty/corsair-re` — Corsair HID protocol reverse-engineering tools.
- **CAC2244**, *"K100: Removal of original thick cord to use USB pass-through port as
  input!"* — <https://www.youtube.com/watch?v=MEwbK5u6r_k>. Teardown of the passthrough
  connector, and the source of the lead about extra contacts there possibly carrying reset.
  See `docs/recovery.md`.
- Existing local work: `k100-color-oxide` already drives per-key colour over Bragi (init
  handshake, open-handle on resource `0x22`, `0x08 0x06/0x07` colour writes, 175-LED map taken
  from OpenRGB). ckb-next's `led_bragi.c` uses **193** LEDs for the K100; that gap is now
  explained rather than a defect — see the resolution in `docs/qmk-layout.md`.

## Settled

- Target is the **K100 RGB wired, OPX optical** SKU (PID `0x1b7c`, to be confirmed by reading
  the descriptor off the actual device).
- Goal is **real QMK on the stock LPC54605**, not a host-side substitute.
- Bricking the keyboard is an accepted risk.

## Answered since

Corsair's public firmware storage settled the hardest questions at no cost — full detail in
`docs/firmware-format.md`, reproducible with `tools/fetch-firmware.py`:

- **A recovery image exists and we have it**, verified against Corsair's published sha512.
- **The K100 officially supports firmware update** (`bragi-props.fwupd`), and **a bootloader
  operating mode is reachable over Bragi** — the capability ckb-next stubs out.
- **Images are plaintext and CRC-validated only** (CRC32/MPEG-2 over bytes `[4..EOF]`, stored
  at offset 0). Confirmed against two independent images, 0.30.257 and 0.35.288. No
  encryption and no signature in the container, so a valid custom image is constructible.

## Open questions

- How does `apply-extended` stream the image? Bootloader *entry* is solved
  (`docs/bragi-firmware-update.md`): the mode field's undocumented third value `BOOTLOADER = 3`
  makes it `08 01 03 03`. The upload sequence itself is still untraced.
- Does the bootloader additionally check a signature beyond the CRC? The manifest implies
  not, but only a real write proves it.
- What is the header field at `0x08` (`75113`), and at what flash address is the payload
  placed? The image starts with code at `0x2c`, not a Cortex-M vector table.
- ~~How does the OPX optical scan work electrically?~~ **Answered** by firmware static
  analysis: a conventional 8x18 contact matrix on GPIO port 1, strobes `P1_18..P1_25`, senses
  `P1_0..P1_17`. The optical sensing is internal to the switch. See `docs/pin-mapping.md`.
- Which `(strobe, sense)` pair is which key? The lookup tables are built at runtime in SRAM;
  their flash source is not yet located.
- Which bus and ICs drive the RGB? `FLEXCOMM3` is the leading candidate.
- Are SWD pads exposed on the PCB, and is ECRP set on the shipped part? Now only relevant as
  a fallback.
- **What are the extra contacts inside the USB passthrough connector?** Reported secondhand
  from a K100 USB-C mod video: an additional connector plus a ground contact, speculated to
  relate to reset. If that is a factory programming header exposing reset and the ISP boot
  pins, hardware ISP becomes reachable without a full teardown. See `docs/recovery.md`.
