# ChibiOS LPC546xx port

QMK reaches ARM parts through the ChibiOS HAL, and new MCU families must land in ChibiOS or
ChibiOS-Contrib rather than in QMK. ChibiOS-Contrib already has an **LPC port** structured as a
shared driver pool plus thin per-family directories, so supporting the K100's LPC54605 means
adding one family directory — not writing a HAL. See `NOTES.md`.

Files here mirror their upstream paths so they can be dropped into, or symlinked from, a
ChibiOS-Contrib checkout and later submitted upstream:

```
chibios/os/hal/ports/LPC/LPC546xx/
    hal_lld.c          clock tree
    hal_lld.h          clock configuration, derived frequencies, flash wait states
    lpc_registry.h     sub-family detection, capabilities, interrupt vectors
    platform.mk        sources and the shared drivers pulled in
chibios/test/
    register-check.c   every SYSCON field the port uses
    check-registers.sh compiles it against NXP's real header
```

## Status

**Clocks are written; nothing has run on hardware.** There is no devboard yet, so none of this
has been executed — see the phase gates in `ARCHITECTURE.md`. What exists is the family
skeleton and a clock tree whose register names are verified against NXP's device header.

| Piece | State |
| --- | --- |
| `hal_lld` clock tree | written, register names verified, **never executed** |
| `lpc_registry.h` | written, interrupt vectors from NXP's `IRQn_Type` |
| System timer (SysTick) | pulled in unchanged from the shared LLD |
| USB device | not started; adapt the shared LLD, same NXP IP |
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
