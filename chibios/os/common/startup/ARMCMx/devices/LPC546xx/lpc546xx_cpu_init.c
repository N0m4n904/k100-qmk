/*
    ChibiOS - Copyright (C) 2026 k100-qmk contributors
    SPDX-License-Identifier: GPL-3.0-or-later

    This file is part of ChibiOS.
    ChibiOS is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.
    ChibiOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file    LPC546xx/lpc546xx_cpu_init.c
 * @brief   LPC546xx core initialization, run before anything touches RAM.
 *
 * @details The LPC546xx presents its main SRAM as one contiguous 160 KB region
 *          at @p 0x20000000, but that region is four separately clocked banks:
 *          a 32 KB SRAM0 and then SRAM1, SRAM2 and a 64 KB SRAM3. **Only SRAM0
 *          is guaranteed to be clocked when the application starts.** An access
 *          above @p 0x20008000 with the others gated off does not fail loudly;
 *          it is simply lost.
 *
 *          That makes it a startup problem rather than a driver problem.
 *          ChibiOS's @p crt0 fills the stacks, copies @p .data, clears @p .bss
 *          and then zeroes the whole of @p ram0 through @p __init_ram_areas --
 *          all of it before a line of C in the HAL runs. If the upper banks are
 *          not clocked by then, the image has already corrupted itself.
 *
 *          @p __cpu_init is the one hook early enough: @p crt0 calls it
 *          immediately after setting the stack pointers and before every one of
 *          those steps. It is also the right owner. This is a property of the
 *          part, not of a board, so a board cannot forget it -- which three
 *          empty @p __early_init implementations in this tree would otherwise
 *          have let it.
 *
 *          Corsair's own firmware agrees, and independently: the first
 *          instructions of its application image, before it even loads its
 *          stack pointer, are a write of @p 0x38 to @p AHBCLKCTRLSET0 -- SRAM1,
 *          SRAM2 and SRAM3. A program that is handed control by this chip's
 *          bootloader cannot assume the banks are on, and Corsair's does not.
 *
 * @note    Replaces the weak definition in ChibiOS's @p crt1.c. That one
 *          enables the caches on a Cortex-M7 and does nothing otherwise; the
 *          LPC546xx is a Cortex-M4, so there is nothing of it to preserve.
 *
 * @addtogroup LPC546xx_STARTUP
 * @{
 */

#include "fsl_device_registers.h"

/**
 * @brief   Clocks the SRAM banks that are not on at reset.
 *
 * @details @p AHBCLKCTRLSET0 is write-one-to-set, so this enables the three
 *          banks without a read-modify-write and without disturbing anything
 *          else the bootloader left running.
 */
void __cpu_init(void) {

  SYSCON->AHBCLKCTRLSET[0] = SYSCON_AHBCLKCTRL_SRAM1_MASK |
                             SYSCON_AHBCLKCTRL_SRAM2_MASK |
                             SYSCON_AHBCLKCTRL_SRAM3_MASK;
}

/** @} */
