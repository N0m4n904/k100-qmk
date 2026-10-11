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

  /* Peripheral clocks the port touches. */
  SYSCON->AHBCLKCTRLSET[0] = SYSCON_AHBCLKCTRL_IOCON_MASK |
                             SYSCON_AHBCLKCTRL_GPIO0_MASK |
                             SYSCON_AHBCLKCTRL_GPIO1_MASK;
  SYSCON->AHBCLKCTRLSET[1] = SYSCON_AHBCLKCTRL_USB0D_MASK;

  /* Peripherals the keyboard uses, so their bases are checked too. */
  (void)IOCON->PIO[1][18];
  (void)GPIO->PIN[1];
  (void)GPIO->DIR[1];
  (void)GPIO->B[1][18];
}
