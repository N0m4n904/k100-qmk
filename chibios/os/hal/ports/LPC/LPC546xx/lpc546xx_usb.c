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
 * @file    LPC546xx/lpc546xx_usb.c
 * @brief   LPC546xx USB clocking, power and endpoint memory.
 *
 * @addtogroup USB
 * @{
 */

#include "hal.h"

#if (HAL_USE_USB == TRUE) || defined(__DOXYGEN__)

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

/**
 * @brief   Endpoint command/status list and endpoint buffers.
 *
 * @details Three constraints shape this, and only the first is obvious:
 *
 *          - @p EPLISTSTART ignores the low eight address bits, so the list
 *            must be **256-byte aligned**.
 *          - @p DATABUFSTART supplies the high bits of every endpoint buffer
 *            address while the list entries carry only the low ones, so the
 *            list and all the buffers must sit in **one 4 MB region**. A single
 *            array guarantees it; this part's SRAM is nowhere near that large.
 *          - USB0 is an AHB master reaching memory through the bus matrix, and
 *            **not every RAM is reachable**. Ordinary static data lands in the
 *            SRAM at @p 0x20000000, which is; @p SRAMX at @p 0x00000000 is
 *            tightly coupled to the core and is not. Keeping this as plain
 *            static data rather than placing it with a linker section is what
 *            keeps it on the right side of that, so a board that moves @p .bss
 *            into @p SRAMX must move this back.
 *
 *          LPC11Uxx needs none of this: its USB controller has a dedicated
 *          2 KB SRAM at a fixed address and can reach nothing else, which is
 *          why the shared driver hardcodes an address instead of allocating.
 */
uint8_t lpc_usb_sram[LPC_USB_SRAM_SIZE] __attribute__((aligned(256)));

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Clocks the USB0 device controller.
 *
 * @details Only the controller's own gate. Its 48 MHz reference is routed by
 *          the clock tree in @p hal_lld.c, which takes it from @p fro_hf
 *          directly and so needs no PLL at all -- the reason this function is
 *          three lines where the LPC11Uxx equivalent has to bring up a PLL and
 *          borrow the main PLL's configuration to do it.
 *
 * @notapi
 */
void lpc_usb_clock_init(void) {

  /* USB0D is bit 25 of AHBCLKCTRL1. NXP's macro name does not say which of the
     three registers it belongs to, and the device and host gates for this same
     controller live in different ones. */
  SYSCON->AHBCLKCTRLSET[1] = SYSCON_AHBCLKCTRL_USB0D_MASK;
}

/**
 * @brief   Powers up the full-speed transceiver.
 *
 * @details Separate from clocking because the HAL powers the transceiver only
 *          when the driver is started, so that a configured but stopped USB
 *          does not present itself on the bus.
 *
 * @notapi
 */
void lpc_usb_power_on(void) {

  /* PDRUNCFG bits are power *down* enables, so clearing one powers the block
     up. The dedicated clear register avoids a read-modify-write that could
     drop another block's bit if it changed in between. */
  SYSCON->PDRUNCFGCLR[0] = SYSCON_PDRUNCFG_PDEN_USB0_PHY_MASK;
}

/**
 * @brief   Powers down the full-speed transceiver.
 *
 * @notapi
 */
void lpc_usb_power_off(void) {

  SYSCON->PDRUNCFGSET[0] = SYSCON_PDRUNCFG_PDEN_USB0_PHY_MASK;
}

#endif /* HAL_USE_USB == TRUE */

/** @} */
