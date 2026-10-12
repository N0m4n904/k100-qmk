/*
 * Copyright (c) 2016, Freescale Semiconductor, Inc.
 * Copyright 2016-2019, NXP
 * Copyright 2026 k100-qmk contributors
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file    LPC546xx/lpc546xx_pll.c
 * @brief   LPC546xx system PLL bring-up.
 *
 * @details This file is BSD-3-Clause rather than Apache-2.0 like the rest of
 *          the port, because the divider encoders and the loop filter selection
 *          below are NXP's, from @p fsl_clock.c in the MCUXpresso SDK. They are
 *          not an algorithm anyone would arrive at independently: @p NDEC,
 *          @p PDEC and @p MDEC do not hold the divider values but a linear
 *          feedback shift register sequence that reaches them, so the encoding
 *          has to match the hardware's sequence exactly. Writing it from
 *          scratch would mean reproducing NXP's constants anyway, so it is
 *          taken with its licence attached.
 *
 * @addtogroup HAL
 * @{
 */

#include "hal.h"

#if LPC_MAINCLKSELB == LPC_MAINCLKSELB_PLL

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/** @brief Largest divider each sequence covers, from NXP's driver. */
#define NVALMAX 0x100U
#define PVALMAX 0x20U
#define MVALMAX 0x8000U

/** @brief Field widths of the encoded dividers. */
#define NDEC_MASK 0x3FFU
#define PDEC_MASK 0x7FU
#define MDEC_MASK 0x1FFFFU

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/**
 * @brief   Encodes the pre-divider N into its @p NDEC sequence value.
 */
static uint32_t pll_encode_n(uint32_t n) {
  uint32_t x, i;

  switch (n) {
  case 0U:
    x = 0x3FFU;
    break;
  case 1U:
    x = 0x302U;
    break;
  case 2U:
    x = 0x202U;
    break;
  default:
    x = 0x080U;
    for (i = n; i <= NVALMAX; i++) {
      x = (((x ^ (x >> 2U) ^ (x >> 3U) ^ (x >> 4U)) & 1U) << 7U) |
          ((x >> 1U) & 0x7FU);
    }
    break;
  }

  return x & NDEC_MASK;
}

/**
 * @brief   Encodes the post-divider P into its @p PDEC sequence value.
 */
static uint32_t pll_encode_p(uint32_t p) {
  uint32_t x, i;

  switch (p) {
  case 0U:
    x = 0x7FU;
    break;
  case 1U:
    x = 0x62U;
    break;
  case 2U:
    x = 0x42U;
    break;
  default:
    x = 0x10U;
    for (i = p; i <= PVALMAX; i++) {
      x = (((x ^ (x >> 2U)) & 1U) << 4U) | ((x >> 1U) & 0xFU);
    }
    break;
  }

  return x & PDEC_MASK;
}

/**
 * @brief   Encodes the multiplier M into its @p MDEC sequence value.
 */
static uint32_t pll_encode_m(uint32_t m) {
  uint32_t x, i;

  switch (m) {
  case 0U:
    x = 0x1FFFFU;
    break;
  case 1U:
    x = 0x18003U;
    break;
  case 2U:
    x = 0x10003U;
    break;
  default:
    x = 0x04000U;
    for (i = m; i <= MVALMAX; i++) {
      x = (((x ^ (x >> 1U)) & 1U) << 14U) | ((x >> 1U) & 0x3FFFU);
    }
    break;
  }

  return x & MDEC_MASK;
}

/**
 * @brief   Loop filter coefficients for a given multiplier.
 *
 * @details Bandwidth settings, which NXP derives from the multiplier rather
 *          than exposing. @p SELR is always zero on this part.
 */
static void pll_find_sel(uint32_t m, uint32_t *selp, uint32_t *seli,
                         uint32_t *selr) {

  *selp = (m < 60U) ? ((m >> 1U) + 1U) : (PVALMAX - 1U);

  if (m > 16384U) {
    *seli = 1U;
  }
  else if (m > 8192U) {
    *seli = 2U;
  }
  else if (m > 2048U) {
    *seli = 4U;
  }
  else if (m >= 501U) {
    *seli = 8U;
  }
  else if (m >= 60U) {
    *seli = 4U * (1024U / (m + 9U));
  }
  else {
    *seli = (m & 0x3CU) + 4U;
  }

  if (*seli > 0x3FU) {
    *seli = 0x3FU;
  }

  *selr = 0U;
}

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Brings up the system PLL and waits for it to lock.
 *
 * @details Leaves the main clock alone: the caller selects the PLL once it is
 *          locked, because switching to an unlocked PLL stops the core.
 *
 * @notapi
 */
void lpc_syspll_init(void) {
  uint32_t selp, seli, selr;

  /* Source first, while the PLL is powered down and cannot react to it. */
  SYSCON->SYSPLLCLKSEL = SYSCON_SYSPLLCLKSEL_SEL(LPC_SYSPLLCLKSEL);

  /* Powered down while being configured, which is what the dividers' sequence
     values are latched out of. */
  SYSCON->PDRUNCFGSET[0] = SYSCON_PDRUNCFG_PDEN_SYS_PLL_MASK;

  pll_find_sel(LPC_SYSPLL_M, &selp, &seli, &selr);

  SYSCON->SYSPLLCTRL = SYSCON_SYSPLLCTRL_SELR(selr) |
                       SYSCON_SYSPLLCTRL_SELI(seli) |
                       SYSCON_SYSPLLCTRL_SELP(selp);

  /* The REQ bits latch the sequence value; the dividers ignore a write
     without them. */
  SYSCON->SYSPLLNDEC = SYSCON_SYSPLLNDEC_NDEC(pll_encode_n(LPC_SYSPLL_N));
  SYSCON->SYSPLLNDEC = SYSCON_SYSPLLNDEC_NDEC(pll_encode_n(LPC_SYSPLL_N)) |
                       SYSCON_SYSPLLNDEC_NREQ_MASK;

  SYSCON->SYSPLLPDEC = SYSCON_SYSPLLPDEC_PDEC(pll_encode_p(LPC_SYSPLL_P));
  SYSCON->SYSPLLPDEC = SYSCON_SYSPLLPDEC_PDEC(pll_encode_p(LPC_SYSPLL_P)) |
                       SYSCON_SYSPLLPDEC_PREQ_MASK;

  SYSCON->SYSPLLMDEC = SYSCON_SYSPLLMDEC_MDEC(pll_encode_m(LPC_SYSPLL_M));
  SYSCON->SYSPLLMDEC = SYSCON_SYSPLLMDEC_MDEC(pll_encode_m(LPC_SYSPLL_M)) |
                       SYSCON_SYSPLLMDEC_MREQ_MASK;

  SYSCON->PDRUNCFGCLR[0] = SYSCON_PDRUNCFG_PDEN_SYS_PLL_MASK;

  /* Switching the core onto an unlocked PLL stops the core, so this wait is
     not optional and has no timeout worth having: there is nothing to fall
     back to this early. */
  while ((SYSCON->SYSPLLSTAT & SYSCON_SYSPLLSTAT_LOCK_MASK) == 0U) {
  }
}

#endif /* LPC_MAINCLKSELB == LPC_MAINCLKSELB_PLL */

/** @} */
