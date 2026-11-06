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
 * @file    LPC546xx/lpc546xx_crash.c
 * @brief   Where the core was when it faulted or the watchdog was about to fire.
 *
 * @details An application running under a watchdog that a bootloader set dies
 *          without a word: a loop that stops feeding it is reset, a fault spins in
 *          the unhandled exception stub until the same happens, and whatever RAM
 *          recorded about it is gone by the next power cycle. This has handlers
 *          for the four fault vectors and the watchdog's warning, passes what was
 *          interrupted to @p lpcCrashReport(), and then resets the part through
 *          the watchdog, so that a bootloader reading @p WDTOF sees what it would
 *          have seen anyway.
 *
 *          **Nothing reached from here may call the kernel.** The warning runs
 *          above the kernel's interrupt mask, because the loop it has to catch may
 *          be stuck with the kernel locked. tools/check-firmware-image.py walks
 *          the calls and fails an image where one does.
 *
 *          A loop stuck with @p PRIMASK set never reaches the warning, and it is
 *          reset with nothing recorded. No maskable interrupt can do better.
 *
 * @addtogroup LPC546xx_CRASH
 * @{
 */

#include "hal.h"

#include "lpc546xx_crash.h"
#include "lpc546xx_wwdt.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/**
 * @name    Words of an exception frame
 * @details The basic frame of ARMv7-M, which starts the same with or without the
 *          floating point state behind it.
 * @{
 */
#define FRAME_LR                            5U
#define FRAME_PC                            6U
#define FRAME_PSR                           7U
#define FRAME_WORDS                         8U
/** @} */

_Static_assert(LPC_CRASH_STALL == 16 + LPC_WWDT_IRQ_NUMBER,
               "LPC_CRASH_STALL does not match the vector number of the watchdog interrupt");

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/**
 * @brief   Whether a stack pointer can be followed without faulting again.
 */
static bool frame_is_in_ram(const uint32_t *frame) {
  extern uint32_t __ram0_base__[], __ram0_end__[];

  return frame >= __ram0_base__ && frame + FRAME_WORDS <= __ram0_end__;
}

/**
 * @brief   The cause an exception number stands for.
 *
 * @return              @p false for a number that is no cause, which no vector
 *                      pointing here produces.
 */
static bool cause_from_exception(uint32_t exception, lpc_crash_cause_t *cause) {

  switch (exception) {
    case LPC_CRASH_HARD:
    case LPC_CRASH_MEM:
    case LPC_CRASH_BUS:
    case LPC_CRASH_USAGE:
    case LPC_CRASH_STALL:
      *cause = (lpc_crash_cause_t)exception;
      return true;
    default:
      return false;
  }
}

/**
 * @brief   Reports the crash and resets. Returns only for a watchdog warning that
 *          is not yet due.
 *
 * @details Fed once, because a warning comes milliseconds before the timeout and
 *          each word the report programs takes milliseconds too. The reset does
 *          not wait for the timeout this buys, which can be long.
 *
 *          @p NVIC_SystemReset() is for a watchdog that is not set to reset, because
 *          otherwise @p lpcWwdtForceReset() returns.
 *
 * @param[in] frame     the frame the exception stacked
 */
__attribute__((used))
void lpc_crash_capture(const uint32_t *frame) {
  lpc_crash_cause_t cause;

  if (!cause_from_exception(__get_IPSR() & IPSR_ISR_Msk, &cause)) {
    return;
  }

  if (cause == LPC_CRASH_STALL && !lpcWwdtWarningIsDue()) {
    lpcWwdtClearWarning();
    return;
  }

  lpcWwdtFeed();

  const bool readable = frame_is_in_ram(frame);
  const uint32_t cfsr = SCB->CFSR;
  uint32_t fault_address = 0U;

  if ((cfsr & SCB_CFSR_BFARVALID_Msk) != 0U) {
    fault_address = SCB->BFAR;
  }
  else if ((cfsr & SCB_CFSR_MMARVALID_Msk) != 0U) {
    fault_address = SCB->MMFAR;
  }

  const lpc_crash_t crash = {
    .cause = cause,
    .pc = readable ? frame[FRAME_PC] : 0U,
    .lr = readable ? frame[FRAME_LR] : 0U,
    .psr = readable ? frame[FRAME_PSR] : 0U,
    .basepri = __get_BASEPRI(),
    .cfsr = cfsr,
    .fault_address = fault_address,
  };

  lpcCrashReport(&crash);

  (void)lpcWwdtForceReset();
  NVIC_SystemReset();
}

/*===========================================================================*/
/* Driver interrupt handlers.                                                */
/*===========================================================================*/

/**
 * @brief   Entry point for every vector this file defines.
 *
 * @details Naked and a tail call, so that @p lr still contains @p EXC_RETURN for
 *          the one path that returns. Bit two of it selects the stack the exception
 *          pushed its frame onto.
 */
__attribute__((naked))
void lpcCrashEntry(void) {

  __asm volatile("tst   lr, #4                 \n"
                 "ite   eq                     \n"
                 "mrseq r0, msp                \n"
                 "mrsne r0, psp                \n"
                 "b     lpc_crash_capture      \n");
}

void HardFault_Handler(void) __attribute__((naked, alias("lpcCrashEntry")));
void MemManage_Handler(void) __attribute__((naked, alias("lpcCrashEntry")));
void BusFault_Handler(void) __attribute__((naked, alias("lpcCrashEntry")));
void UsageFault_Handler(void) __attribute__((naked, alias("lpcCrashEntry")));
void LPC_WWDT_IRQ_VECTOR(void) __attribute__((naked, alias("lpcCrashEntry")));

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Starts watching for the watchdog's warning. Faults need no starting.
 *
 * @notapi
 */
void lpcCrashCaptureStart(void) {

  lpcWwdtEnableWarning(CORTEX_MAXIMUM_PRIORITY);
}

/**
 * @brief   Called with what was interrupted, by an application that needs the report.
 *
 * @details Runs in the exception, above the kernel's interrupt mask for a warning
 *          and above everything for a fault, so it may not call the kernel, and it
 *          has to return for the part to reset. The core may be in any state: a
 *          fault here locks the core up and the watchdog resets it.
 *
 * @param[in] crash     what was interrupted
 */
__attribute__((weak))
void lpcCrashReport(const lpc_crash_t *crash) {

  (void)crash;
}

/** @} */
