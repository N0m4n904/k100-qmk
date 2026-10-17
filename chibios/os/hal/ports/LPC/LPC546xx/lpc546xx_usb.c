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
 * @file    LPC546xx/lpc546xx_usb.c
 * @brief   LPC546xx USB clocking, power and endpoint memory.
 *
 * @addtogroup USB
 * @{
 */

#include "hal.h"

#if (HAL_USE_USB == TRUE) || defined(__DOXYGEN__)

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/**
 * @brief   A short busy wait that works before the kernel is started.
 *
 * @details Deliberately not @p chSysPolledDelayX, which is the obvious choice
 *          and is wrong here. That counts cycles with the DWT, and ChibiOS only
 *          enables the DWT in @p port_init() from @p chSysInit() -- while this
 *          driver's clock setup is reached from @p usb_lld_init(), which
 *          @p halInit() calls *before* @p chSysInit(). The cycle counter is
 *          still frozen at that point, so the delay never finishes and boot
 *          hangs in @p halInit with no sign of why.
 *
 *          A plain loop instead. The count is approximate -- the compiler is
 *          free to schedule it however it likes -- and that is acceptable
 *          because the requirement is a floor, not a period: NXP asks for "at
 *          least 50 us" after powering the PHY.
 *
 * @param[in] microseconds  the minimum to wait for
 */
static void usb_rough_delay(uint32_t microseconds) {
  /* Four cycles an iteration is a deliberate underestimate, so the loop runs
     for longer than asked rather than shorter. */
  const uint32_t iterations = (LPC_SYS_FREQUENCY / 4000000U) * microseconds;

  for (volatile uint32_t spin = 0U; spin < iterations; spin++) {
  }
}

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Clocks the high-speed device controller and its SRAM.
 *
 * @details Two gates, not one, and both live in @p AHBCLKCTRL2: the controller
 *          at bit 5 and its dedicated SRAM at bit 6. NXP's header names these
 *          @p USB1D and @p USB1RAM without saying which of the three
 *          @p AHBCLKCTRL registers they belong to, and the bit numbers collide
 *          with @p SRAM2 and @p SRAM3 in @p AHBCLKCTRL0 -- enabling them in the
 *          wrong register would clock the wrong thing and leave USB dead. The
 *          register comes from NXP's own clock driver, where
 *          @p kCLOCK_Usbd1 is @p CLK_GATE_DEFINE(AHB_CLK_CTRL2, 5).
 *
 *          @p USBCLKADJ makes the FRO trim itself against USB's start-of-frame
 *          timing instead of relying on its factory trim, which is what NXP's
 *          @p CLOCK_EnableUsbhs0DeviceClock does when the FRO is the USB clock
 *          source. The frequency bits are set by @p hal_lld.c and left alone
 *          here.
 *
 * @notapi
 */
void lpc_usb_clock_init(void) {

  /* NXP's order, from CLOCK_EnableUsbhs0DeviceClock: the controller's gate goes
     off before its clock source is touched, and back on afterwards, with time
     to settle in between. Enabling the gate across a clock change is what their
     sequence is careful to avoid. */
  SYSCON->AHBCLKCTRLCLR[2] = SYSCON_AHBCLKCTRL_USB1D_MASK;

  /* VD5 first, and this is the one that matters: it is the *supply* for both
     USB PHYs, and without it the transceiver has no power at all. A device that
     never powers its PHY never pulls up D+, so the host sees an empty port --
     no descriptor read, no error, nothing in the kernel log. That was this
     port's symptom for three flash cycles.

     This is the step NXP performs in POWER_SetUsbPhy(), which ships only inside
     their binary power library and so looked unreachable. It is not: the domain
     is a documented PDRUNCFG bit, described in NXP's own device header as
     "PDEN_VD5 - Power control both USB0 PHY and USB1 PHY". */
  SYSCON->PDRUNCFGCLR[0] = SYSCON_PDRUNCFG_PDEN_VD5_MASK;
  usb_rough_delay(50U);

  /* Then the transceiver and its PLL inside that domain, and the endpoint SRAM.
     PDRUNCFG bits are power *down* enables, so clearing powers up. */
  SYSCON->PDRUNCFGCLR[0] = SYSCON_PDRUNCFG_PDEN_USB1_PHY_MASK |
                           SYSCON_PDRUNCFG_PDEN_USB1_PLL_MASK |
                           SYSCON_PDRUNCFG_PDEN_USB_RAM_MASK;

  SYSCON->FROCTRL |= SYSCON_FROCTRL_USBCLKADJ_MASK;

  /* NXP waits at least 50 us here for the PHY and its PLL. A polled delay
     because this runs before the kernel is started. */
  usb_rough_delay(100U);

  SYSCON->AHBCLKCTRLSET[2] = SYSCON_AHBCLKCTRL_USB1D_MASK |
                             SYSCON_AHBCLKCTRL_USB1RAM_MASK;
}

/**
 * @brief   Powers up the transceiver and the endpoint SRAM.
 *
 * @details Separate from clocking because the HAL powers the transceiver only
 *          when the driver is started, so that a configured but stopped USB
 *          does not present itself on the bus.
 *
 * @note    The high-speed PHY also sits behind a voltage domain that NXP brings
 *          up in @p POWER_SetUsbPhy(), which ships only as part of their binary
 *          power library -- there is no source for it and no register
 *          documentation in the device header. This port does not attempt it.
 *          It does not have to: on this part the boot ROM and Corsair's
 *          bootloader have already enumerated at high speed by the time an
 *          application runs, so the domain is live at handover. That is why
 *          @p lpc_usb_power_off() below does nothing.
 *
 * @notapi
 */
void lpc_usb_power_on(void) {

  /* Idempotent, and deliberately a repeat of what lpc_usb_clock_init() already
     did. The HAL separates clocking from transceiver power so that a configured
     but stopped USB does not present itself on the bus; on this part the clock
     cannot be brought up without the PHY, so the split does not survive and
     this is what is left of it. */
  SYSCON->PDRUNCFGCLR[0] = SYSCON_PDRUNCFG_PDEN_VD5_MASK |
                           SYSCON_PDRUNCFG_PDEN_USB1_PHY_MASK |
                           SYSCON_PDRUNCFG_PDEN_USB1_PLL_MASK;
}

/**
 * @brief   Does not power the transceiver down.
 *
 * @details Deliberately empty. Powering this PHY down is not symmetric with
 *          powering it up: bringing it back needs the voltage-domain sequence
 *          described above, which this port cannot perform. Switching it off
 *          would therefore be a one-way operation that makes USB unrecoverable
 *          until the next reset, which is a far worse outcome than leaving a
 *          stopped controller's transceiver powered.
 *
 * @notapi
 */
void lpc_usb_power_off(void) {
}

#endif /* HAL_USE_USB == TRUE */

/** @} */
