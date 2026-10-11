/*
 * Compile-time check that every SYSCON field the LPC546xx hal_lld uses really
 * exists in NXP's device header.
 *
 * Writing a clock tree from a user manual is exactly where an invented or
 * misremembered register name slips in, and such a mistake is invisible until
 * the chip silently fails to start. This mirrors every SYSCON expression in
 * hal_lld.c so that a wrong name is a compile error instead.
 *
 * Run it with chibios/test/check-registers.sh.
 */

#include "LPC54605.h"

/* The LPC11Uxx-named USB macros, mapped to this part. Including it here means a
   mapping that does not resolve is a compile error. */
#include "lpc546xx_usb_compat.h"

void lpc546xx_register_check(void);

void lpc546xx_register_check(void) {

  /* Flash wait states. */
  SYSCON->FLASHCFG = (SYSCON->FLASHCFG & ~SYSCON_FLASHCFG_FLASHTIM_MASK) |
                     SYSCON_FLASHCFG_FLASHTIM(4U);

  /* FRO power and high-frequency output, preserving the factory trim. */
  SYSCON->PDRUNCFGCLR[0] = SYSCON_PDRUNCFG_PDEN_FRO_MASK;
  SYSCON->FROCTRL = (SYSCON->FROCTRL &
                     ~(SYSCON_FROCTRL_SEL_MASK | SYSCON_FROCTRL_HSPDCLK_MASK)) |
                    SYSCON_FROCTRL_SEL(0U) | SYSCON_FROCTRL_HSPDCLK(1U);

  /* Main clock selection and divider. */
  SYSCON->MAINCLKSELA = SYSCON_MAINCLKSELA_SEL(3U);
  SYSCON->MAINCLKSELB = SYSCON_MAINCLKSELB_SEL(0U);
  SYSCON->AHBCLKDIV = SYSCON_AHBCLKDIV_DIV(0U);
  while ((SYSCON->AHBCLKDIV & SYSCON_AHBCLKDIV_REQFLAG_MASK) != 0U) {
  }

  /* USB0 device clock. */
  SYSCON->USB0CLKSEL = SYSCON_USB0CLKSEL_SEL(0U);
  SYSCON->USB0CLKDIV = SYSCON_USB0CLKDIV_DIV(0U);
  while ((SYSCON->USB0CLKDIV & SYSCON_USB0CLKDIV_REQFLAG_MASK) != 0U) {
  }

  /* USB0's own clock gate, which the USB driver enables. */
  SYSCON->AHBCLKCTRLSET[1] = SYSCON_AHBCLKCTRL_USB0D_MASK;
}

/*
 * Every IOCON and GPIO expression the PAL driver uses.
 *
 * The PAL header cannot be included here, needing the rest of the HAL, so its
 * register use is mirrored instead -- the same arrangement as the SYSCON block
 * above, and for the same reason: these are the names that a user manual read
 * wrong would get wrong.
 */
void lpc546xx_pal_check(void);

void lpc546xx_pal_check(void) {
  unsigned port = 1U, pad = 18U;
  uint32_t mask = 1U << pad;

  /* Clock gates, in the two different AHBCLKCTRL registers they live in. NXP's
     names do not carry the register index, so getting this wrong is easy and
     silent; the indices follow NXP's own clock driver. */
  SYSCON->AHBCLKCTRLSET[0] = SYSCON_AHBCLKCTRL_IOCON_MASK |
                             SYSCON_AHBCLKCTRL_GPIO0_MASK |
                             SYSCON_AHBCLKCTRL_GPIO1_MASK |
                             SYSCON_AHBCLKCTRL_GPIO2_MASK |
                             SYSCON_AHBCLKCTRL_GPIO3_MASK;
  SYSCON->AHBCLKCTRLSET[2] = SYSCON_AHBCLKCTRL_GPIO4_MASK |
                             SYSCON_AHBCLKCTRL_GPIO5_MASK;

  /* The pad configuration word, built from NXP's field macros exactly as the
     PAL modes build it. This is the part that differs from LPC11Uxx in every
     field position, so it is the part most worth checking. */
  IOCON->PIO[port][pad] = (IOCON_PIO_FUNC(0U) | IOCON_PIO_MODE(2U) |
                           IOCON_PIO_DIGIMODE(1U) | IOCON_PIO_INVERT(0U) |
                           IOCON_PIO_OD(1U) | IOCON_PIO_SLEW(1U) |
                           IOCON_PIO_FILTEROFF(1U)) & 0x0000FFFFU;

  /* Direction, through the set and clear registers rather than read-modify-
     write of DIR. DIR itself is unused by the driver but checked, since the
     pin mapping in docs/pin-mapping.md is written in terms of it. */
  GPIO->DIRSET[port] = mask;
  GPIO->DIRCLR[port] = mask;
  (void)GPIO->DIR[port];

  /* Whole-port access. SET is read as well as written, for the output latch. */
  (void)GPIO->PIN[port];
  (void)GPIO->SET[port];
  GPIO->PIN[port] = mask;
  GPIO->SET[port] = mask;
  GPIO->CLR[port] = mask;
  GPIO->NOT[port] = mask;

  /* Single-pad access, through the byte pin registers. */
  (void)GPIO->B[port][pad];
  GPIO->B[port][pad] = 1U;
}

/*
 * Every USB macro the shared LLD/USB driver uses, written exactly as the driver
 * writes it. These names come from lpc546xx_usb_compat.h, so this fails if any
 * mapping is wrong or if NXP renames a field underneath it.
 */
void lpc546xx_usb_compat_check(void);

void lpc546xx_usb_compat_check(void) {
  unsigned ep = 2U;

  /* The nine registers the driver touches that this part has. INTROUTING is
     absent here, which is the one thing the shared driver needs guarded. */
  LPC_USB->DEVCMDSTAT = USB_DEVCMDSTAT_DEV_EN | USB_DEVCMDSTAT_SETUP |
                        USB_DEVCMDSTAT_DCON | USB_DEVCMDSTAT_DRES_C |
                        USB_DEVCMDSTAT_DSUS_C | USB_DEVCMDSTAT_INTONNAK_CO |
                        USB_DEVCMDSTAT_DSUP |
                        (1U & USB_DEVCMDSTAT_DEVADDR_MASK);
  (void)((LPC_USB->INFO & USB_INFO_FRAME_NR_MASK) >> USB_INFO_FRAME_NR_POS);
  LPC_USB->EPLISTSTART = 0x20000000U & USB_EPLISTSTART_MASK;
  LPC_USB->DATABUFSTART = 0x20000000U & USB_DATABUFSTART_MASK;
  LPC_USB->EPSKIP = 0U;
  LPC_USB->EPINUSE = 0U;
  LPC_USB->EPBUFCFG = 0U;

  /* Interrupt bits, including the shifted forms the driver builds. */
  LPC_USB->INTSTAT = USB_INT_DEV_INT | USB_INT_FRAME_INT | USB_INT_EP_ALL_INT;
  LPC_USB->INTEN = USB_INT_DEV_INT | USB_INT_FRAME_INT;
  LPC_USB->INTEN |= USB_INT_EP(ep * 2U);
  LPC_USB->INTEN |= USB_INT_EP((ep * 2U) + 1U);
  (void)(LPC_USB->INTSTAT & (USB_INT_EPn_INT << (2U * ep)));
}
