/*
    ChibiOS - Copyright (C) 2026 k100-qmk contributors
    SPDX-License-Identifier: Apache-2.0

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

#include "lpc546xx_rit.h"

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
 * @note    The wait is **bounded**, and that is not defensive dressing. A
 *          divider only acknowledges when its source clock is actually running,
 *          so a register written for a branch of the clock tree that is not yet
 *          fed never clears its flag. An unbounded wait there hangs inside
 *          @p halInit(), before there is a kernel, a console or a USB device to
 *          say so -- on a part whose bootloader leaves a watchdog armed, the
 *          only external symptom is a reset a second later, which is
 *          indistinguishable from a dozen other faults.
 *
 *          Giving up and carrying on is the better failure: the divider keeps
 *          its previous ratio, which was by definition a working one, and
 *          everything downstream still gets a chance to run.
 *
 * @param[out] reg      the divider register
 * @param[in] value     the raw divider value
 */
static void lpc_set_divider(volatile uint32_t *reg, uint32_t value) {
  /* Generous: the flag clears within a few clocks of either domain, so this is
     orders of magnitude longer than a working divider needs. */
  uint32_t spins = 1000000U;

  *reg = value;
  while (((*reg & SYSCON_AHBCLKDIV_REQFLAG_MASK) != 0U) && (spins > 0U)) {
    spins--;
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
 * @details Brings the core up on the FRO with no PLL. USB1 can take @p fro_hf
 *          directly, so a 48 MHz FRO serves both the core and a full-speed USB
 *          device, which keeps the first bring-up free of PLL configuration.
 *
 * @notapi
 */
void lpc_clock_init(void) {

  /* Flash wait states, in two steps, and the order is the whole point.
     Instruction fetches have to keep working throughout a clock change, and the
     requirement runs in opposite directions depending on which way the clock is
     going: raising it needs the wait states raised first, lowering it needs them
     lowered afterwards. An application cannot satisfy both by writing the final
     value once, because it does not know what frequency it inherited -- a
     bootloader may well have left the core far faster than this configuration
     asks for.

     So: go to the slowest setting, which is safe at any frequency, change the
     clock, and only then drop to the value this configuration actually wants.
     Writing the target value up front is correct if and only if the clock is
     about to rise, and on a part behind a bootloader that is a coin toss. When
     it loses, flash returns garbage and the core faults inside halInit(), long
     before anything exists that could report it. */
  SYSCON->FLASHCFG = (SYSCON->FLASHCFG & ~SYSCON_FLASHCFG_FLASHTIM_MASK) |
                     SYSCON_FLASHCFG_FLASHTIM(LPC_FLASHCFG_WAITSTATES_MAX);

  /* The FRO is powered at reset and the boot ROM has already loaded its trim,
     but ask for it explicitly so a warm start from a low-power mode behaves the
     same as a cold one. */
  SYSCON->PDRUNCFGCLR[0] = SYSCON_PDRUNCFG_PDEN_FRO_MASK;

  /* Select the high-frequency output and enable it, preserving the trim. */
  SYSCON->FROCTRL = (SYSCON->FROCTRL & ~FROCTRL_FREQ_BITS) |
                    SYSCON_FROCTRL_SEL(LPC_FROHF_SEL) |
                    SYSCON_FROCTRL_HSPDCLK(1U);

#if LPC_MAINCLKSELB == LPC_MAINCLKSELB_PLL
  /* Locked before the core is switched onto it, because switching to an
     unlocked PLL stops the core. */
  lpc_syspll_init();
#endif

  /* Main clock: pick the source, then the divider. */
  SYSCON->MAINCLKSELA = SYSCON_MAINCLKSELA_SEL(LPC_MAINCLKSELA);
  SYSCON->MAINCLKSELB = SYSCON_MAINCLKSELB_SEL(LPC_MAINCLKSELB);
  lpc_set_divider(&SYSCON->AHBCLKDIV, SYSCON_AHBCLKDIV_DIV(LPC_AHBCLKDIV));

#if LPC_HAS_USB
  /* USB1 device clock. The controller itself is enabled by the USB driver; this
     only routes a clock to it. */
  SYSCON->USB1CLKSEL = SYSCON_USB1CLKSEL_SEL(LPC_USB1CLKSEL);
  lpc_set_divider(&SYSCON->USB1CLKDIV, SYSCON_USB1CLKDIV_DIV(LPC_USB1CLKDIV));
#endif

  /* The clock is now what this configuration asked for, so the flash can be
     given the wait states that match it. */
  SYSCON->FLASHCFG = (SYSCON->FLASHCFG & ~SYSCON_FLASHCFG_FLASHTIM_MASK) |
                     SYSCON_FLASHCFG_FLASHTIM(LPC_FLASHCFG_WAITSTATES);

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

  /* After the clock, not before: this counter's callers are told it ticks at
     LPC_SYSCLK, so it has to be started at LPC_SYSCLK. Early, too, because what
     is built on it is every microsecond delay in the system -- and on this part
     the alternative is a DWT that reads as a constant, which turns those delays
     into hangs. See lpc546xx_rit.c.

     The result is dropped rather than stored because it is not a fact about the
     past: lpcRitIsRunning() answers the same question against the hardware at
     any later moment, and a caller that cares should ask it then. */
  (void)lpcRitInit();
}

/** @} */
