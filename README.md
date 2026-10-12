# k100-qmk

An attempt to run **QMK and VIA on the Corsair K100 RGB** (OPX optical, ISO DE) in place of
its stock firmware.

---

## ⚠️ Work in progress — read this before running anything

**This project is unfinished, unproven, and may never work.** Nothing here has been run on a
keyboard. There is no working firmware, and there may never be.

**Using anything in this repository is entirely at your own risk.**

Specifically, you should understand before you start:

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
| Firmware distribution and image format | **Understood and verified.** Official images download and validate. |
| Recovery path | **Largely established**, one step unconfirmed on real hardware. |
| Bragi protocol | **Command set recovered.** Bootloader entry found, upload sequence partly traced. |
| QMK layout, keymap, LED map, VIA | **Done** for OPX ISO DE, generated from ckb-next's data. |
| ChibiOS LPC546xx port | Not started. |
| Optical scan and RGB wiring | Unknown. |
| Working firmware | **No.** |

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
tools/
  fetch-firmware.py       downloads and verifies official firmware
  k100-bragi/             Rust Bragi client (read-only ops and mode transitions)
  extract-ckb-keymap/     dumps ckb-next's authoritative layout
  generate-qmk-layout.py  generates the QMK and VIA definitions
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

To talk to the keyboard without root, install `udev/99-corsair-k100.rules`; see `udev/README.md`,
which also records why Corsair's web updater cannot be used on Linux.

## Licensing and attribution

GPL-3, **except** for the files destined for ChibiOS-Contrib (Apache-2.0) and QMK
(GPL-2.0-or-later) and the one tool that links against ckb-next (GPL-2.0-or-later). Each says so
in its own header; **[`LICENSING.md`](LICENSING.md)** explains why each exception is necessary
rather than merely tidy.

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
