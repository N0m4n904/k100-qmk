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

This was the open question, and the answer is better than expected: **QMK's build needs one
entry.** `platforms/chibios/platform.mk` resolves everything from variables and searches
ChibiOS-Contrib before ChibiOS for both the startup makefile and
`os/hal/ports/$(MCU_FAMILY)/$(MCU_SERIES)/platform.mk`. So a family that exists in Contrib is
reachable without touching any build logic.

What is needed:

| Piece | Why | Kind |
| --- | --- | --- |
| `mcu_selection.mk` entry | maps `processor: LPC54605` onto the family, startup, linker script and board | **patch** |
| `vendors/LPC/_pin_defs.h` | QMK's default pin names are STM32's, built from a GPIO base address; this part uses a port number | addition |
| `boards/GENERIC_LPC_LPC54605/` | every QMK target must name a `BOARD`, and ChibiOS ships no LPC546xx board to borrow | addition |

Only the first touches an existing file, so only the first is a patch. It applies cleanly to
upstream `master`, verified against a freshly fetched copy.

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

## Status: unbuilt

**No QMK firmware has been built for this keyboard.** The pieces above are written and the
patches are verified to apply, but that is all — and the ChibiOS demo is a standing reminder of
what that is worth: four real defects appeared the moment it was first compiled, in code that had
already been checked another way.

Two things stand between here and a build:

1. **`matrix_scan_custom()` does not exist.** `keyboard.json` declares `matrix_pins: {custom:
   true}` because the K100 drives its matrix itself: eight strobes on `P1_18`–`P1_25` as
   open-drain outputs against eighteen senses on `P1_0`–`P1_17`, which is 144 intersections for
   119 keys. The pin mapping is in `docs/pin-mapping.md`; the scan routine is the next piece of
   code.
2. **RGB has to be off for a first build.** `keyboard.json` enables `rgb_matrix`, and the LED
   drivers hang off Flexcomm SPI, which the port does not implement. A keyboard that types without
   lighting is a perfectly good first milestone, so RGB waits for the SPI driver.
