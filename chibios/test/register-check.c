/* SPDX-License-Identifier: GPL-3.0-or-later */
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

  /* System PLL, from lpc546xx_pll.c. The dividers hold a shift-register
     sequence rather than the divider value, and the REQ bits are what latch
     it -- a write without them is ignored. */
  SYSCON->SYSPLLCLKSEL = SYSCON_SYSPLLCLKSEL_SEL(0U);
  SYSCON->PDRUNCFGSET[0] = SYSCON_PDRUNCFG_PDEN_SYS_PLL_MASK;
  SYSCON->PDRUNCFGCLR[0] = SYSCON_PDRUNCFG_PDEN_SYS_PLL_MASK;
  SYSCON->SYSPLLCTRL = SYSCON_SYSPLLCTRL_SELR(0U) | SYSCON_SYSPLLCTRL_SELI(8U) |
                       SYSCON_SYSPLLCTRL_SELP(16U);
  SYSCON->SYSPLLNDEC = SYSCON_SYSPLLNDEC_NDEC(0x302U) |
                       SYSCON_SYSPLLNDEC_NREQ_MASK;
  SYSCON->SYSPLLPDEC = SYSCON_SYSPLLPDEC_PDEC(0x62U) |
                       SYSCON_SYSPLLPDEC_PREQ_MASK;
  SYSCON->SYSPLLMDEC = SYSCON_SYSPLLMDEC_MDEC(0x1FFFFU) |
                       SYSCON_SYSPLLMDEC_MREQ_MASK;
  while ((SYSCON->SYSPLLSTAT & SYSCON_SYSPLLSTAT_LOCK_MASK) == 0U) {
  }

  /* USB0's clock gate and transceiver power, from lpc546xx_usb.c. USB0D is bit
     25 of AHBCLKCTRL1, not AHBCLKCTRL0: the device and host gates for the same
     controller live in different registers and NXP's macro names do not say
     which. PDRUNCFG bits are power-down enables, so the clear register is what
     powers the transceiver up. */
  SYSCON->AHBCLKCTRLSET[1] = SYSCON_AHBCLKCTRL_USB0D_MASK;
  SYSCON->PDRUNCFGCLR[0] = SYSCON_PDRUNCFG_PDEN_USB0_PHY_MASK;
  SYSCON->PDRUNCFGSET[0] = SYSCON_PDRUNCFG_PDEN_USB0_PHY_MASK;
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
 * Every Flexcomm and SPI field the SPI driver uses. The peripheral is reached
 * two ways -- as SPI_Type from base+0x400 and as FLEXCOMM_Type for the function
 * selector near the end of the same 4 KB window -- so both views are exercised.
 */
void lpc546xx_spi_check(void);

void lpc546xx_spi_check(void) {
  SPI_Type *spi = (SPI_Type *)SPI3_BASE;
  FLEXCOMM_Type *fc = (FLEXCOMM_Type *)SPI3_BASE;

  /* Function selection, and the two fields that say whether it is allowed. */
  (void)(fc->PSELID & FLEXCOMM_PSELID_SPIPRESENT_MASK);
  (void)(fc->PSELID & FLEXCOMM_PSELID_LOCK_MASK);
  fc->PSELID = FLEXCOMM_PSELID_PERSEL(2U);

  /* Bus clock gate and function clock. Flexcomm 0 to 7 are in AHBCLKCTRL1
     from bit 11, which is why the driver computes the bit rather than naming
     it; FCLKSEL is indexed by block. */
  SYSCON->AHBCLKCTRLSET[1] = 1UL << (11U + 3U);
  SYSCON->FCLKSEL[3] = SYSCON_FCLKSEL_SEL(0U);

  /* Master configuration, clock polarity and phase, and chip-select polarity. */
  spi->CFG = SPI_CFG_ENABLE_MASK | SPI_CFG_MASTER_MASK | SPI_CFG_CPHA_MASK |
             SPI_CFG_CPOL_MASK | SPI_CFG_SPOL0_MASK | SPI_CFG_LOOP_MASK;
  spi->DIV = SPI_DIV_DIVVAL(11U);

  /* FIFO setup. The empty bits are commands rather than state. */
  spi->FIFOCFG = SPI_FIFOCFG_EMPTYTX_MASK | SPI_FIFOCFG_EMPTYRX_MASK;
  spi->FIFOCFG = SPI_FIFOCFG_ENABLETX_MASK | SPI_FIFOCFG_ENABLERX_MASK;
  spi->FIFOTRIG = 0U;

  /* A data word carries the frame width, which of four chip selects to assert,
     whether to drop the reply, and whether this ends the transfer. */
  spi->FIFOWR = 0xA5U | SPI_FIFOWR_LEN(7U) | SPI_FIFOWR_EOT_MASK |
                SPI_FIFOWR_RXIGNORE_MASK |
                SPI_FIFOWR_TXSSEL0_N_MASK | SPI_FIFOWR_TXSSEL1_N_MASK |
                SPI_FIFOWR_TXSSEL2_N_MASK | SPI_FIFOWR_TXSSEL3_N_MASK;
  (void)(spi->FIFORD & SPI_FIFORD_RXDATA_MASK);

  /* FIFO status, including the two error bits the driver clears. */
  (void)(spi->FIFOSTAT & (SPI_FIFOSTAT_TXNOTFULL_MASK |
                          SPI_FIFOSTAT_RXNOTEMPTY_MASK |
                          SPI_FIFOSTAT_TXEMPTY_MASK));
  spi->FIFOSTAT = SPI_FIFOSTAT_TXERR_MASK | SPI_FIFOSTAT_RXERR_MASK;

  /* FIFO interrupt enables. */
  spi->FIFOINTENSET = SPI_FIFOINTENSET_TXLVL_MASK | SPI_FIFOINTENSET_RXLVL_MASK;
  spi->FIFOINTENCLR = SPI_FIFOINTENCLR_TXLVL_MASK | SPI_FIFOINTENCLR_RXLVL_MASK;
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

  /* The suspend bit, whose mapping rests on the two families placing it at the
     same bit number under different names: LPC11Uxx's header calls bit 17 DSUP
     and this one calls bit 17 DSUS. Pinned so a renumbering is caught. */
  _Static_assert(USB_DEVCMDSTAT_DSUS_SHIFT == 17U,
                 "DSUS has moved; the DSUP mapping rested on it being bit 17");

  /* Interrupt bits, including the shifted forms the driver builds. */
  LPC_USB->INTSTAT = USB_INT_DEV_INT | USB_INT_FRAME_INT | USB_INT_EP_ALL_INT;
  LPC_USB->INTEN = USB_INT_DEV_INT | USB_INT_FRAME_INT;
  LPC_USB->INTEN |= USB_INT_EP(ep * 2U);
  LPC_USB->INTEN |= USB_INT_EP((ep * 2U) + 1U);
  (void)(LPC_USB->INTSTAT & (USB_INT_EPn_INT << (2U * ep)));
}
