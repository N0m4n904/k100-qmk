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
    hal_pal_lld.c          pad and direction programming
    hal_pal_lld.h          pad mode encoding, port and line identifiers, accessors
    lpc_registry.h         sub-family detection, capabilities, interrupt vectors
    lpc546xx_usb.c         USB clocking, transceiver power, endpoint memory
    lpc546xx_usb.h         what the shared USB driver asks of this family
    lpc546xx_usb_compat.h  LPC11Uxx USB register names, mapped to this part
    platform.mk            sources and the shared drivers pulled in
chibios/patches/
    0001-usb-family-seam.patch   the change the shared USB driver needs
chibios/test/
    register-check.c       every SYSCON, IOCON, GPIO and USB field the port uses
    pal-check.c            the exact IOCON word each pad mode produces
    check-registers.sh     compiles both against NXP's real header
```

Because of that destination these files are **Apache-2.0**, matching the rest of
ChibiOS-Contrib's LPC tree, and not the repository's GPL-3. See `LICENSING.md`.

## Status

**Clocks and GPIO are written; nothing has run on hardware.** There is no devboard yet, so none
of this has been executed — see the phase gates in `ARCHITECTURE.md`. What exists is the family
skeleton, a clock tree, and a PAL driver, all of them checked against NXP's device header and
none of them ever run.

| Piece | State |
| --- | --- |
| `hal_lld` clock tree | written, register names verified, **never executed** |
| `lpc_registry.h` | written, interrupt vectors from NXP's `IRQn_Type` |
| PAL / GPIO | written, pad encoding verified against UM10912, **never executed** |
| System timer (SysTick) | pulled in unchanged from the shared LLD |
| USB device | wired in, needs an unbuilt patch to the shared driver — see below |
| PAL events / PINT | not started; `PAL_USE_WAIT` and `PAL_USE_CALLBACKS` must be `FALSE` |
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

## PAL: family local, unlike USB

USB reuses the shared `LLD/USB` driver through a name-mapping header. PAL does not reuse
`LLD/GPIO`, and the reason is worth recording because the two cases look alike and are not.

The GPIO block really is near enough identical — `PIN`, `SET`, `CLR`, `NOT` and `DIR` all exist
here under the same names. But an `iomode_t` is mostly an IOCON pad configuration word, and
**LPC546xx places every IOCON field at a different bit from LPC11Uxx.** `FUNC` widens from three
bits to four and pushes everything above it up:

| Field | LPC11Uxx | LPC546xx |
| --- | --- | --- |
| `FUNC` | bits 2:0 | bits **3:0** |
| pull mode | bits 4:3 | bits **5:4** |
| invert | bit 6 | bit **7** |
| digital (`ADMODE` / `DIGIMODE`) | bit 7 | bit **8** |
| open drain | bit 10 | bit **11** |

That encoding is the substance of a PAL driver. Sharing the dozen register accessors around it
would buy nothing and would mean reworking the only family currently using them, which cannot be
tested here. Every ChibiOS family upstream has its own `hal_pal_lld.h` anyway; the LPC port's
sharing of one is the outlier, and it holds only because LPC11Uxx is so far its only member.

Going family local also made the driver **simpler** than the shared one rather than more complex,
because this part's CMSIS header exposes things LPC11Uxx's does not:

- IOCON is reached as `IOCON->PIO[port][pin]` instead of a hardcoded `0x40044000`, which also
  lifts the shared driver's two-port limit to this part's six.
- Direction uses the atomic `DIRSET` and `DIRCLR` registers, so configuring one pad cannot lose a
  concurrent change to another pad of the same port. LPC11Uxx has to read, modify and write `DIR`.
- A pad group now takes **one** direction write for the whole mask rather than one per pad.

Three further notes:

- **Mode names are deliberately the sibling family's** — `MODE_MODE_PULL_UP`, `MODE_AD_DIGITAL`,
  `MODE_DIR_OUT` — so a board file reads the same across the LPC port even though the bit patterns
  differ. Fields this part lacks, such as LPC11Uxx's separate hysteresis control, are simply
  absent, so a board asking for one fails to build instead of writing a bit that means something
  else here.
- **`PAL_NOLINE` is `0xFFFFFFFF`, not `0`.** Ports here are small integers rather than peripheral
  base addresses, so a zero line identifier is P0_0 — a real, usable pin. Most ChibiOS ports can
  afford the conventional zero; this one cannot, and the shared LPC header does not notice.
- **`DIGIMODE` must be set for any digital use.** A pad left analogue reads nothing at all, and
  the symptom is a dead input rather than an error, so every mode the driver defines sets it and
  `pal-check.c` asserts that it does.

The IOCON and GPIO clock gates moved out of `hal_lld.c` into `_pal_lld_init()`, since gating a
peripheral's clock belongs to that peripheral's driver and `halInit()` reaches PAL immediately
after the clock tree. NXP's header is a trap here: it names the bits `SYSCON_AHBCLKCTRL_GPIOn_MASK`
without saying which of the three `AHBCLKCTRL` registers each belongs to, and they are not all in
the same one — IOCON and GPIO0 to GPIO3 are in `AHBCLKCTRL0`, GPIO4 and GPIO5 in `AHBCLKCTRL2`.

## USB: the registers fit, the plumbing does not

USB decides whether this whole approach works, so it was surveyed before anything was built on it.

**The register interface is genuinely common.** This part's USB0 is the same NXP device IP the
shared `LLD/USB` driver already targets — an endpoint command/status list in SRAM, `DEVCMDSTAT`,
five logical endpoints — and the driver touches exactly ten registers:

`EPSKIP` · `DEVCMDSTAT` · `INTEN` · `INTSTAT` · `INTROUTING` · `INFO` · `EPLISTSTART` ·
`EPINUSE` · `EPBUFCFG` · `DATABUFSTART`

Nine of the ten exist here under the same names. Only the bit macros differ, because NXP's newer
headers suffix them `_MASK` and name one interrupt bit per endpoint direction instead of providing
a single shifted macro. `lpc546xx_usb_compat.h` maps them, and every mapping is compile-checked
against NXP's header, written exactly as the driver writes it.

**The initialisation code is not common, and the register survey did not show that.** On the
strength of the table above this port expected to need one guarded line in the shared driver.
Reading `usb_lld_init()`, `usb_lld_start()` and `usb_lld_stop()` afterwards found LPC11Uxx
hardcoded in five places — the endpoint RAM address and its 2 KB budget, `SYSAHBCLKCTRL`, a USB
PLL brought up by copying the main PLL's configuration, `PDRUNCFG`, and `USB_IRQn`.

Four of the five already sit behind `#if LPC_USB_USE_USB1`, so the shape of the code invites
adding a parallel branch holding LPC546xx's SYSCON writes. **This port does not do that**, because
a shared driver that must be edited for every new part is the wrong shape for a shared driver pool
— and the register table is the evidence that the hardware does not ask for it. Instead the driver
gains a seam and asks the family for the three things that are not register names:

