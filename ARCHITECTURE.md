# Architecture

**K100 RGB (wired), OPX optical switches** — QMK and VIA running on the stock
LPC54605J512, behind Corsair's own bootloader. This describes the thing as built. `NOTES.md`
has the hardware research it rests on, and the documents under `docs/` each cover one part in
depth.

This file was a plan once, with work streams and phase gates and a devboard to buy. All of it
happened or was replaced by something cheaper, so it says what the shape is now. The history
is in the git log and in the documents; a plan kept past its usefulness reads as a description
and misleads.

## Guiding principle

**Nothing K100-specific belongs in QMK core or in the ChibiOS HAL.** Both expose the seams
this needs, and the whole design hangs off them:

- **ChibiOS-Contrib** takes a new MCU family as a directory of clock, PAL, SPI and USB code
  plus a makefile. The port knows nothing about keyboards.
- **QMK** takes a non-standard matrix through `matrix_init_custom()` /
  `matrix_scan_custom()`, non-standard LEDs through an `rgb_matrix` driver, and a keyboard's
  own effects through `rgb_matrix_kb.inc`. The optical front end and the three LED drivers
  slot in behind those.

Editing QMK core or the ChibiOS HAL to accommodate this keyboard is the signal that something
is in the wrong layer. It has happened **seven** times, and each one is a patch with its
reasoning: six against QMK and fifteen against ChibiOS-Contrib, in `qmk/patches/` and
`chibios/patches/`. Three of the ChibiOS ones are defects in the shared LPC USB driver that
were found here; none of them mentions the K100.

## How a build is assembled

There are no submodules and nothing vendored. `qmk/test/build-k100.sh` fetches QMK at the
commits it pins, ChibiOS and ChibiOS-Contrib at the shas QMK pins, and NXP's device headers;
applies the patches; copies this repository's port and keyboard directory into the tree; and
builds. A second run reuses the downloads.

That keeps every piece of this repository an upstreamable change against a named upstream,
rather than a private fork that has to be maintained.

```
chibios/os/hal/ports/LPC/LPC546xx/      the family port: clocks, PAL, SPI, USB, EEPROM, WWDT
chibios/patches/                        fifteen changes to ChibiOS and ChibiOS-Contrib
qmk/platforms/chibios/                  board definition and the EEPROM driver
qmk/keyboards/corsair/k100/             the keyboard: matrix, LEDs, effects, VIA, bootloader
qmk/patches/                            six changes to QMK
qmk/test/build-k100.sh                  fetches, patches, assembles, builds, checks the image
tools/                                  the host side; see below
via/k100-iso-de.json                    the VIA definition, generated
swd/                                    recovery over SWD with a Raspberry Pi
docs/                                   one document per part, each with its measurements
```

## The layers, bottom up

**The part.** LPC54605J512ET100, Cortex-M4 at 180 MHz, 512 KB flash and a real 2 KB EEPROM.
The first 64 KB of flash is Corsair's bootloader, which this firmware does not replace: the
application links at `0x00010000` behind a `0x2c`-byte container header that the bootloader
reads and branches through. `docs/firmware-format.md` has the format, proven by rebuilding
both of Corsair's own images byte for byte.

**The ChibiOS port.** Clock tree to 180 MHz with a 48 MHz USB clock, PAL, SPI and the USB
device driver. The USB work was the expensive part: the controller's endpoint list, its
latched status bits and its handling of completed transfers all needed fixing in the shared
LPC driver. `docs/usb-bringup.md` has what was measured and what was ruled out.

**The matrix.** Eight open-drain strobes on `P1_18..P1_25` against eighteen pulled-up senses
on `P1_0..P1_17`, all on GPIO port 1, read out of the stock firmware's own IOCON setup. The
senses are a contiguous block from bit 0, so one 32-bit port read is a whole row. The OPX
switches present as ordinary contacts — the optical sensing happens inside the switch — but
they need **500 µs** per strobe to settle rather than the 20 µs first guessed, because a
strobe has to light an emitter and let a phototransistor answer. Debouncing is per key, since
several positions with no switch in them flicker continuously. `docs/pin-mapping.md`.

**The lighting.** 190 LEDs across three IS31FL3743B drivers on SPI3 at 12 MHz. Which index
lights which LED is Corsair's wiring and agrees with neither the key matrix nor the physical
layout, so all 190 were measured one at a time with a probe built into the firmware.
`docs/qmk-layout.md`.

**The keyboard.** Six effects of its own, a colour and a brightness for every LED, zones for
the lights that are not keys, the locks shown on their own keys, two effects that react to
typing, a gauge on the ring around the wheel, and the lighting resting when the host sleeps.
What it remembers lives in one EEPROM region owned by one file, `lighting_store.c`.

**VIA.** Protocol 13 over the control endpoint, because the interrupt OUT endpoint this
keyboard should use never acknowledges in a way the host accepts. The definition is generated
rather than written, and declares its own lighting menu: VIA's built-in one sends a position
in a list of all 45 QMK effects, and QMK numbers only the effects a firmware is *built* with.

## Generated, not written

The layout, the keymap, the VIA definition and four small headers come from one script,
`tools/generate-qmk-layout.py`, out of ckb-next's geometry and the stock firmware's own
tables. Neither input is vendored; both are fetched.

A fresh run leaves the working tree unchanged, which is the test rather than the claim.
Everything the generator knows that neither input does — which LED index lights which key,
measured — is a table in the script with the reasoning beside it.

## The host side

`tools/` is one Rust program and a set of Python tools over three small modules: `k100_usb.py`
finds the device, `k100_via.py` speaks VIA's protocol, and `k100_stored.py` knows what the
keyboard stores and where. The rest are presentation on top.

| | |
| --- | --- |
| `k100-update.py` | installs a build: one command, including the trip through the bootloader |
| `k100-bragi` | Corsair's own update protocol, in Rust; what actually writes flash |
| `k100-backup.py` | takes the keymap, macros, colours and settings off the keyboard and puts them back |
| `pack-firmware.py` | wraps a build in Corsair's container, and proves the writer against their images |
| `generate-qmk-layout.py` | the layout, keymap, VIA definition and generated headers |
| `watch-input.py` | whether keystrokes reach the host, and which device they arrive on |

## What is not finished

- **Two of VIA's four layers cannot be reached**, because no key selects them. They exist in
  the stored keymap and are transparent throughout.
- **A keyboard that will not boot has never been recovered as one.** Both halves are proven,
  including on a keyboard running this firmware, and the keys that do it are read by Corsair's
  bootloader before any firmware of ours runs — but nobody has had to put them together in
  anger. `docs/recovery.md`.
- **One keyboard, one layout.** ISO DE, the OPX model, `1b1c:1b7c`. The Cherry MX variant
  `1b7d` runs the same silicon and has not been tried; `1b1c:1bc5` is different silicon and
  none of this applies to it.
