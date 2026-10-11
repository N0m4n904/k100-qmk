# QMK side

Two separable things live here: **support for the LPC546xx family in QMK at all**, which has
nothing to do with this keyboard, and **the K100 keyboard definition**, which is only useful once
the first exists.

```
qmk/platforms/chibios/
    vendors/LPC/_pin_defs.h          P0_0 .. P5_31, overriding QMK's STM32 pin names
    boards/GENERIC_LPC_LPC54605/     board definition for a bare LPC54605J512
qmk/patches/
    0001-mcu-selection-lpc546xx.patch   the one existing QMK file that needs changing
qmk/keyboards/corsair/k100/          the keyboard itself, generated -- see docs/qmk-layout.md
```

## Adding a new MCU family to QMK needs almost nothing

This was the open question, and the answer is better than expected: **no build *logic* changes,
only four places that have to learn the processor's name.**
`platforms/chibios/platform.mk` resolves everything from variables and searches
ChibiOS-Contrib before ChibiOS for both the startup makefile and
`os/hal/ports/$(MCU_FAMILY)/$(MCU_SERIES)/platform.mk`. So a family that exists in Contrib is
reachable without touching any build logic.

What is needed:

| Piece | Why | Kind |
| --- | --- | --- |
| `mcu_selection.mk` entry | maps `processor: LPC54605` onto the family, startup, linker script and board | **patch** |
| `vendors/LPC/_pin_defs.h` | QMK's default pin names are STM32's, built from a GPIO base address; this part uses a port number | addition |
| `boards/GENERIC_LPC_LPC54605/` | every QMK target must name a `BOARD`, and ChibiOS ships no LPC546xx board to borrow | addition |
| `keyboard.jsonschema` processor enum | or `keyboard.json` fails validation and is discarded whole | **patch** |
| `constants.py` `CHIBIOS_PROCESSORS` | how QMK decides a target is ChibiOS and not AVR | **patch** |
| `chibios_config.h` `CPU_CLOCK` | every family defines one; QMK's timer code needs it | **patch** |

The four patched hunks are all in one patch, which applies cleanly to upstream `master`, verified
against freshly fetched copies. **Three of the four were found by building, and all three fail in
a way that points somewhere else** — a missing schema entry presents as a keyboard with no
layouts, a missing `CHIBIOS_PROCESSORS` entry ends up in the *AVR* bootloader makefile, and a
missing `CPU_CLOCK` is an undeclared identifier in an unrelated header.

Two further facts the entry has to carry, both learned the hard way from building the ChibiOS
demo:

- **The part macro has to be defined twice**, as `CPU_LPC54605J512ET100` for NXP's headers and as
  a bare `LPC54605` for `lpc_registry.h`, and both have to be on the command line rather than in
  `board.h` — `cmparams.h` is reached from `crt1.c` and `vectors_lpc.S`, where no board header is
  in scope. `board.h` `#error`s if either is missing, so the failure is legible.
- **`BOOTLOADER = custom`**, and `EARLY_INIT_PERFORM_BOOTLOADER_JUMP` is `FALSE`. There is nothing
  for the firmware to jump to: entering Corsair's bootloader is a host-side operation over the
  Bragi protocol, which is `docs/bragi-firmware-update.md`.

## It also needs the ChibiOS-Contrib side

QMK builds against its own forks, `qmk/ChibiOS` and `qmk/ChibiOS-Contrib`, rather than upstream.
For the LPC tree those forks are **byte-identical to upstream** — checked, not assumed — so
`chibios/patches/0001-usb-family-seam.patch` applies to either.

The device headers have to be vendored rather than fetched, because QMK vendors everything it
builds with. That is permitted: NXP's device headers carry
`SPDX-License-Identifier: BSD-3-Clause` in the files themselves, whatever the MCUXpresso SDK's
top-level click-through licence says, and ChibiOS-Contrib already keeps `LPC11Uxx.h` on that
basis. So `startup_lpc546xx.mk` looks for `os/common/ext/CMSIS/LPC/$(LPC_PART)/` first and only
falls back to an SDK path, which means the port has no opinion about where they come from.

## Status: it builds

**A QMK firmware for the K100 builds and links.** About 22 KB of flash.

```sh
nix develop .#qmk
qmk/test/build-k100.sh            # or: build-k100.sh ~/somewhere to keep the tree
```

Same arrangement as the ChibiOS demo: the script fetches QMK, QMK's ChibiOS
forks and NXP's headers, applies both patches, drops the port and the keyboard
in, and builds. It is the build instructions, so they cannot rot.

It then checks the linked image, sharing `tools/check-firmware-image.py` with
the ChibiOS build:

```
ok: QMK builds for the K100
vector checksum          ok   80 vectors, first 8 sum to 0x00000000
usb endpoint memory      ok   0x20001600, 2048 bytes, 256-byte aligned, single 4MB window, in reachable SRAM
```

Eighty vectors is sixteen system vectors plus the sixty-four the family
declares. The image is a Cortex-M4 build with a VFPv4-D16 FPU, and
`matrix_init_custom`, `matrix_scan_custom`, `lpc_clock_init`, `_pal_lld_init`
and `lpc_usb_clock_init` are all in it, so every layer of the port is reached.

**It has never run.** Building is not working, and two things in `matrix.c` are
hypotheses that only hardware can settle — see below.

### Submodules: use the pinned commits, not the branch heads

QMK pins `lib/chibios` and `lib/chibios-contrib` to exact commits, and the
branch heads are **not** those commits. Using a head produces a ChibiOS whose
layout QMK's build does not expect, which surfaces as a missing `chlib.h` in
completely unrelated code. The script resolves the pinned sha through the GitHub
API for each submodule.

A stock keyboard failing the same way is what proved that was the cause rather
than anything in this port — worth remembering as the first test whenever a
QMK build breaks here.

`lib/printf` and `lib/lufa` are needed too. LUFA is not obvious: QMK's ChibiOS
USB stack borrows LUFA's HID class headers even on ARM.

### What the matrix does not know

`matrix.c` is written and compiles, and two things in it are unresolved. Both
are one define wide, deliberately.

- **Whether a pressed key reads low.** The pad configuration is established: a
  strobe is asserted by driving it low, the senses are pulled up, so a
  conducting switch pulls its sense down. What is not established is which way
  an OPX *optical* switch conducts — each key interrupts an infrared beam, and
  whether pressing breaks it or clears it cannot be read out of the firmware's
  pin setup. If every key reports held and releases when pressed, invert
  `MATRIX_SENSE_ACTIVE_LOW`.
- **How long a strobe needs to settle.** The stock firmware does not assert and
  sample in one go: its scan is a four-state machine, and the state that samples
  is not the state that asserted, so at least one tick passes between them. That
  is what optical sensing needs. The tick length was not recovered, so
  `MATRIX_STROBE_SETTLE_US` is a generous guess; a matrix that misses keys or
  reports neighbours wants it raised before anything else is suspected.

### Still off: RGB

`rgb_matrix` is disabled in `keyboard.json`. The LED drivers hang off Flexcomm
SPI, which the ChibiOS port does not implement. The generated `rgb_matrix`
configuration is kept because it is correct, so turning it back on is one edit
once the SPI driver exists.
