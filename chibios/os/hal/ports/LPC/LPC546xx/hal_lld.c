/*
    ChibiOS - Copyright (C) 2026 k100-qmk contributors

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/**
 * @file    LPC546xx/hal_lld.c
 * @brief   LPC546xx HAL subsystem low level driver source.
 *
 * @addtogroup HAL
 * @{
 */

#include "hal.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/*
 * The FRO's trim values are factory-programmed into the OTP/boot ROM and the
 * boot code copies them into FROCTRL. Changing the output frequency must keep
 * them, so the frequency bits are read-modify-written rather than assigned.
 */
#define FROCTRL_FREQ_BITS       (SYSCON_FROCTRL_SEL_MASK |                  \
                                 SYSCON_FROCTRL_HSPDCLK_MASK)

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local variables and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/**
 * @brief   Applies a clock divider and waits for it to take effect.
 *
 * @details Each divider register carries a @p REQFLAG that the hardware clears
 *          once the new ratio is live. Returning before it clears would run the
 *          next configuration step against an unsettled clock.
 *
 * @param[out] reg      the divider register
 * @param[in] value     the raw divider value
 */
static void lpc_set_divider(volatile uint32_t *reg, uint32_t value) {

  *reg = value;
  while ((*reg & SYSCON_AHBCLKDIV_REQFLAG_MASK) != 0U) {
  }
}

/*===========================================================================*/
/* Driver interrupt handlers.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   LPC546xx clock initialization.
 *
 * @details Brings the core up on the FRO with no PLL. USB0 can take @p fro_hf
 *          directly, so a 48 MHz FRO serves both the core and a full-speed USB
 *          device, which keeps the first bring-up free of PLL configuration.
 *
 * @notapi
 */
void lpc_clock_init(void) {

  /* Flash wait states first. Raising the core clock before the flash can keep
     up would fetch garbage; lowering it afterwards is harmless. */
  SYSCON->FLASHCFG = (SYSCON->FLASHCFG & ~SYSCON_FLASHCFG_FLASHTIM_MASK) |
                     SYSCON_FLASHCFG_FLASHTIM(LPC_FLASHCFG_WAITSTATES);

  /* The FRO is powered at reset and the boot ROM has already loaded its trim,
     but ask for it explicitly so a warm start from a low-power mode behaves the
     same as a cold one. */
  SYSCON->PDRUNCFGCLR[0] = SYSCON_PDRUNCFG_PDEN_FRO_MASK;

  /* Select the high-frequency output and enable it, preserving the trim. */
  SYSCON->FROCTRL = (SYSCON->FROCTRL & ~FROCTRL_FREQ_BITS) |
                    SYSCON_FROCTRL_SEL(LPC_FROHF_SEL) |
                    SYSCON_FROCTRL_HSPDCLK(1U);

  /* Main clock: pick the source, then the divider. */
  SYSCON->MAINCLKSELA = SYSCON_MAINCLKSELA_SEL(LPC_MAINCLKSELA);
  SYSCON->MAINCLKSELB = SYSCON_MAINCLKSELB_SEL(LPC_MAINCLKSELB);
  lpc_set_divider(&SYSCON->AHBCLKDIV, SYSCON_AHBCLKDIV_DIV(LPC_AHBCLKDIV));

#if LPC_HAS_USB
  /* USB0 device clock. The controller itself is enabled by the USB driver; this
     only routes a clock to it. */
  SYSCON->USB0CLKSEL = SYSCON_USB0CLKSEL_SEL(LPC_USB0CLKSEL);
  lpc_set_divider(&SYSCON->USB0CLKDIV, SYSCON_USB0CLKDIV_DIV(LPC_USB0CLKDIV));
#endif

  /* The IOCON and GPIO clock gates belong to the PAL driver and are enabled in
     _pal_lld_init(), which halInit() reaches immediately after this. Gating a
     peripheral's clock is that peripheral's driver's business, and splitting it
     that way keeps this function to the clock tree itself. */
}

/**
 * @brief   Low level HAL driver initialization.
 *
 * @notapi
 */
void hal_lld_init(void) {

  lpc_clock_init();
}

/** @} */
