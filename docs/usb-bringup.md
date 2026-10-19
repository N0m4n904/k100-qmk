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

### The pins are not it

41 of the 192 `IOCON` entries differ between the two firmwares, which looked
like a lead until the pin was identified. **`USB1_VBUS` is a dedicated pin on
this part, not a function selectable through `IOCON`** — NXP's pin table lists
it as a fixed function, with the note to tie it to `VDD` where it is unused. A
firmware cannot fail to configure it, so none of those 41 entries can account
for `VBUS_DEBOUNCED` staying clear.

### Nor is anything else in SYSCON

All 512 words of SYSCON were read from both firmwares and compared. Beyond the
clock-tree differences above, only five registers differ, and none of them is a
USB control:

| offset | register | Corsair | this firmware | |
| --- | --- | --- | --- | --- |
| `0x000` | `CTRL` | `0x00000002` | `0x00000000` | undocumented in the header |
| `0x04c` | `ASYNCAPBCTRL` | `0x00000001` | `0x00000000` | the asynchronous APB bridge |
| `0x428` | `USB1CLKSTAT` | `0x00000001` | `0x00000003` | **read-only status** |
| `0x680` | `STARTER[0]` | `0x20000000` | `0x00000000` | wake-up start logic |
| `0x684` | `STARTER[1]` | `0x00010000` | `0x00000000` | wake-up start logic |

`USB1CLKSTAT` is the only USB one and it reports rather than controls:
`DEV_NEED_CLKST` in both, and `HOST_NEED_CLKST` additionally set here, which
describes an uninitialised host block and is a symptom rather than a lever.

## Three more things tried on the running part, and refuted

Each was applied with `--poke` to the firmware while it ran, and each reading
was attributed by reading the program counter afterwards and checking it was
still in SRAM. That check exists because the first attempt produced what looked
like a complete success — `VBUS_DEBOUNCED` set, device address 31, the frame
counter moving — which turned out to be **Corsair's firmware**: the run had
lasted eleven seconds, this firmware hands the keyboard back after ten, and
every register read after that point described the code in flash.

| tried | result |
| --- | --- |
| Crystal and USB PLL up, selected **before** USB init, by breaking at `usb_lld_start` | `USBPLLSTAT` 1, `DEVCMDSTAT` unchanged, no frames |
| PHY powered down and up again **after** everything else, as NXP's order has it | `DEVCMDSTAT` `0x08410880`, `VBUS_DEBOUNCED` still clear, no frames |
| Both together: crystal, USB PLL, then the PHY last | `USBPLLSTAT` 1, `VBUS_DEBOUNCED` still clear, no frames |
| `FORCE_VBUS` set, so the controller assumes bus power regardless | `DEVCMDSTAT` `0x00410c80`, still no frames |

The last one matters most. With `FORCE_VBUS` set the controller no longer needs
to detect anything and still receives nothing, so **VBUS is a symptom and not
the cause**: the PHY is not driving the lines at all, which is why the host
never sees the pull-up and why nothing ever comes back.

## What is left: the driver

The USB driver in use is ChibiOS's LPC one, written for the full-speed device
controller of the LPC11Uxx family and reached through
`lpc546xx_usb_compat.h`, which maps its register names onto `USBHSD`. The two
blocks are similar but not the same, and **this keyboard runs at 480 Mbit/s**.
Anything the high-speed controller requires that the full-speed one does not is
therefore absent, and would look exactly like what is observed: a controller
that accepts `DEV_EN` and `DCON` and then does nothing at all.

NXP's own bring-up for this part, from `CLOCK_EnableUsbhs0DeviceClock` in their
SDK, also has an order this port does not follow:

1. gate the USB1 device clock off
2. `POWER_SetUsbPhy()`, which powers the PHY's own VD5 supply
3. select and divide the clock source, then wait 50 µs
4. enable the USB1 device and USB1 RAM clocks
5. **power up the PHY last**, with `POWER_DisablePD(kPDRUNCFG_PD_USB1_PHY)`

For the FRO path it additionally sets `FROCTRL.HSPDCLK` and
`FROCTRL.USBCLKADJ`. `HSPDCLK` is already set here, and in Corsair's firmware,
by whatever ran before. `USBCLKADJ` is set in neither — Corsair does not need
it, because they do not use the FRO.

## POWER_SetUsbPhy, disassembled

### First, a correction about where to read it

The flash dump is **not** a copy of Corsair's application. It was taken before
the keyboard was restored, so from `0x00010000` it holds this project's own
bricked firmware; only the first 64 KB, the bootloader, is Corsair's. A function
found in the dump's application region and reported here as Corsair's
`POWER_SetUsbPhy` was this port's own USB bring-up code — which is why poking
its sequence into the running firmware changed nothing: the firmware already
does it.

