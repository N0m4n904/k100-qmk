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

  SYSCON->AHBCLKCTRLSET[2] = SYSCON_AHBCLKCTRL_USB1D_MASK |
                             SYSCON_AHBCLKCTRL_USB1RAM_MASK;

  SYSCON->FROCTRL |= SYSCON_FROCTRL_USBCLKADJ_MASK;
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

  /* PDRUNCFG bits are power *down* enables, so clearing one powers the block
     up. The dedicated clear register avoids a read-modify-write that could
     drop another block's bit if it changed in between. */
  SYSCON->PDRUNCFGCLR[0] = SYSCON_PDRUNCFG_PDEN_USB1_PHY_MASK |
                           SYSCON_PDRUNCFG_PDEN_USB_RAM_MASK;
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
