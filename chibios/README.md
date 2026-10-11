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
chibios/os/common/startup/ARMCMx/
    devices/LPC546xx/cmparams.h          core model, vector count, FPU and MPU
    compilers/GCC/ld/LPC54605J512.ld     memory map and the boot ROM checksum
    compilers/GCC/mk/startup_lpc546xx.mk Cortex-M4F startup and the NXP headers
chibios/demos/LPC/LPC54605_GENERIC/
                           a program whose only purpose is to be built
chibios/patches/
    0001-usb-family-seam.patch   the change the shared USB driver needs
chibios/test/
    register-check.c       every SYSCON, IOCON, GPIO and USB field the port uses
    pal-check.c            the exact IOCON word each pad mode produces
    check-registers.sh     compiles both against NXP's real header
    build-demo.sh          fetches upstream, patches, builds, checks the image
```

Because of that destination these files carry the licence their upstream neighbours do rather
than the repository's GPL-3: **Apache-2.0** for the HAL port, and **GPL-3** for the startup files,
whose siblings under `os/common/startup` are GPL-3 where the LPC HAL tree is Apache-2.0. See
`LICENSING.md`.

## Status

**It builds and links as a ChibiOS HAL. Nothing has run on hardware.** There is no devboard yet,
so none of it has been executed — see the phase gates in `ARCHITECTURE.md`. Everything below is
checked three ways: register names against NXP's device header, pad encodings against UM10912, and
the whole port through a real `arm-none-eabi-gcc` build of a demo that touches every layer.

| Piece | State |
| --- | --- |
| `hal_lld` clock tree | builds, register names verified, **never executed** |
| `lpc_registry.h` | interrupt vectors from NXP's `IRQn_Type` |
| PAL / GPIO | builds, pad encoding verified against UM10912, **never executed** |
| System timer (SysTick) | pulled in unchanged from the shared LLD |
| USB device | builds, needs the family-seam patch to the shared driver — see below |
| Startup, vectors, linker script | builds, boot ROM checksum verified in the linked image |
| PAL events / PINT | not started; `PAL_USE_WAIT` and `PAL_USE_CALLBACKS` must be `FALSE` |
| SPI | not started; needs writing, this part uses Flexcomm |
| PLL | not implemented, and not needed yet — see below |

"Builds" is worth more than it sounds: four real defects came out of getting there that no amount
of reading had found, and they are listed under **Building it** below.

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
  actually reach — the 160 KB SRAM at `0x20000000`, not the 32 KB core-coupled `SRAMX` at
  `0x04000000`. Leaving it as plain static data is what keeps it on the right side of that last
  point.

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

## Building it

**The port builds and links.** One command does it:

```sh
nix develop .#firmware
chibios/test/build-demo.sh            # or: build-demo.sh ~/somewhere to keep the tree
```

This repository carries only its own files. ChibiOS, ChibiOS-Contrib and NXP's device headers
belong to other people, so vendoring them would mean redistributing them and pinning them to
whatever they were on the day they were copied. The script fetches them instead, applies the USB
patch, drops this port into place and builds — which is also why the build instructions cannot rot
into something that no longer works, because **they are the thing that runs**. Downloads are
cached, so a second run is quick.

What it produces is `chibios/demos/LPC/LPC54605_GENERIC`, a program whose only purpose is to be
built: it touches the clock tree through `halInit()`, the kernel and SysTick through `chSysInit()`
and `chThdSleep()`, PAL through a pad it drives and a pad it reads, and the USB device driver
through `usbStart()`, which pulls in the endpoint allocator, the interrupt handler and the
family's clocking and power hooks. About 6 KB of flash. **It has never been run.**

Three properties of the linked image are then checked, because each is decided by the linker
rather than by any source file, and each fails *silently* on hardware:

```
vector checksum          ok   first 8 vectors sum to 0x00000000
usb endpoint memory      ok   0x20000c00, 2048 bytes, 256-byte aligned, single 4MB window, in reachable SRAM
```

The first is the boot ROM's image checksum: it sums the first eight vectors and refuses to start
an image unless the result is zero, with no output of any kind to say why. The second is the three
constraints on endpoint memory described above. Unlike the register checks, this one is not
self-verifying — but the checksum parse at least refuses to pass on an empty parse, since the sum
of nothing is also zero.

### What the build needed that was not obvious

Every one of these was found by compiling, not by reading:

- **The part macro has to be defined twice, on the command line.** NXP's
  `fsl_device_registers.h` selects its header from `CPU_LPC54605J512ET100`; `lpc_registry.h`
  detects the sub-family from a bare `LPC54605` that nothing in NXP's headers defines. Neither can
  live in `board.h`, however natural that looks, because `cmparams.h` is reached from `crt1.c` and
  from `vectors_lpc.S`, where no board header is in scope. `board.h` now `#error`s if they are
  missing, so the failure is legible rather than `"No valid CPU defined!"`.
- **The USB compat header has to be included from `hal_lld.h`.** The shared USB driver's header
  defines macros that expand inside ChibiOS's own `hal_usb.c` — `usb_lld_wakeup_host()` among them
  — so the mapped register names must be in scope wherever `hal.h` is, not just in the driver's
  own translation unit. The register check could not have caught this: it includes the compat
  header directly, which is exactly what the build did not do.
- **`LLD/STM` requires `LPC_SYS_FREQUENCY`.** The shared SysTick driver reloads from that name and
  will not build without it. It is the shared pool's contract, so `hal_lld.h` satisfies it rather
  than the driver being patched.
- **ChibiOS 21.11 requires `hal_lld_get_clock_point`.** Most ports answer `0U`; these frequencies
  are all known at compile time, so this one returns them.

### Running it, eventually

The first milestone should be an **LPCXpresso54628 (OM13098)** rather than the keyboard — it has
working SWD and is effectively unbrickable. Its MCU is the LPC54628 at 220 MHz against the
keyboard's LPC54605 at 180 MHz. Same family, so the port applies; keep clock configuration
per-part rather than hardcoded, which is why the frequency is derived from `LPC_FROHF_SEL` and the
dividers instead of being a constant.
