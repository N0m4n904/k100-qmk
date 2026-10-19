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

/**
 * @brief   Power domain ready status, undocumented.
 *
 * @details Not in NXP's device header, and not in their published SDK either.
 *          It is read from Corsair's firmware, whose POWER_SetUsbPhy writes
 *          VD5's power bit and then waits on bit 9 here before returning;
 *          their VD3 routine waits on bit 5 of the same word. Reading it on a
 *          running keyboard gives 0x00000fff with every domain up, and
 *          0x00000fcf with VD3 left off, which is consistent with one bit per
 *          domain.
 */
#define LPC_POWER_READY         (*(volatile uint32_t *)0x40020054U)
#define LPC_POWER_READY_VD5     (1U << 9)

/* About a millisecond at any clock this part runs at, which is far longer than
   a voltage domain takes to come up. */
#define VD5_READY_SPINS         100000U

/* The same, for the host-clock handshake below. */
#define USB1_HOST_CLOCK_SPINS   100000U

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Bring-up trace point, overridable by the application.
 *
 * @details Does nothing, and exists so that an application bringing USB up on a
 *          board with no debug access has somewhere to hang an observation. The
 *          stages are reached in order, so an application that can signal
 *          externally -- by holding a keyboard off the bus for a measurable
 *          time, for instance -- can tell how far this driver's setup got.
 *
 * @param[in] stage     1 for clock and power setup, 2 for the transceiver
 *
 * @notapi
 */
__attribute__((weak)) void lpcUsbTrace(unsigned stage) {

  (void)stage;
}

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

  lpcUsbTrace(1U);

  /* NXP's order, from CLOCK_EnableUsbhs0DeviceClock: the controller's gate goes
     off before its clock source is touched, and back on afterwards, with time
     to settle in between. Enabling the gate across a clock change is what their
     sequence is careful to avoid. */
  SYSCON->AHBCLKCTRLCLR[2] = SYSCON_AHBCLKCTRL_USB1D_MASK;

  /* Read off the firmware that works on this keyboard, at 0x00022940 in
     Heracles_Application_Firmware_v0.35.288: the clock gates go on and the
     transceiver's power comes last, after them. NXP's own sequence agrees --
     POWER_DisablePD(kPDRUNCFG_PD_USB1_PHY) is the final step of
     CLOCK_EnableUsbhs0DeviceClock. An earlier version of this function had it
     the other way round, powering the transceiver and then clocking the
     controller, which is the order nothing else uses. */

  /* VD5 first, and this is the one that matters: it is the *supply* for both
     USB PHYs, and without it the transceiver has no power at all. A device that
     never powers its PHY never pulls up D+, so the host sees an empty port --
     no descriptor read, no error, nothing in the kernel log.

     This is the step NXP performs in POWER_SetUsbPhy(), which ships only inside
     their binary power library and so looked unreachable. It is not: the domain
     is a documented PDRUNCFG bit, described in NXP's own device header as
     "PDEN_VD5 - Power control both USB0 PHY and USB1 PHY". */
  SYSCON->PDRUNCFGCLR[0] = SYSCON_PDRUNCFG_PDEN_VD5_MASK;

  /* And then wait for the domain to say it is up. Corsair's POWER_SetUsbPhy,
     at 0x0002d0f8, does exactly this: it writes the same bit and then spins on
     bit 9 of a word at 0x40020054 until it is set. That address appears
     nowhere in NXP's device header for this part -- it is the power
     management block their binary power library talks to, and the only way to
     learn of it is to read code that uses it.

     Bounded, because spinning for ever on an undocumented register is not a
     thing to do on a keyboard: if it never answers, carry on and let the rest
     of bring-up report the failure instead of hanging here. */
  for (uint32_t spin = 0U; spin < VD5_READY_SPINS; spin++) {
    if ((LPC_POWER_READY & LPC_POWER_READY_VD5) != 0U) {
      break;
    }
  }

  /* NXP's FRO path also sets FROCTRL's USBCLKADJ, which makes the oscillator
     trim itself against USB start-of-frame timing instead of trusting its
     factory trim. It is deliberately not set here.
     
     Setting it was tried on the part and did harm: FREQTRIM fell from 0xaf to
     0x00 within seconds, because the hardware adjusts towards traffic that is
     not arriving and there is nothing to converge on. That is the trim for the
     oscillator the whole system runs from, not just USB. It brought no benefit
     either -- the transceiver behaved exactly as before.
     
     HSPDCLK, the other bit of that pair, is already set at handover on this
     board and in Corsair's firmware, so there is nothing to do for it. */

  usb_rough_delay(50U);

  /* Then the transceiver and its PLL -- and note the register index. These two
     are in PDRUNCFG**1**, while VD5 above is in PDRUNCFG0, and NXP's header
     names every bit SYSCON_PDRUNCFG_PDEN_* without saying which of the two it
     belongs to. The bit numbers collide meaningfully: bits 0 and 1 of
     PDRUNCFG0 are not the USB PHY and PLL, and bit 16 is RAM3 rather than
     anything to do with USB. Writing these to register 0, as an earlier version
     did, powers up two unrelated blocks and leaves the PHY and its PLL switched
     off -- which looks exactly like a working driver whose device never appears
     on the bus.

     The authority is NXP's own power driver:
         kPDRUNCFG_PD_VD5      = MAKE_PD_BITS(PDRCFG0, 28U)
         kPDRUNCFG_PD_USB1_PHY = MAKE_PD_BITS(PDRCFG1,  0U)
         kPDRUNCFG_PD_USB_PLL  = MAKE_PD_BITS(PDRCFG1,  1U)

     This is the third place in this part where a bit name does not say which of
     several identical registers it belongs to; AHBCLKCTRL in hal_pal_lld.c and
     lpc546xx_wwdt.c's neighbours are the others. Treat every
     SYSCON_<REG>_<FIELD> macro as needing an index from somewhere else.

     The endpoint SRAM needs no power bit of its own: NXP's USB enable does not
     touch one, only the USB1RAM clock gate below. */
  /* Gates first, controller then SRAM, as two writes in that order. */
  SYSCON->AHBCLKCTRLSET[2] = SYSCON_AHBCLKCTRL_USB1D_MASK;
  SYSCON->AHBCLKCTRLSET[2] = SYSCON_AHBCLKCTRL_USB1RAM_MASK;

  usb_rough_delay(100U);

  /* The transceiver last, and only the transceiver. Its PLL is a separate bit
     in the same register and belongs to the clock driver, which powers it when
     the configuration selects it. */
  SYSCON->PDRUNCFGCLR[1] = SYSCON_PDRUNCFG_PDEN_USB1_PHY_MASK;

  /*
   * And then a handshake that exists in no document.
   *
   * The firmware that works on this keyboard enables the USB1 *host*
   * controller's clock, spins until USB1CLKSTAT's HOST_NEED_CLKST clears, and
   * switches the host clock off again -- on a device that is never a host.
   * It is at 0x000128ec in Heracles_Application_Firmware_v0.35.288, the first
   * thing their USB bring-up does after powering the transceiver.
   *
   * It is not decoration. Read on a part running this firmware without it,
   * USB1CLKSTAT is 0x3 -- both the device and the host side asking for a clock
   * -- where the working firmware runs at 0x1. Performing the handshake here
   * takes it to 0x0.
   *
   * Bounded, because the loop's exit condition is a bit whose meaning is known
   * only from the code that waits on it.
   */
  SYSCON->AHBCLKCTRLSET[2] = SYSCON_AHBCLKCTRL_USB1H_MASK;
  for (uint32_t spin = 0U; spin < USB1_HOST_CLOCK_SPINS; spin++) {
    if ((SYSCON->USB1CLKSTAT & SYSCON_USB1CLKSTAT_HOST_NEED_CLKST_MASK) == 0U) {
      break;
    }
  }
  SYSCON->AHBCLKCTRLCLR[2] = SYSCON_AHBCLKCTRL_USB1H_MASK;
}

