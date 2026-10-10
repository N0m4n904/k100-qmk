# Architecture

Target: **K100 RGB (wired), OPX optical switches** — QMK running on the stock LPC54605J512,
configurable from VIA. See `NOTES.md` for the hardware research this rests on.

## Guiding principle

Nothing K100-specific belongs in QMK core or in the ChibiOS port. Both already expose the
seams we need, and the whole design hangs off them:

- **ChibiOS-Contrib** takes a new MCU family as a directory of clock code plus a makefile.
  The port knows nothing about keyboards.
- **QMK** takes a non-standard key matrix through `matrix_init_custom()` /
  `matrix_scan_custom()`, and non-standard LEDs through an `rgb_matrix` driver. The optical
  front-end and the RGB drivers slot in behind those seams.

If we ever find ourselves editing QMK core or the ChibiOS HAL to accommodate the K100, that
is the signal that something is in the wrong layer.

## Work streams

Four streams. **A gates everything**; B and C run in parallel; D and E are assembly.

### A. Flashability and recovery — the gate

If the chip cannot be written and reverted, there is no project.

**Status: largely closed, without spending anything or opening the case.** Corsair's public
firmware storage gave up the device manifest and both official images, and the container
format is fully verified — see `docs/firmware-format.md`. What we now know:

- The K100 **officially supports firmware update** (`bragi-props.fwupd` is present).
- A **bootloader operating mode is reachable over Bragi** (`"operating-modes": [...,
  "bootloader"]`), the capability we had assumed was missing because ckb-next stubs it.
- Images are **plaintext, unencrypted, and validated by CRC32/MPEG-2 only** — no signature
  appears anywhere in the container.
- We hold a **verified-authentic recovery image**, reproducible via `tools/fetch-firmware.py`.
- **The keyboard can now be talked to**, over usbdevfs with all interfaces claimed. Framing
  is settled (two-byte header, payload at response offset 3), and the unit reports firmware
  **0.32.6**, bootloader **0.10.45**, `OPERATING_MODE` **SELF_OPERATED** and `HWLAYOUT`
  **ISO**. If commands ever stop being answered, **replug the keyboard first** — that is what
  unblocked this. See `docs/bragi-firmware-update.md`.

Remaining steps, cheapest and least destructive first:

1. **Dump USB descriptors and the interface map.** Confirms the PID (expect `0x1b7c`) and
   every endpoint we can talk to. Extends `k100-color-oxide`.
2. **Read `BRAGI_APP_VER (0x13)` and `BRAGI_BLD_VER (0x14)`.** Trivial, read-only, and it
   proves a bootloader is present and addressable on our specific unit.
3. **Enumerate Bragi resource IDs.** Open a handle per candidate ID and `BRAGI_PROBE_HANDLE`
   it; the probe returns the resource length, so a firmware resource should stand out by
   size. If one is readable, `bragi_read_from_handle()` gives us a recovery image for nothing.
   Read-only and the single highest-value cheap experiment in the project.
4. ~~Read Corsair's web firmware updater and download the official image.~~ **Done.** The
   manifest, both packages and the verified container format are in
   `docs/firmware-format.md`; `tools/fetch-firmware.py` reproduces them.
5. **Extract the update sequence from the updater bundle.** Mostly done — see
   `docs/bragi-firmware-update.md`. **Bootloader entry is found**: the mode field has an
   undocumented third value, `BOOTLOADER = 3`, so entry is `SET OPERATING_MODE 3`
   (`08 01 03 03`), one byte off a command `k100-color-oxide` already sends. The documented
   way back out is `BOOTLOADER → SELF_OPERATED → HOST_CONTROLLED`. Still to trace: how
   `apply-extended` streams the image.
6. **Optionally let the updater run a real update** with `navigator.hid` wrapped to log every
   report, as a cross-check on step 5. Yields a fully framed trace of the official sequence.
   This is Corsair's own supported operation on its own hardware, so the risk is that of a
   normal firmware update, not a novel one.