```c
void lpc_usb_clock_init(void);   /* clocks, and any PLL the family needs */
void lpc_usb_power_on(void);     /* transceiver power, at driver start   */
void lpc_usb_power_off(void);
```

plus `LPC_USB_IRQ_NUMBER`, `LPC_USB_HAS_INTROUTING`, `LPC_USB_SRAM_START` and `LPC_USB_SRAM_END`.
After the patch the shared driver names no SYSCON register and no address, and LPC11Uxx's code
lives in a family file of its own, unchanged. The patch is in `chibios/patches`, where its
README explains it; **it applies cleanly to upstream but has never been compiled**, since building
the shared driver needs a full ChibiOS checkout.

`lpc546xx_usb.c` holds this part's answers, and they are short, because clocking USB from `fro_hf`
means there is no PLL to configure at all.

Three details worth knowing:

- **The endpoint memory is an ordinary aligned array, not an address.** LPC11Uxx has a dedicated
  2 KB USB SRAM and its controller can reach nothing else, which is why the shared driver could get
  away with a constant. USB0 here is a bus master with no RAM of its own, so the array must be
  256-byte aligned for `EPLISTSTART`, must sit with its buffers inside one 4 MB region because
  `DATABUFSTART` supplies only the high address bits, and must be in memory the controller can
  actually reach — the SRAM at `0x20000000`, not the core-coupled `SRAMX` at `0`. Leaving it as
  plain static data is what keeps it on the right side of that last point.