/**
 * @brief   Powers up the transceiver and the endpoint SRAM.
 *
 * @details Separate from clocking because the HAL powers the transceiver only
 *          when the driver is started, so that a configured but stopped USB
 *          does not present itself on the bus.
 *
 * @note    An earlier version of this comment claimed the transceiver was
 *          already live at handover, because the bootloader enumerates at high
 *          speed before starting the application. It is not, and that was
 *          measured rather than argued: stopping this keyboard at its
 *          application's entry point, @p 0x00012568, reads @p DEVCMDSTAT as
 *          @p 0x00400800 with @p VBUS_DEBOUNCED clear, and @p PDRUNCFG1 bit 0
 *          set -- the bootloader hands over with the transceiver powered
 *          *down*. Corsair's application powers it up itself, and so must this
 *          one. @p lpc_usb_clock_init() above does it.
 *
 * @notapi
 */
void lpc_usb_power_on(void) {

  lpcUsbTrace(2U);

  /* Idempotent, and deliberately a repeat of what lpc_usb_clock_init() already
     did. The HAL separates clocking from transceiver power so that a configured
     but stopped USB does not present itself on the bus; on this part the clock
     cannot be brought up without the PHY, so the split does not survive and
     this is what is left of it. */
  SYSCON->PDRUNCFGCLR[0] = SYSCON_PDRUNCFG_PDEN_VD5_MASK;
  SYSCON->PDRUNCFGCLR[1] = SYSCON_PDRUNCFG_PDEN_USB1_PHY_MASK;
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

  lpcUsbTrace(3U);
}

#endif /* HAL_USE_USB == TRUE */

/** @} */
