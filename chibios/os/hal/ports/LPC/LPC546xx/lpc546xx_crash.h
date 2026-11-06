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
 * @file    LPC546xx/lpc546xx_crash.h
 * @brief   Where the core was when it faulted or the watchdog was about to fire.
 *
 * @addtogroup LPC546xx_CRASH
 * @{
 */

#ifndef LPC546XX_CRASH_H
#define LPC546XX_CRASH_H

#include <stdint.h>

/**
 * @brief   Why the core stopped, as the number of the exception it took.
 */
typedef enum {
  LPC_CRASH_HARD_FAULT = 3,
  LPC_CRASH_MEM_MANAGE = 4,
  LPC_CRASH_BUS_FAULT = 5,
  LPC_CRASH_USAGE_FAULT = 6,
  LPC_CRASH_WATCHDOG_WARNING = 16
} lpc_crash_cause_t;

/**
 * @brief   What the interrupted code was doing.
 *
 * @details @p pc, @p lr and @p psr are zero when the stack pointer of the
 *          interrupted code does not lead to RAM, which a stack overflow can do.
 *          @p fault_address is zero unless @p cfsr says it holds one.
 */
typedef struct {
  lpc_crash_cause_t cause;
  uint32_t pc;
  uint32_t lr;
  uint32_t psr;
  uint32_t basepri;
  uint32_t cfsr;
  uint32_t fault_address;
} lpc_crash_t;

#ifdef __cplusplus
extern "C" {
#endif
  void lpcCrashCaptureStart(void);
  void lpcCrashReport(const lpc_crash_t *crash);
#ifdef __cplusplus
}
#endif

#endif /* LPC546XX_CRASH_H */

/** @} */