7. **Open the case and probe for SWD pads and the ISP pins** (ISP, PIO0_4, PIO0_5). Now only
   a fallback, since the Bragi bootloader route looks viable, so it comes last.

Stop condition: SWD disabled *and* ECRP set *and* no restore image obtainable. In that state
a failed write permanently bricks the part, and the honest move is to re-open the choice of
approach rather than push on.

### B. ChibiOS-Contrib LPC546xx family port

**All of this happens on an LPCXpresso54608 devboard, never on the keyboard.** The devboard
has working SWD, is unbrickable in practice, and costs far less than a K100. The port is
finished and proven before the keyboard is ever flashed.

1. NXP SDK CMSIS headers for LPC54605 + startup/linker scripts.
2. `hal_lld.c` — clock tree to 180 MHz and a 48 MHz USB clock. ChibiOS kernel runs.
3. ST driver via SysTick, then a blinking LED: proves clocks and the kernel.
4. PAL driver.
5. USB device driver adapted from `LLD/USB`: enumerate as a HID keyboard.
6. Minimal QMK keymap on the devboard that types one key over USB.

Step 6 is the milestone that retires essentially all the risk in this stream.

### C. K100 hardware reverse engineering

Runs in parallel with B. **Static analysis of the official firmware has already answered the
hardest part of this, without opening the case** — see `docs/pin-mapping.md`.

**Settled:**

- **The matrix is conventional.** 8 open-drain strobes on **P1_18..P1_25**, 18 pulled-up
  senses on **P1_0..P1_17**, all on GPIO port 1. The OPX optical switches present as ordinary
  contacts — the optical sensing happens inside the switch and outputs a plain digital
  signal. There is no IR front-end to design, and QMK's conventional matrix model applies.
  This was the item previously called "the unknown that matters most"; it is no longer
  unknown.

**Still open:**

- **Which `(strobe, sense)` pair is which key.** The mask and lookup tables are built at
  runtime in SRAM; their flash source has not been located. Finding it would replace the
  generated logical matrix with the real electrical one.
- **Per-key RGB + the three-sided LightEdge.** Which driver ICs, on which bus. **FLEXCOMM3**
  is the standout candidate from reference counts.
- **iCUE wheel** — encoder or discrete contacts? Decides whether it belongs in QMK's
  `encoder_map` or the matrix.
- **8 MB external flash** and which bus it hangs off.

Method, cheapest first: keep mining the firmware, since it has been far more productive than
expected. Only then PCB photos, continuity tracing, and a logic analyser on the RGB bus while
the *stock* firmware runs.

### D. QMK keyboard definition

Splits in two, and the upper half turned out not to need B or C at all.

**Done — the data layer.** `LAYOUT_k100_iso_de` (7x22, 123 keys), the default keymap, and
`rgb_matrix.layout` (174 LEDs) are generated from ckb-next's own data, with the VIA
definition alongside. See `docs/qmk-layout.md`. The unlock was realising that the `matrix`
positions QMK wants are a **logical** matrix of our own choosing, since we write the matrix
driver ourselves — so none of this was ever blocked on the electrical layer.

**Remaining — the drivers.** These do need B and C:

- Custom matrix driver implementing the optical scan behind QMK's `matrix_*_custom` seam,
  mapping the real scan onto the logical matrix above.
- `rgb_matrix` driver for the per-key LEDs and the LightEdge.
- Whether the iCUE wheel is a quadrature encoder (and so belongs in `encoder_map` rather
  than the matrix) is still open.

### E. VIA

**Done.** `via/k100-iso-de.json` carries the same matrix positions and dimensions as
`keyboard.json`, verified. Side-load it through VIA's design tab during development; submit
to the VIA repo once the firmware runs.

## Starting with no bench tooling

A large part of stream A and all of the project setup needs nothing but the keyboard and a
USB cable, so that work comes first and the hardware purchases are deferred until they
actually block something.

