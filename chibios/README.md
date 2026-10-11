# ChibiOS LPC546xx port

QMK reaches ARM parts through the ChibiOS HAL, and new MCU families must land in ChibiOS or
ChibiOS-Contrib rather than in QMK. ChibiOS-Contrib already has an **LPC port** structured as a
shared driver pool plus thin per-family directories, so supporting the K100's LPC54605 means
adding one family directory — not writing a HAL. See `NOTES.md`.

Files here mirror their upstream paths so they can be dropped into, or symlinked from, a
ChibiOS-Contrib checkout and later submitted upstream:

```
chibios/os/hal/ports/LPC/LPC546xx/
    hal_lld.c              clock tree
    hal_lld.h              clock configuration, derived frequencies, flash wait states
    lpc_registry.h         sub-family detection, capabilities, interrupt vectors
    lpc546xx_usb_compat.h  LPC11Uxx USB register names, mapped to this part
    platform.mk            sources and the shared drivers pulled in
chibios/test/
    register-check.c       every SYSCON and USB field the port uses
    check-registers.sh     compiles it against NXP's real header
```

Because of that destination these files are **Apache-2.0**, matching the rest of
ChibiOS-Contrib's LPC tree, and not the repository's GPL-3. See `LICENSING.md`.

## Status

**Clocks are written; nothing has run on hardware.** There is no devboard yet, so none of this
has been executed — see the phase gates in `ARCHITECTURE.md`. What exists is the family
skeleton and a clock tree whose register names are verified against NXP's device header.

| Piece | State |
| --- | --- |
| `hal_lld` clock tree | written, register names verified, **never executed** |
| `lpc_registry.h` | written, interrupt vectors from NXP's `IRQn_Type` |
| System timer (SysTick) | pulled in unchanged from the shared LLD |
| USB device | register names mapped, driver not yet wired in — see below |
| PAL / GPIO | not started; adapt the shared LLD |
| SPI | not started; needs writing, this part uses Flexcomm |
| PLL | not implemented, and not needed yet — see below |

## No PLL, deliberately

The first bring-up runs from the FRO with the PLL untouched, because `USB0CLKSEL` can select
`fro_hf` **directly**: a 48 MHz FRO clocks both the core and a full-speed USB device. That
removes the LPC546xx PLL's `SELP`/`SELI`/`SELR` setup from the first milestone entirely, which
is worth doing when the milestone is "does it boot and enumerate at all".

The stock keyboard firmware runs at 180 MHz, so the PLL will be wanted eventually. The
configuration options are already shaped for it: `LPC_MAINCLKSELB` accepts the PLL output and
`hal_lld.h` errors out rather than silently miscomputing a frequency.

Flash wait states scale with the core clock and are set before it rises, since raising the
clock past what the flash can serve fetches garbage.

## USB: the shared driver nearly fits

USB is the layer that decides whether this whole approach works, so it was surveyed before
anything was built on it. The verdict is good. This part's USB0 is the **same NXP device IP** the
shared `LLD/USB` driver already targets — an endpoint command/status list in SRAM, `DEVCMDSTAT`,
five logical endpoints — and the driver touches exactly ten registers:

`EPSKIP` · `DEVCMDSTAT` · `INTEN` · `INTSTAT` · `INTROUTING` · `INFO` · `EPLISTSTART` ·
`EPINUSE` · `EPBUFCFG` · `DATABUFSTART`

**Nine of the ten exist here under the same names.** Only the bit macros differ, because NXP's
newer headers suffix them `_MASK` and name one interrupt bit per endpoint direction instead of
providing a single shifted macro. `lpc546xx_usb_compat.h` maps them, so the shared driver stays
unmodified and upstreamable and the whole family difference sits in one file. Every mapping in it
is compile-checked against NXP's header by the register check, written exactly as the driver
writes it.

Two caveats, both recorded in that header:

- **`INTROUTING` does not exist on this part.** It is the one thing the shared driver cannot
  survive unchanged. On LPC11Uxx it routes USB interrupts between two handlers; LPC546xx has no
  such register, and it is absent from NXP's `USB_Type` — a compile error, not a silent
  misconfiguration. The driver writes it once, as `0`, in `usb_lld_reset()`, which is the no-op
  case, so the fix upstream is a one-line guard:

  ```c
  #if LPC_USB_HAS_INTROUTING
    LPC_USB->INTROUTING = 0;
  #endif
  ```

  with the capability declared in each family's `lpc_registry.h`, which is where that port already
  keeps its capability flags.

- **`DSUP` is the one uncertain mapping.** The driver spells the suspend bit `DSUP`, which appears
  in neither family's NXP naming; this part calls it `DSUS`. Both of the driver's uses read as
  suspend, so `DSUS` is the reading taken, but it wants confirming against UM10912's `DEVCMDSTAT`
  table before suspend and remote wakeup are trusted. Enumeration does not depend on it.

## The register check

Writing a clock tree from a user manual is where an invented register name slips in, and that
mistake is invisible until the chip quietly fails to start. So every SYSCON expression in
`hal_lld.c` is mirrored in `register-check.c` and compiled against NXP's header:

```sh
nix develop .#firmware
chibios/test/check-registers.sh
```

It also **verifies it can fail**, by compiling a deliberately wrong field name and requiring
the compiler to reject it. A check that passes everything proves nothing.

Headers are fetched, not vendored: NXP's and ARM's are theirs to distribute.

As a side effect the check confirms the struct shapes this project derived from disassembling
the stock firmware — `IOCON->PIO[port][pin]`, `GPIO->DIR[port]`, `GPIO->B[port][pin]` — against
NXP's definitions. Those underpin the pin mapping in `docs/pin-mapping.md`.

## Building for real

Not yet possible here, and it needs three things this repository does not carry:

1. a ChibiOS checkout and a ChibiOS-Contrib checkout, with these files placed in the latter;
2. NXP's MCUXpresso SDK `devices/LPC54605` directory on the include path, for
   `fsl_device_registers.h` and the startup and linker files;
3. a board definition, which for the first milestone should be an **LPCXpresso54628
   (OM13098)** rather than the keyboard — it has working SWD and is effectively unbrickable.

Its MCU is the LPC54628 at 220 MHz against the keyboard's LPC54605 at 180 MHz. Same family, so
the port applies; keep clock configuration per-part rather than hardcoded, which is why the
frequency is derived from `LPC_FROHF_SEL` and the dividers instead of being a constant.