Corsair's application is available as a file instead:
`Heracles_Application_Firmware_v0.35.288.bin`, which
`tools/fetch-firmware.py` downloads and checksum-verifies. Disassembled at
`0x10000`, past its `0x2c` container header, it is the right thing to read.

### What it does

At `0x0002d0f8`:

```
mov.w r3, #0x40000000        SYSCON
mov.w r2, #0x10000000        VD5, bit 28
str.w r2, [r3, #0x630]       PDRUNCFGCLR[0] -- power VD5 up
ldr   r2, =0x40020000
ldr   r3, [r2, #0x54]        read 0x40020054
lsls  r3, r3, #22            test bit 9
bpl   -6                     loop until bit 9 is set
bx    lr
```

So it powers the domain and then **waits for it to report ready**. Its sibling
at `0x0002d0dc` does the same for VD3 and waits on bit 5 of the same word.

**`0x40020000` does not appear anywhere in NXP's header for this part.** It is
an undocumented power-management block, which is consistent with these functions
living in a binary library rather than in the SDK's source.

### What it reads

| | `0x40020054` | `PDRUNCFG[0]` |
| --- | --- | --- |
| Corsair | `0x00000fff` | `0x02a80540` |
| this firmware | `0x00000fcf` | `0x06f80740` |

Twelve domains report ready in Corsair's; bits 4 and 5 are missing here, and
bit 5 is the one their VD3 routine waits on — matching `PDRUNCFG[0]` bit 26,
`VD3`, which this port leaves powered down.

**Bit 9 is set in both.** VD5, the USB PHY's own domain, is powered and ready
in this firmware too, so the PHY's supply is not what stops it. That remains
unexplained.

## The driver, compared against NXP's

NXP's `usb_device_lpcip3511.c` is written for this exact controller and is
available in source, so the comparison is line by line rather than by
inference.

### What matches

- **The clock and power sequence.** `lpc_usb_clock_init()` already does exactly
  what `CLOCK_EnableUsbhs0DeviceClock` does: gate the USB1 device clock off,
  power VD5, settle, power the PHY and its PLL, settle, gate the device and RAM
  clocks back on. Measurement agrees — VD5 reports ready.
- **The buffer addressing.** The two generations of this IP split a buffer
  address between `DATABUFSTART` and the endpoint entry differently, and the
  shared driver handles both; this port defines `LPC_USB_EPLIST_ADDR_BASE`,
  which selects the form the high-speed controller needs.
- **`EPBUFCFG`**, which is zero in both this firmware and Corsair's.
- **The pull-up.** `usb_lld_connect_bus()` sets `DCON`, and the measured
  register confirms it.

### What differs

**`DEVCMDSTAT` is written with read-modify-write throughout.** The register
carries write-one-to-clear status bits — `DCON_C` at 24, `DSUS_C` at 25,
`DRES_C` at 26 — and NXP's driver masks them off before every write. A `|=`
reads them back as one and writes them, clearing them. This silently discards
connect, suspend and reset events, which are exactly what tells the driver a
host is there.

That is a real defect and worth fixing on its own account. It does not explain
the current symptom, because it concerns software noticing the bus rather than
the transceiver driving it.

**`FORCE_NEEDCLK` is never set.** NXP sets it unless low-power mode is
configured. **Tested on the part and refuted**: with the bit set the controller
still receives nothing, and `USB1CLKSTAT` does not change.

### Confirmed by reading Corsair's code

Their application masks the status bits before every write to `DEVCMDSTAT`:

```
ldr   r3, [r2, #0]
bic.w r3, r3, #0xef000000      bits 24-27 and 29-31, the write-one-to-clear ones
orr/bic ...
str   r3, [r2, #0]
```

So the read-modify-write above is not a theoretical hazard. Both NXP's driver
and the firmware that works on this keyboard avoid it deliberately.

Their controller init, at `0x00025300`, also disables before it configures:

```
bic.w r3, r3, #0x10800         DCON and LPM_SUP
bic.w r3, r3, #0x80            DEV_EN
str   r3, [r6, #0]             controller off
bl    0x25294                  configure
orr.w r3, r3, #0x80            DEV_EN back on
```

**`LPM_SUP` is cleared there and never cleared here.** It is set in this
firmware before its driver touches anything — the handover state, measured at a
breakpoint ahead of `usb_lld_start`. **Tested and refuted**: clearing it on the
running part changes nothing.

Their endpoint command and status list sits at `0x40100a00`, with
`EPLISTSTART` at `0x40100b00`; this port puts both at `0x40100000`.

### Where that leaves it

The transceiver does not drive the bus, and it does so with its supply powered
and ready, its clock running, `DEV_EN` set, `DCON` set, `FORCE_VBUS` set,
`FORCE_NEEDCLK` set and `LPM_SUP` cleared. Every one of those was measured on the part rather than
assumed. Nothing in the clock tree, the power domains, the pins, the peripheral
resets, SYSCON as a whole, or the driver's register programming accounts for
it.

## Watching the firmware that works