**Zero-cost, in order:**

1. **Read Corsair's web firmware updater** in Chromium and pull both the Bragi update
   sequence and the official firmware image URL out of it (gate 0 steps 4-5). Highest value
   per effort in the project: no code to write, nothing written to the device, and it can
   close the gate on its own.
2. **Dump USB descriptors and the interface map**, then read `BRAGI_APP_VER` and
   `BRAGI_BLD_VER`. Extends `k100-color-oxide`, which already opens the device over hidapi
   and speaks Bragi.
3. **Enumerate Bragi resource IDs** looking for a readable firmware resource (gate 0 step 3).
   Read-only, and an independent second route to a recovery image.
4. **Collect the reference documents:** LPC546xx user manual (UM10912), datasheet, and the
   errata sheet — the errata matter because they document real USB0 device bugs we will hit.
5. **Stand up the repo and toolchain:** `arm-none-eabi` via Nix, ChibiOS + ChibiOS-Contrib +
   QMK as submodules, and prove the toolchain by building QMK for an existing ChibiOS target.
6. **Write the LPC546xx family directory and get it to compile.** Untestable without
   hardware, but compiling catches a large fraction of the mistakes for free.

**Then acquire, in this order:**

1. **LPCXpresso54628 board (OM13098).** The one purchase that matters, and it does two jobs:
   it is an LPC546xx target to develop the port on, *and* its onboard Link2 probe is
   CMSIS-DAP / J-Link compatible and can debug an **external** target over the P1 header
   (with JP1 fitted), which is exactly what we need later for the K100's SWD pads. So it is
   devboard and debug probe in one. Note its MCU is the LPC54628 (220 MHz) rather than the
   K100's LPC54605 (180 MHz) — same family, so the port applies, but keep clock
   configuration per-part rather than hardcoded.
   Caveat: the older OM13092 is discontinued and OM13098 is its named replacement; verify
   current stock and price before ordering, as distributor listings for this family are
   inconsistent.
2. **Multimeter and a fine-tip soldering iron.** Unblocks stream C continuity tracing and
   attaching to the K100's SWD test points. Cheap.
3. **8-channel USB logic analyser** (an FX2 clone driven by sigrok/PulseView is fine). The
   cheapest item and the last needed. 24 MS/s is ample: a 4000 Hz full-matrix scan implies
   per-strobe activity in the tens of kHz, far below that ceiling.

## Phase gates

| Gate | Proves |
| --- | --- |
| 0 | The chip can be written *and* restored. |
| 1 | QMK types a key on an LPC546xx devboard over USB. |
| 2 | The optical scan mechanism and RGB buses are understood on paper. |
| 3 | Our firmware runs on the actual K100 and enumerates — flash and recovery proven on target. |
| 4 | All keys report correctly through the custom matrix driver. |
| 5 | RGB, wheel and macro keys work. |
| 6 | VIA configures it; port submitted upstream. |

## Repository layout

Proposed. The port and the keyboard definition live in forks referenced as submodules, so
both stay as upstreamable branches rather than private patch sets — which matters because
QMK will not take a keyboard whose MCU support is not upstream in ChibiOS.

```
k100-qmk/
├── NOTES.md                 hardware research
├── ARCHITECTURE.md          this file
├── docs/
│   ├── flashing.md          ISP/SWD entry and the recovery procedure
│   ├── optical-scan.md      how OPX scanning actually works
│   └── hardware.md          chip inventory and net tracing
├── hardware/
│   ├── photos/              PCB photography
│   └── captures/            logic analyser traces
├── tools/                   Rust: descriptor dump, firmware dump, flashing
├── extern/
│   ├── chibios-contrib/     submodule — our LPC546xx branch
│   └── qmk_firmware/        submodule — our keyboards/corsair/k100 branch
└── via/                     VIA keyboard definition
```

Stock Corsair firmware images are copyrighted and must stay out of git — `.gitignore` them
even though we need them locally for recovery.
