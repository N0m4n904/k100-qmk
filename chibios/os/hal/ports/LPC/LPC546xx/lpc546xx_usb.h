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
 * @file    LPC546xx/lpc546xx_usb.h
 * @brief   LPC546xx answers to what the shared USB driver needs from a family.
 *
 * @details @p lpc546xx_usb_compat.h maps the register *names* the shared
 *          @p LLD/USB driver uses. This file supplies the three things that are
 *          not register names and that the shared driver currently hardcodes
 *          for LPC11Uxx: how the controller is clocked, how its transceiver is
 *          powered, and where its endpoint memory lives.
 *
 *          The seam exists so that the shared driver need not know which family
 *          it is running on. See the patch under @p chibios/patches.
 *
 * @addtogroup USB
 * @{
 */

#ifndef LPC546XX_USB_H
#define LPC546XX_USB_H

#if (HAL_USE_USB == TRUE) || defined(__DOXYGEN__)

/*===========================================================================*/
/* Driver constants.                                                         */
/*===========================================================================*/

/**
 * @brief   Base of the endpoint command/status list, in the USB1 SRAM.
 *
 * @details A fixed address rather than an array, and it has to be. @p USBHSD
 *          supplies the top twelve bits of @p EPLISTSTART itself -- bits 20 to
 *          31 are the register's @p EP_LIST_FIXED field -- so the list is only
 *          addressable inside the 1 MB window at @p 0x40100000, which is where
 *          this part puts its 8 KB of dedicated USB SRAM. An array in main SRAM
 *          cannot be reached by that register at all.
 *
 *          It works out tidier than the full-speed controller would have been:
 *          @p DATABUFSTART here takes a whole 32-bit address rather than only
 *          the high bits, so the shared driver's two writes need no help.
 *
 *          The stock firmware agrees, placing its own endpoint list at
 *          @p 0x40100A00.
 */
#define LPC_USB_SRAM_START          0x40100000U

/**
 * @brief   One past the end of the endpoint memory.
 * @details The shared driver asserts its allocator stays inside this.
 */
#define LPC_USB_SRAM_END            (LPC_USB_SRAM_START + LPC_USB_SRAM_SIZE)

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif
  void lpc_usb_clock_init(void);
  void lpc_usb_power_on(void);
  void lpc_usb_power_off(void);
#ifdef __cplusplus
}
#endif

#endif /* HAL_USE_USB == TRUE */

#endif /* LPC546XX_USB_H */

/** @} */
