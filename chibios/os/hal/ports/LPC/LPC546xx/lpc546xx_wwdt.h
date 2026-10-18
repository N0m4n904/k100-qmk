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
 * @file    LPC546xx/lpc546xx_wwdt.h
 * @brief   LPC546xx windowed watchdog, for an application that inherits one.
 *
 * @addtogroup LPC546xx_WWDT
 * @{
 */

#ifndef LPC546XX_WWDT_H
#define LPC546XX_WWDT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
  bool lpcWwdtIsRunning(void);
  void lpcWwdtFeed(void);
  bool lpcWwdtStretch(void);
#ifdef __cplusplus
}
#endif

#endif /* LPC546XX_WWDT_H */

/** @} */
