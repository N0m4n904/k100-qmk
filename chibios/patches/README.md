# Patches to shared ChibiOS-Contrib code

Everything under `chibios/os/` is new code in a new family directory, so it changes nothing that
exists. These patches are the exception: they change the shared LPC driver pool, which today has
exactly one consumer, LPC11Uxx.

Apply them to a ChibiOS-Contrib checkout with `git apply`, from its root.

| Patch | Touches |
| --- | --- |
| `0001-usb-family-seam.patch` | `os/hal/ports/LPC/LLD/USB/hal_usb_lld.c`, `.../hal_usb_lld.h`, `os/hal/ports/LPC/LPC11Uxx/` |

**None of these have been compiled.** Building the shared USB driver needs a full ChibiOS
checkout, which this repository does not carry and which the register checks deliberately avoid
needing — see `chibios/README.md`. Treat them as a reviewed proposal, not as tested code.

## Why the USB driver needs changing at all

A register-level survey of the shared USB driver was encouraging: it touches ten registers, nine
of which exist on LPC546xx under identical names, so `lpc546xx_usb_compat.h` maps the bit macros
and the driver's register access needs nothing further.

Reading its *initialisation* code afterwards was less encouraging, and corrected that first
impression. The driver hardcodes LPC11Uxx in five places:

| Where | What |
| --- | --- |
| `hal_usb_lld.c:36` | `LPC_USB_SRAM_START` fixed at `0x20004000`, LPC11Uxx's dedicated USB SRAM |
| `hal_usb_lld.c:115` | the allocator's budget, `0x800`, being that SRAM's size |
| `usb_lld_init()` | `SYSCON->SYSAHBCLKCTRL`, then a USB PLL brought up by copying the main PLL's configuration |
| `usb_lld_start()` | transceiver power through `PDRUNCFG`, and `nvicEnableVector(USB_IRQn, ...)` |
| `usb_lld_stop()` | the same in reverse |

Four of those five are already inside `#if LPC_USB_USE_USB1 == TRUE`, so the structure invites
adding a parallel `LPC_USB_USE_USB0` branch holding LPC546xx's SYSCON code instead. The patch
does not do that. A shared driver carrying a branch per family is a driver that has to be edited
for every new part, and the register survey above is evidence that the hardware genuinely does not
need that — only the plumbing around it differs.

So the patch gives the driver a seam and moves LPC11Uxx's code behind it, unchanged:

```c
void lpc_usb_clock_init(void);   /* clocks, and any PLL the family needs */
void lpc_usb_power_on(void);     /* transceiver power, at driver start   */
void lpc_usb_power_off(void);
```

plus three family-supplied values: `LPC_USB_IRQ_NUMBER`, `LPC_USB_SRAM_START` and
`LPC_USB_SRAM_END`. After it, the shared driver names no SYSCON register and no address.

LPC546xx's answers are in `chibios/os/hal/ports/LPC/LPC546xx/lpc546xx_usb.c`, and they are short
because this port clocks USB from `fro_hf` directly: a 48 MHz FRO serves both the core and a
full-speed device, so there is no PLL to configure. That is also why the driver's
`#error "USB Currently requires LPC_MAINCLK_FREQUENCY = 48MHz"` has to move — the constraint is
real but it belongs to the family that needs a PLL to satisfy it, not to the driver.

Two smaller changes come along with it:

- **`INTROUTING` is guarded.** It does not exist on LPC546xx and is absent from NXP's `USB_Type`,
  so the driver does not compile without this. It is written once, as `0`, which is the no-op
  case, so the guard costs nothing.
- **`USB_EPLIST` becomes a macro** rather than a statically initialised pointer. Once
  `LPC_USB_SRAM_START` can be an array address rather than a literal, the old form relies on an
  integer-to-pointer cast being a valid static initialiser, which it is not in standard C.
