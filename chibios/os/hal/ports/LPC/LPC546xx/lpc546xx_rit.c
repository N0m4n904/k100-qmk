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
 * @file    LPC546xx/lpc546xx_rit.c
 * @brief   LPC546xx repetitive interrupt timer, used as the realtime counter.
 *
 * @details This driver exists to stand in for the DWT cycle counter, on a part
 *          where that counter cannot be used.
 *
 *          ChibiOS's ARMv7-M port implements its realtime counter as
 *          @p DWT->CYCCNT, and that is a reasonable choice: it is in the core,
 *          it needs no clock gate and it counts core cycles exactly. It is also
 *          in the debug block, and a part shipped with debug access disabled --
 *          as this keyboard's is -- faults on the write that starts it and reads
 *          a constant if it is never started.
 *
 *          A constant realtime counter is worse than an absent one, because the
 *          one thing built on it is a delay. @p chSysPolledDelayX() samples the
 *          counter, computes an end value and spins until the counter passes it,
 *          so a counter that never advances does not delay, it hangs. Everything
 *          QMK times with @p wait_us() goes through that function on this
 *          platform, including the settling delay in the matrix scan, which runs
 *          before USB is started. The resulting failure is thoroughly
 *          misdirecting: the firmware hangs in the first matrix read, the
 *          inherited watchdog resets the part a second later, and from the host
 *          it looks as though USB initialisation is what crashed.
 *
 *          The RIT replaces it. It is a free-running up-counter on the AHB, so
 *          with @p AHBCLKDIV at one it counts at @p LPC_SYSCLK -- the same rate
 *          @p CYCCNT would, which is what QMK's @p REALTIME_COUNTER_CLOCK
 *          assumes. Only the low 32 bits are read, which wrap exactly as
 *          @p CYCCNT does and as @p rtcnt_t expects.
 *
 *          Reading one rather than the other is a one-line change in the kernel
 *          port, behind @p PORT_USE_EXTERNAL_RT_COUNTER; see
 *          chibios/patches/0007-external-realtime-counter.patch.
 *
 * @addtogroup LPC546xx_RIT
 * @{
 */

#include "hal.h"

#include "lpc546xx_rit.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/**
 * @brief   Clock gate for the RIT.
 * @details @p AHBCLKCTRL1 bit 1. NXP's header names the bit
 *          @p SYSCON_AHBCLKCTRL_RIT_MASK without saying which of the three
 *          @p AHBCLKCTRL registers it belongs to, and bit 1 of @p AHBCLKCTRL0 is
 *          @p ROM -- so the obvious reading of that macro enables the wrong
 *          block and leaves this one dark. The register index comes from NXP's
 *          clock driver, which states it:
 *          @p kCLOCK_Rit @p = @p CLK_GATE_DEFINE(AHB_CLK_CTRL1, @p 1).
 *
 *          There is no reset to release. NXP's reset driver has entries for the
 *          timers either side of this one, @p kMRT_RST_SHIFT_RSTn and
 *          @p kUTICK_RST_SHIFT_RSTn, and none for the RIT.
 */
#define LPC_RIT_AHBCLKCTRL_INDEX            1U

/**
 * @brief   How long to wait for the counter to prove it is running.
 * @details Only has to outlast a couple of AHB cycles; this is generous by
 *          orders of magnitude so that it does not become a frequency
 *          dependency of its own.
 */
#define LPC_RIT_ADVANCE_SPINS               1000U

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Starts the RIT as a free-running counter.
 *
 * @details Must run after the clock tree reaches its final frequency, because
 *          the rate this counter ticks at is the rate its callers are told it
 *          ticks at.
 *
 * @return              Whether the counter was observed to advance.
 * @retval false        The counter is dead. Nothing can be timed, and a caller
 *                      that delays will not return.
 *
 * @notapi
 */
bool lpcRitInit(void) {

  SYSCON->AHBCLKCTRLSET[LPC_RIT_AHBCLKCTRL_INDEX] =
    SYSCON_AHBCLKCTRL_RIT_MASK;

  /* Configured while disabled, because COUNTER is writable only then.

     Free-running means: do not clear on a compare match (RITENCLR), and do not
     stop when a debugger halts the core (RITENBR) -- the latter being moot on a
     part no debugger can halt, which is the reason this driver exists. The
     compare value is pushed to the top of the 48-bit range and left unmasked;
     no RIT interrupt is routed to the NVIC, so the match flag it eventually
     sets has nowhere to go. */
  RIT->CTRL     = 0U;
  RIT->MASK     = 0U;
  RIT->MASK_H   = 0U;
  RIT->COMPVAL  = 0xFFFFFFFFU;
  RIT->COMPVAL_H = 0xFFFFFFFFU;
  RIT->COUNTER  = 0U;
  RIT->COUNTER_H = 0U;

  RIT->CTRL = RIT_CTRL_RITEN_MASK;

  return lpcRitIsRunning();
}

/**
 * @brief   Is the counter advancing?
 *
 * @details Worth asking separately from @p lpcRitInit(), because the failure
 *          this detects is silent in every other respect: a wrong clock gate
 *          leaves the registers readable and the counter at zero.
 *
 * @return              Whether the counter changed within
 *                      @p LPC_RIT_ADVANCE_SPINS reads.
 *
 * @notapi
 */
bool lpcRitIsRunning(void) {
  uint32_t start = RIT->COUNTER;

  for (uint32_t spins = LPC_RIT_ADVANCE_SPINS; spins > 0U; spins--) {
    if (RIT->COUNTER != start) {
      return true;
    }
  }

  return false;
}

/**
 * @brief   The realtime counter, as the kernel port asks for it.
 *
 * @details Declared by the ARMv7-M port when @p PORT_USE_EXTERNAL_RT_COUNTER is
 *          defined, and reached through @p chSysGetRealtimeCounterX().
 *
 * @return              The low 32 bits of the RIT counter.
 *
 * @notapi
 */
rtcnt_t port_rt_external_counter_value(void) {

  return (rtcnt_t)RIT->COUNTER;
}

/** @} */