- **`INTROUTING` does not exist on this part** and is absent from NXP's `USB_Type`, so a reference
  to it is a compile error rather than a silent misconfiguration. It is written once, as `0`, which
  is the no-op case, so the capability flag costs nothing.

- **`DSUP` is the one uncertain mapping.** The driver spells the suspend bit `DSUP`, which appears
  in neither family's NXP naming; this part calls it `DSUS`. Both of the driver's uses read as
  suspend, so `DSUS` is the reading taken, but it wants confirming against UM10912's `DEVCMDSTAT`
  table before suspend and remote wakeup are trusted. Enumeration does not depend on it.

## The checks

Nothing here has run, so what stands in for running it has to be worth something:

```sh
nix develop .#firmware
chibios/test/check-registers.sh
```

**`register-check.c`** mirrors every SYSCON, IOCON, GPIO and USB expression the port makes and
compiles it against NXP's own device header. Writing a clock tree from a user manual is exactly
where an invented or misremembered register name slips in, and that mistake is invisible until the
chip quietly fails to start.

**`pal-check.c`** goes further, because mirroring has a blind spot: it can only confirm that the
names exist, so a mistake in the port's *own* macros survives it. That file includes the real
`hal_pal_lld.h` and asserts the exact IOCON word every pad mode produces, against bit positions
restated from UM10912 rather than taken from NXP's macros — deriving them from
`IOCON_PIO_*_SHIFT` would only assert that the header agrees with itself.

Both **verify they can fail.** One run compiles a nonexistent register field; the other moves a
pad field back to its LPC11Uxx bit position, which is precisely the mistake a port adapted from
the sibling family would make, and which compiles cleanly while configuring the wrong thing. Both
must be rejected. A check that passes everything proves nothing.

Headers are fetched, not vendored: NXP's and ARM's are theirs to distribute.

As a side effect the checks confirm the struct shapes this project derived from disassembling the
stock firmware — `IOCON->PIO[port][pin]`, `GPIO->DIR[port]`, `GPIO->B[port][pin]` — against NXP's
definitions. Those underpin the pin mapping in `docs/pin-mapping.md`.

## Building for real

Not yet possible here, and it needs four things this repository does not carry:

1. a ChibiOS checkout and a ChibiOS-Contrib checkout, with these files placed in the latter;
2. NXP's MCUXpresso SDK `devices/LPC54605` directory on the include path, for
   `fsl_device_registers.h` and the startup and linker files;
3. **two** part macros on the command line, which is easy to get half right. NXP's
   `fsl_device_registers.h` selects its header from `CPU_LPC54605J512ET100`, while
   `lpc_registry.h` detects the sub-family from a bare `LPC54605` that nothing in NXP's headers
   defines. Passing only the first fails with `unknown/unsupported LPC546xx microcontroller`;
4. a board definition, which for the first milestone should be an **LPCXpresso54628
   (OM13098)** rather than the keyboard — it has working SWD and is effectively unbrickable.

Its MCU is the LPC54628 at 220 MHz against the keyboard's LPC54605 at 180 MHz. Same family, so
the port applies; keep clock configuration per-part rather than hardcoded, which is why the
frequency is derived from `LPC_FROHF_SEL` and the dividers instead of being a constant.
