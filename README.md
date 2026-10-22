# k100-qmk

An attempt to run **QMK and VIA on the Corsair K100 RGB** (OPX optical, ISO DE) in place of
its stock firmware.

---

## ⚠️ Work in progress — read this before running anything

**This project has already bricked one keyboard.** That keyboard was recovered, but only by
opening the case and attaching a programmer to the PCB. The firmware now runs, enumerates, types,
lights and holds up under use, and VIA reads and writes its keymap. Nothing here should be
mistaken for finished -- the lighting has eleven LEDs with no place in the layout, and the
vendor interface reaches the firmware by a different route than it was meant to.

**Using anything in this repository is entirely at your own risk.**

Specifically, you should understand before you start:

- **This has already bricked a K100.** Not hypothetically. A custom image was flashed whose
  route back to the bootloader had never been observed working, and which called that route
  *before* the held-key escape hatch. The call faulted, both ways back were lost together, and
  the keyboard stopped enumerating entirely — no USB at all, and no key held at power-up
  changed that. Recovering it took opening the case and wiring a programmer to the board. The
  post-mortem is in [`docs/recovery.md`](docs/recovery.md) and the procedure that got it back
  is in [`swd/`](swd/README.md). **It need not have.** Corsair's bootloader keeps the keyboard
  if `B` and `L` are held while it is plugged in — decoded out of the bootloader and measured
  since. The key tried at the time was Escape, which is the one key that guarantees the
  opposite.
- **You can permanently destroy your keyboard.** The K100's MCU can ship with read-out
  protection and debug access disabled, in which case a failed firmware write leaves no way
  to recover it. This is not a theoretical risk; it is documented for this chip family.
- **No warranty, no guarantee, no support.** Not of correctness, not of fitness for anything,
  not that any procedure described here will do what it says.
- **This will void your warranty** and is not endorsed by or affiliated with Corsair in any
  way.
