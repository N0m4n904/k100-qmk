<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Why this firmware does not appear on the USB bus

Everything here was read off the keyboard over SWD, from both this project's
firmware and Corsair's working one, on the same unit within the same hour. It
is a record of measurements, not of reasoning.

The method throughout: run one firmware, halt the part, read the registers,
run the other, read the same registers, and compare. `swd/fault-report.sh
--peek` and `swd/run-from-ram.sh --peek` do the reading; neither writes to
flash.

## What works

The firmware boots and runs. Halted four seconds in, the program counter
resolves to `matrix_scan_custom`, the link register to `chSysPolledDelayX` and
the stack pointer into the process stack: it is through `crt0`, `halInit()` and
`chSysInit()`, into QMK's main loop, scanning the matrix. `CFSR` and `HFSR` are
both zero, so nothing has faulted.

It then resets itself after about ten seconds, by design and not by failure.
A breakpoint on `__NVIC_SystemReset` is reached with the link register inside
`k100_enter_bootloader`, which is the `K100_USB_WATCHDOG_MS` timeout in
`housekeeping_task` handing the keyboard back to the bootloader because USB
never became usable. Reaching that call also proves the EEPROM write and its
read-back succeeded, since the function returns early if either fails — the
route back to the bootloader works on hardware, which it had never been shown
to do before.

## What does not

| `USBHSD` register | Corsair | this firmware |
| --- | --- | --- |
| `DEVCMDSTAT` | `0x10813090` | `0x00410880` |
| → `DEV_ADDR` | 16, assigned by the host | 0 |
| → `DEV_EN` | set | set |
| → `DCON` | set | set |
| → `FORCE_VBUS` | clear | clear |
| → `VBUS_DEBOUNCED` | **set** | **clear** |
| `INFO` frame number | counting | 0 |
| `INTSTAT` | `0x40000008` | `0x00000000` |
| `INTEN` | `0x80000fff` | `0x80000003` |

The controller is enabled and its pull-up is asserted, but it never sees the
bus: no VBUS, no frames, no interrupt. The host agrees — sixty seconds of
watching its USB tree at 20 ms resolution recorded nothing at all.

**The keyboard enumerates at 480 Mbit/s under Corsair's firmware**, so the
high-speed PHY is present and in use on this part, and whatever brings it up is
what is missing here.

## The clock, measured and then ruled out

Corsair clocks USB1 from the dedicated USB PLL; this port clocks it from the
internal FRO.

```
                      Corsair      this firmware
SYSPLLCLKSEL          1 (CLKIN)    0
MAINCLKSELA / B       1 / 2        3 / 0
USB1CLKSEL            2 (USB PLL)  0 (FRO)
USB1CLKDIV            0            0
SYSOSCCTRL            0x00000000   --
USBPLLCTRL            0x00000d3f   0x00000000
USBPLLSTAT            1 (locked)   0
PDRUNCFG[0]           0x02a80540   0x06f80740
PDRUNCFG[1]           0x20000084   0x2000008c
```

The crystal is **12 MHz**, derived rather than assumed: `SYSPLLNDEC`,
`SYSPLLPDEC` and `SYSPLLMDEC` decode through the LFSR encoders in
`lpc546xx_pll.c` to M=15, N=1, P=1, and `AHBCLKDIV` is zero, so the core runs at
fifteen times the input. 180 MHz is this part's ceiling, which leaves 12 MHz as
the only input in specification.

`USBPLLCTRL = 0x00000d3f` is MSEL=63, PSEL=1, NSEL=3, which from 12 MHz gives
**48 MHz** — the same figure the FRO already supplies. The firmware is not
missing a clock; it would be missing an accurate one.

**Tested and refuted.** Writing the crystal and USB PLL into the running part
and breaking at `usb_lld_start` so that USB was initialised with the PLL
already locked and selected changed nothing: `DEVCMDSTAT` stayed at
`0x00410880`, `INFO` stayed at zero, and the host still saw nothing. The clock
source alone is not what stops it.

### One thing that came out of trying

The USB PLL will not lock unless the analog domains are powered. Clearing
`PDRUNCFG[0]` bits 9, 20, 22 and 26 — `VD2_ANA`, `WDT_OSC`, `SYS_PLL`, `VD3` —
is what moved `USBPLLSTAT` from 0 to 1. A PLL is an analog circuit and this
port leaves its supply switched off.

It also has to be configured while powered down. Writing `USBPLLCTRL` to a
running PLL reads back as zero; powering it down first, writing, then powering
it up is what makes the value stick. `lpc_syspll_init()` already says this
about the system PLL.

The sequence that locks it, verified on hardware:

```
PDRUNCFGCLR[0] = 0x04500200   analog domains up
PDRUNCFGCLR[1] = 0x08         crystal oscillator up
SYSOSCCTRL     = 0x00000000   crystal, 1-20 MHz, no bypass
PDRUNCFGSET[1] = 0x02         USB PLL down, to configure it
USBPLLCTRL     = 0x00000d3f
PDRUNCFGCLR[1] = 0x02         USB PLL up
                              wait for USBPLLSTAT bit 0
USB1CLKDIV     = 0
USB1CLKSEL     = 2
```

## Ruled out by measurement

- **Peripheral resets.** `PRESETCTRL[0..2]` read `0x00000000` in both, so
  nothing is held in reset in either.
- **USB clock gates.** `AHBCLKCTRL[2]` has bits 5 and 6, `USB1D` and `USB1RAM`,
  set in both.
- **A PHY register block.** There is none on this part. Every peripheral base in
  the header was listed; the PHY is reached only through SYSCON.
- **The watchdog.** `WWDT->MOD` is zero while this firmware runs, so the
  watchdog is not even running, and the reset that interrupts it is the
  firmware's own.

## What is left

Two candidates, in the order they are worth pursuing.

**The pins.** 41 of the 192 `IOCON` entries differ between the two firmwares.
Among them Corsair drives `P0_23`–`P0_28` at function 6 and `P1_28`–`P1_31`
with a different mode, where this port leaves all of them at the default. If
one of those carries VBUS sense, it would account for the symptom exactly.
Resolving them needs NXP's pin multiplexing table, which this repository does
not have; nothing should be guessed here, because a pin driven into the wrong
function is a hardware risk rather than a failed experiment.

**The driver.** The USB driver in use is ChibiOS's LPC one, written for the
full-speed device controller of the LPC11Uxx family, reached through
`lpc546xx_usb_compat.h`, which maps its register names onto `USBHSD`. The two
blocks are similar but not the same, and this keyboard runs at high speed.
Anything the high-speed controller requires that the full-speed one does not
would be absent, and would look exactly like this: a controller that accepts
`DEV_EN` and `DCON` and then does nothing.