A hardware breakpoint reaches flash, which is the half of the address space
this core's breakpoint unit can match, so Corsair's own firmware can be stopped
at a chosen instruction. Two readings settle things this document previously
had wrong.

**At their application's entry point, `0x00012568`:**

```
DEVCMDSTAT  0x00400800     VBUS_DEBOUNCED clear
PDRUNCFG1   0x20000085     bit 0 set -- the transceiver is powered DOWN
```

The bootloader hands over with the transceiver off, and `DEVCMDSTAT` reads
exactly what this firmware starts from. So the handover state is the same for
both, and running from SRAM is a faithful harness rather than a special case.
A comment in `lpc546xx_usb.c` claiming the domain was live at handover has been
corrected; it was reasoning, and it was wrong.

**At `0x0002534c`, after they configure and before they set `DEV_EN`:**

```
DEVCMDSTAT  0x10400000     DEV_EN clear, DCON clear, VBUS_DEBOUNCED SET
```

So the transceiver reports bus power before the controller is enabled at all.
`VBUS_DEBOUNCED` depends on neither `DEV_EN` nor `DCON` — only on the
transceiver being alive. Whatever this firmware fails to do, it fails before
that point.

## Their bring-up, and the order this port had backwards

From `0x00022940`:

```
FROCTRL        clear 0x7d008000, set 0x41000000    HSPDCLK and USBCLKADJ
               delay
AHBCLKCTRLSET2 = 0x20                              USB1D
AHBCLKCTRLSET2 = 0x40                              USB1RAM
PDRUNCFGCLR1   = 0x01                              transceiver -- last
```

**The clock gates go on first and the transceiver's power comes last.** NXP's
`CLOCK_EnableUsbhs0DeviceClock` ends the same way. This port had it the other
way round, powering the transceiver and then clocking the controller, which is
the order neither reference uses. That is now fixed.

**It did not fix USB.** `DEVCMDSTAT` still reads `0x00410880` with no frames.
The order was wrong and is worth having right; it was not what stopped it.

### USBCLKADJ: tried, harmful, reverted

Setting `FROCTRL.USBCLKADJ`, which NXP's FRO path does, made the oscillator
trim itself towards USB traffic that never arrives: `FREQTRIM` fell from `0xaf`
to `0x00` within seconds. That is the trim for the oscillator the whole part
runs from. It brought no benefit, so it is not set.

It does not persist: a normal boot restores `FROCTRL` to `0x40af178a`, checked
on the keyboard afterwards, which still enumerates at 480 Mbit/s.

## Their USB init, from the top

Comparing both firmwares halted on the *same instruction* -- the one that
powers the transceiver -- showed Corsair's `PDRUNCFG[1]` bit 0 already clear.
They had powered it earlier, somewhere between the application's entry and
there. That narrowed the search to a few hundred instructions, and it ends at
`0x000128ec`:

```
str.w r3, [r4, #0x634]      PDRUNCFGCLR1 = 1   power the transceiver, first
bl    0x22914                                  clock setup
str.w r3, [r4, #0x228]      AHBCLKCTRLSET2 = 0x10   the USB1 HOST clock on
ldr.w r5, [r4, #0x428]      USB1CLKSTAT
ands.w r5, r5, #2           HOST_NEED_CLKST
bne   back                  spin until it clears
str.w r3, [r4, #0x248]      AHBCLKCTRLCLR2 = 0x10   host clock off again
```

**They enable the USB host controller's clock, wait for `USB1CLKSTAT` bit 1 to
clear, and switch it off again** -- on a keyboard that is a device and never a
host. It is a handshake that brings the transceiver up, and it appears in no
documentation.

It matches a measurement taken before anyone was looking for it: this firmware
reads `USB1CLKSTAT` as `0x3` where Corsair's running firmware reads `0x1`.
`HOST_NEED_CLKST` is stuck set here and is exactly the bit they wait on.

**Performing the handshake does clear it**, from `0x3` to `0x0`, both after
this firmware's USB init and before it. **It does not wake the transceiver**:
`DEVCMDSTAT` stays at `0x00410880`, and the host still records nothing.

## What is still different at that instruction

With both halted where the transceiver is powered, everything else that differs
is the clock tree and what hangs off it:

```
                   Corsair      this firmware
USB1CLKSEL         2            0
SYSPLLCLKSEL       1            0
MAINCLKSELA / B    1 / 2        3 / 0
USBPLLCTRL         0x00000d3f   0x00000000
USBPLLSTAT         1            0
SYSPLL{N,P,M}DEC   programmed   0
PDRUNCFG[1]        0x20000084   0x2000008f
0x40020054         0x00000fff   0x00000fcf
USBHSD LPM         0x00100000   0x00200000
```

`PDRUNCFG[1]` is the readable summary: Corsair have the transceiver, the USB
PLL and the crystal oscillator all powered; this firmware has none of the three.

## Where it stands