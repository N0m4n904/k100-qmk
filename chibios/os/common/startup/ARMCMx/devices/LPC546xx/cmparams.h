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
 * @file        LPC546xx/cmparams.h
 * @brief       ARM Cortex-M4 parameters for the NXP LPC546xx family.
 *
 * @defgroup    ARMCMx_LPC546xx NXP Semi. LPC546xx.
 * @ingroup     ARMCMx_SPECIFIC
 * @details     This file contains the Cortex-M4 specific parameters for the
 *              LPC546xx platform.
 * @{
 */

#ifndef _CMPARAMS_H_
#define _CMPARAMS_H_

/**
 * @brief   Cortex core model.
 */
#define CORTEX_MODEL            4

/**
 * @brief   Systick unit presence.
 */
#define CORTEX_HAS_ST           TRUE

/**
 * @brief   Memory Protection unit presence.
 */
#define CORTEX_HAS_MPU          1

/**
 * @brief   Floating Point unit presence.
 * @note    Single precision only. The LPC11Uxx sibling has none, which is why
 *          this family cannot share its startup files.
 */
#define CORTEX_HAS_FPU          1

/**
 * @brief   Number of bits in priority masks.
 */
#define CORTEX_PRIORITY_BITS    3

/**
 * @brief   Number of interrupt vectors.
 * @note    This number does not include the 16 system vectors and must be
 *          rounded to a multiple of 8. The highest @p IRQn_Type value NXP
 *          defines for this family is 56, so 64 is the smallest legal choice
 *          that covers the table.
 */
#define CORTEX_NUM_VECTORS      64

/**
 * @brief   Vector table checksum.
 * @details The boot ROM refuses to start an image whose first eight vectors do
 *          not sum to zero, so the startup code has to place the complement at
 *          offset @p 0x1C. Shared with LPC11Uxx, which needs the same.
 */
#define LPC_VECTOR_CHECKSUM     TRUE

/* The following code is not processed when the file is included from an
   asm module.*/
#if !defined(_FROM_ASM_)
/* Including the device CMSIS header. Note, we are not using the definitions
   from this header because we need this file to be usable also from
   assembler source files. We verify that the info matches instead.*/
#include "fsl_device_registers.h"

#if CORTEX_MODEL != __CORTEX_M
#error "CMSIS __CORTEX_M mismatch"
#endif

#if CORTEX_PRIORITY_BITS != __NVIC_PRIO_BITS
#error "CMSIS __NVIC_PRIO_BITS mismatch"
#endif

#if CORTEX_HAS_FPU != __FPU_PRESENT
#error "CMSIS __FPU_PRESENT mismatch"
#endif

#if CORTEX_HAS_MPU != __MPU_PRESENT
#error "CMSIS __MPU_PRESENT mismatch"
#endif

#endif /* !defined(_FROM_ASM_) */

#endif /* _CMPARAMS_H_ */

/** @} */