- **The protocol findings are reverse-engineered** from Corsair's own publicly served
  software and from [ckb-next](https://github.com/ckb-next/ckb-next). They are inferences
  about undocumented behaviour and some are explicitly marked as unconfirmed.

If you are not willing to lose the keyboard, **do not write anything to it.**

Read [`docs/recovery.md`](docs/recovery.md) first. It explains what is and is not recoverable,
and the two rules that keep a mistake survivable.

---

## Where it stands

| Area | State |
| --- | --- |
| Firmware container format | **Proven.** The writer rebuilds both official images byte for byte. |
| Firmware distribution | **Understood and verified.** Official images download and validate. |
| Reflashing a *healthy* keyboard | **Proven on hardware.** An official image was written, applied and verified, many times. |
| Recovery from firmware that will not boot, over USB | **Half of it is proven.** The firmware's own route back to the bootloader works and has been watched doing so -- for a while it worked rather too well, firing on every cold boot because it could not tell a host that had not woken up from USB that was broken. Writing a replacement image from the bootloader over USB is **not implemented**; flashing here goes over SWD. Relying on this route before it had ever been seen to work cost a keyboard. |
| Recovery from firmware that will not boot, over SWD | **Proven on hardware.** A bricked K100 was dumped, erased and restored to stock with a Raspberry Pi and four jumper wires. See [`swd/`](swd/README.md). |
| Bragi protocol | **Command set recovered and the write path exercised** on a real unit. |
| QMK layout, keymap and VIA definition | **Generated** for OPX ISO DE from ckb-next's data and the stock firmware's own tables, lighting included; a fresh run reproduces all three files byte for byte. The VIA definition carries the labels for the keycodes this keyboard adds, in the order the keyboard's header declares them, so the app names them instead of showing a hex code, and a lighting menu holding the effects this firmware is actually built with -- VIA's own menu sends the position in a list of all 45, which is a different number from the one QMK gives a build with two. `tools/via-lighting.py` drives the same four controls, and all four were measured written and read back. The definition is not loaded into the application on trust either: `tools/via-definition-check` runs it through `@the-via/reader`, the package the application reads definitions with, and its schema guard, its transform and the form it stores all accept this one. |
| ChibiOS LPC546xx port | **Runs on the keyboard.** Boots through `crt0`, `halInit()` and `chSysInit()` into QMK's main loop, from SRAM and from flash alike. |
| Key matrix pins | **Proven on hardware**, and this time read against `bcdDevice` so it is this firmware and not the bootloader's own keyboard: 67 presses across 16 keys arrived while the device reported `0.01`. The settle time is 500 us: these are optical switches, and the 20 us first guessed was far too short for an emitter and phototransistor to answer. |
| The two rotary controls | **Both turn and both are the right one.** Neither rotation is matrix-scanned, so they are encoders on `P1_28`-`P1_31`, read out of the stock firmware's pin-interrupt configuration. Which pair is the iCUE wheel and which the volume roller was a guess from the order the image's own tables list them in, and turning them confirmed it: the wheel sets the lighting's brightness, the roller the volume. The roller's directions came out inverted and are crossed in `encoders.c`; `ENCODER_DIRECTION_FLIP` would have turned the wheel round with it. |
| Lighting | **Working.** 190 LEDs across three IS31FL3743B drivers on SPI3, solid white by default and settable like any other QMK keyboard. It stayed dark long after the Flexcomm, pin mux, bus configuration, frames and enable outputs had all been matched against Corsair's own firmware while that firmware was lighting the keyboard. What was missing was a function register at `0x25` that they write with a different value for one of the three drivers: these parts take their scan timing from one of their number, and a set in which every one is told to follow has nobody leading. Global current and the scaling page were missing with it, and either alone is also a keyboard that stays dark with a working bus. [`docs/pin-mapping.md`](docs/pin-mapping.md) has the comparison. |
| Status icons | **Four of the six report something true.** Num, Caps and Scroll Lock come from the host; Win Lock is the keyboard's own GUI lock, and the lock key beside the iCUE wheel is mapped to toggle it. The icon follows that state: with the lock set in the running firmware over SWD, its channel in the LED buffer went to `0xff`, and back to `0x00` when the state was put back. **That a press on the key reaches the state is not shown** -- that needs someone at the keyboard. Scroll Lock is driven and may never light, because whether the host reports it is the host's business and a Wayland desktop generally does not. Mute stays dark deliberately -- the host tells a keyboard about its locks and nothing else, so lighting it from our own volume keys would report what was last asked for rather than what is so. They survive the lighting being turned off, which took an effect of our own: QMK draws indicators only while an effect is running, and its way of having none disables the lighting task, so the VIA menu's "Off" selects an effect that paints black instead. |
| Eleven LEDs nobody can name | **Not placed, and not the light bars.** Indices 179 to 189 of Corsair's own LED table are real channels, three each on the third driver, but ckb-next's geometry has neither a position nor a name for them, so no effect reaches them and they have been dark since init. The bars, the ring around the iCUE wheel and the logo are placed and do light. |
| VIA | **Working, over the control endpoint.** Reads and writes the keymap on the ordinary HID path, which is the one the VIA application uses: protocol version 13, row 7 column 2 reads `700b` against the same value taken independently out of the keyboard's EEPROM, and a keycode written to a spare entry reads back changed. The interrupt OUT endpoint it would normally use takes the packet and answers it but never gives the host an acknowledgement it accepts, so the interface is given no OUT endpoint and the host sends its requests as `SET_REPORT` instead -- an ordinary shape for a HID interface, and the one direction on this keyboard that was never in doubt. Three defects in the shared LPC USB driver were found and fixed on the way. [`docs/usb-bringup.md`](docs/usb-bringup.md) has the measurements and what was ruled out. |
| Route back to the bootloader | **Solved twice over.** From the keyboard alone: hold `B` and `L` while plugging in, decoded out of the bootloader's own startup matrix scan and measured. From the firmware: `Fn+Esc` forces a watchdog reset, because `WDTOF` — the flag such a reset leaves behind — is what the bootloader keys on, not the EEPROM mode word this project first read it as. Over SWD it is still one command. [`docs/recovery.md`](docs/recovery.md) has all three, and why the EEPROM reading was wrong. |
| USB | **Working.** Enumerates at 480 Mbit/s as `1b1c:1b7c`, `bcdDevice 0001`, and the kernel binds all three HID interfaces. |
| Working firmware | **Yes.** It runs from flash, enumerates, binds three HID interfaces, types and lights. Typing was measured at 5204 key events across four minutes of continuous use. It used to hang within a second of being used; the cause was a system-timer priority that is not a priority on a part with three priority bits, which put the tick above the kernel's own interrupt mask. [`docs/scheduler-stall.md`](docs/scheduler-stall.md) has the measurements, the fix and the eight explanations that were refuted on the way. |
| Telling our firmware from Corsair's | **Only `bcdDevice` does it.** Corsair's bootloader presents a working keyboard of its own under the same vendor and product id, so typing proves nothing on its own. A whole 104-key sweep was once collected from the bootloader and credited to this firmware. `tools/watch-input.py` now refuses to attribute keystrokes it cannot place. |
| Test hardware | **One K100**, running this firmware, with its SWD header wired to a Raspberry Pi. |
| Corsair's bootloader | **Dumped.** 64 KB read out over SWD and checked: its vector checksum is valid, so the image is complete. Not redistributable, so not in this repository. |

Start with [`NOTES.md`](NOTES.md) for the research and
[`ARCHITECTURE.md`](ARCHITECTURE.md) for the plan.

## Hardware

Corsair K100 RGB, wired, **NXP LPC54605J512** (ARM Cortex-M4, 180 MHz, 512 KB flash).

| Variant | USB ID | Platform |
| --- | --- | --- |
| OPX optical — the target | `1b1c:1b7c` | NXP LPC54605 |
| Cherry MX Speed | `1b1c:1b7d` | NXP LPC54605, byte-identical firmware |
| OPX optical, "later variant" | `1b1c:1bc5` | **NXP i.MX RT — a different platform** |

**`1bc5` is not a revision of this board, it is different silicon**, so none of this work applies
to it: not the ChibiOS port, not the pin mapping, not a single firmware address. Its firmware is
`Splinter_App`, an i.MX RT image running from external QSPI flash, where `1b7c`'s is `Heracles`,
an LPC54605 image running from internal flash. See `docs/firmware-format.md`.

The K100 Air is a different device again and none of this applies to it either.

## Layout

```
docs/          research write-ups; recovery.md is the one to read first
swd/           talking to the controller over SWD with a Raspberry Pi: recovering
               a keyboard that will not boot, and debugging one that does not yet
tools/
  fetch-firmware.py       downloads and verifies official firmware
  k100_container.py       Corsair's firmware container, read and written
  pack-firmware.py        wraps a build in that container; --verify proves it can
  k100-bragi/             Rust Bragi client: identify, mode transitions, flash
  extract-ckb-keymap/     dumps ckb-next's authoritative layout
  generate-qmk-layout.py  generates the QMK and VIA definitions
  via-definition-check/   checks the generated definition with VIA's own reader
chibios/       the ChibiOS-Contrib LPC546xx port, and its checks under test/
qmk/keyboards/corsair/k100/   generated QMK keyboard definition
via/                          generated VIA definition
udev/          rules so k100-bragi runs without root
flake.nix      dev shells: default, .#keymap, .#firmware, .#qmk
```

## Building

```sh
nix develop          # Rust tooling
nix develop .#keymap # Qt, for the keymap extractor
nix develop .#firmware && chibios/test/check-registers.sh   # the ChibiOS port's checks
nix develop .#firmware && chibios/test/build-demo.sh        # build the port
nix develop .#qmk      && qmk/test/build-k100.sh            # build the keyboard firmware
```

The two build scripts fetch ChibiOS, QMK and NXP's headers themselves rather than this
repository vendoring them, so they are the build instructions as well as the build.

A QMK build is a bare `.bin`; the K100's bootloader starts an application from a header it
expects to find in flash ahead of it, so the build has to be wrapped before it means anything
to the keyboard:

```sh
tools/fetch-firmware.py --out fw/                      # Corsair's own images
tools/pack-firmware.py --verify fw/*.bin                # prove the writer against them
tools/pack-firmware.py .build/corsair_k100_default.bin \
    --elf .build/corsair_k100_default.elf --out k100.bin
tools/pack-firmware.py --check k100.bin                 # before going near the keyboard
```

To talk to the keyboard without root, install `udev/99-corsair-k100.rules`; see `udev/README.md`,
which also records why Corsair's web updater cannot be used on Linux.

## Licensing and attribution

GPL-3 by `LICENSE`, **except** for the files destined for ChibiOS-Contrib (Apache-2.0) and QMK
(GPL-2.0-or-later), the one tool that links against ckb-next (GPL-2.0-or-later), and one file of
NXP's (BSD-3-Clause). Each says so in its own SPDX header, which is the only thing that travels
with a file into another project.

**[`LICENSING.md`](LICENSING.md)** explains why each exception is necessary rather than merely
tidy, and `tools/check-licensing.py` fails if the files and that document disagree.

Protocol and layout knowledge derives substantially from
[ckb-next](https://github.com/ckb-next/ckb-next) and
[OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB), both GPL. Corsair firmware images are
copyrighted and are **not** redistributed here — `tools/fetch-firmware.py` downloads them
from Corsair's own public endpoint instead.

Hardware findings credited to others:

- **[CAC2244](https://www.youtube.com/@cac2244)** — *"K100: Removal of original thick cord to
  use USB pass-through port as input!"*
  ([video](https://www.youtube.com/watch?v=MEwbK5u6r_k)). Teardown of the USB passthrough
  connector, and the source of the lead that its extra contacts may carry reset — which would
  make hardware ISP reachable without a full teardown. See
  [`docs/recovery.md`](docs/recovery.md).
