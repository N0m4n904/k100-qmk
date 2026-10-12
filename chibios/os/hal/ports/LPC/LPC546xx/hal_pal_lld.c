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
 * @file    LPC546xx/hal_pal_lld.c
 * @brief   LPC546xx PAL subsystem low level driver source.
 *
 * @addtogroup PAL
 * @{
 */

#include "hal.h"

#if (HAL_USE_PAL == TRUE) || defined(__DOXYGEN__)

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/*
 * Clock gates for IOCON and the GPIO ports.
 *
 * NXP's header names these SYSCON_AHBCLKCTRL_GPIOn_MASK without saying which
 * of the three AHBCLKCTRL registers each belongs to, and they are not all in
 * the same one: IOCON and GPIO0 to GPIO3 sit in AHBCLKCTRL0, GPIO4 and GPIO5 in
 * AHBCLKCTRL2. Enabling a bit in the wrong register would leave the port
 * unclocked while turning something unrelated on, so the split is spelled out
 * here. It follows NXP's own clock driver, whose kCLOCK_Gpio4 and kCLOCK_Gpio5
 * are defined against AHB_CLK_CTRL2.
 *
 * Which ports exist comes from the capabilities registry, so a sub-family
 * without one of them does not get a write to a clock gate it has no port for.
 */
#define LPC_PAL_AHBCLKCTRL0                                                 \
  (SYSCON_AHBCLKCTRL_IOCON_MASK |                                           \
   (LPC_HAS_GPIO0 ? SYSCON_AHBCLKCTRL_GPIO0_MASK : 0U) |                    \
   (LPC_HAS_GPIO1 ? SYSCON_AHBCLKCTRL_GPIO1_MASK : 0U) |                    \
   (LPC_HAS_GPIO2 ? SYSCON_AHBCLKCTRL_GPIO2_MASK : 0U) |                    \
   (LPC_HAS_GPIO3 ? SYSCON_AHBCLKCTRL_GPIO3_MASK : 0U))

#define LPC_PAL_AHBCLKCTRL2                                                 \
  ((LPC_HAS_GPIO4 ? SYSCON_AHBCLKCTRL_GPIO4_MASK : 0U) |                    \
   (LPC_HAS_GPIO5 ? SYSCON_AHBCLKCTRL_GPIO5_MASK : 0U))

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
 * @brief   Applies a pad direction.
 *
 * @details Through @p DIRSET and @p DIRCLR rather than a read-modify-write of
 *          @p DIR, so configuring one pad cannot lose a concurrent change to
 *          another pad of the same port. LPC11Uxx has no such registers and its
 *          driver has to read, modify and write.
 *
 * @param[in] port      the port number
 * @param[in] mask      the pads to apply the direction to
 * @param[in] mode      the mode whose direction bit is applied
 */
static void lpc_set_direction(unsigned port, uint32_t mask, iomode_t mode) {

  if ((mode & MODE_DIR_MASK) != 0U) {
    GPIO->DIRSET[port] = mask;
  }
  else {
    GPIO->DIRCLR[port] = mask;
  }
}

/*===========================================================================*/
/* Driver interrupt handlers.                                                */
/*===========================================================================*/

/*===========================================================================*/
/* Driver exported functions.                                               */
/*===========================================================================*/

/**
 * @brief   LPC546xx I/O ports configuration.
 *
 * @details Only clock gating. Pads keep their reset configuration until a board
 *          or a driver asks for a mode, which is the contract the rest of the
 *          HAL expects.
 *
 * @notapi
 */
void _pal_lld_init(void) {

  /* IOCON's registers read back plausible values while unclocked and writes to
     them are simply dropped, which makes a missing clock gate look like a
     wiring fault. Enabling GPIO without IOCON fails the same quiet way. */
  SYSCON->AHBCLKCTRLSET[0] = LPC_PAL_AHBCLKCTRL0;
#if LPC_PAL_AHBCLKCTRL2 != 0U
  SYSCON->AHBCLKCTRLSET[2] = LPC_PAL_AHBCLKCTRL2;
#endif
}

/**
 * @brief   Pads mode setup.
 * @details This function programs a pad with the specified mode.
 *
 * @param[in] port      the port identifier
 * @param[in] pad       the pad id
 * @param[in] mode      the mode
 *
 * @notapi
 */
void _pal_lld_setpadmode(ioportid_t port, iopadid_t pad, iomode_t mode) {
  unsigned p = LPC_IOPORT_NUM(port);

  osalDbgAssert(p < LPC_GPIO_PORTS, "invalid port");
  osalDbgAssert(pad < PAL_IOPORTS_WIDTH, "invalid pad");

  /* The pad first, the direction second: a pad made an output before its pull
     and open-drain configuration is in place drives the pin through whatever
     the previous configuration was. */
  IOCON->PIO[p][pad] = mode & MODE_IOCONF_MASK;
  lpc_set_direction(p, 1U << pad, mode);
}

/**
 * @brief   Group Pads mode setup.
 * @details This function programs a pads group belonging to the same port
 *          with the specified mode.
 *
 * @param[in] port      the port identifier
 * @param[in] mask      the group mask
 * @param[in] mode      the mode
 *
 * @notapi
 */
void _pal_lld_setgroupmode(ioportid_t port,
                           ioportmask_t mask,
                           iomode_t mode) {
  unsigned p = LPC_IOPORT_NUM(port);
  uint32_t iocon = mode & MODE_IOCONF_MASK;

  osalDbgAssert(p < LPC_GPIO_PORTS, "invalid port");

  /* Each pad has its own IOCON register, so the pad configuration cannot be
     done in one write. The direction can, and is. */
  for (unsigned pad = 0U; pad < PAL_IOPORTS_WIDTH; pad++) {
    if ((mask & (1U << pad)) != 0U) {
      IOCON->PIO[p][pad] = iocon;
    }
  }
  lpc_set_direction(p, mask, mode);
}

#endif /* HAL_USE_PAL == TRUE */

/** @} */
