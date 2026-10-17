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
 * @file    LPC546xx/lpc546xx_rit.h
 * @brief   LPC546xx repetitive interrupt timer, used as the realtime counter.
 *
 * @addtogroup LPC546xx_RIT
 * @{
 */

#ifndef LPC546XX_RIT_H
#define LPC546XX_RIT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif
  bool lpcRitInit(void);
  bool lpcRitIsRunning(void);
#ifdef __cplusplus
}
#endif

#endif /* LPC546XX_RIT_H */

/** @} */
