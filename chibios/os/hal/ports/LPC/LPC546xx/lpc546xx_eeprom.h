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
 * @file    LPC546xx/lpc546xx_eeprom.h
 * @brief   LPC546xx on-chip EEPROM, one word at a time.
 *
 * @addtogroup LPC546xx_EEPROM
 * @{
 */

#ifndef LPC546XX_EEPROM_H
#define LPC546XX_EEPROM_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief   Where the EEPROM array appears in the memory map.
 *
 * @details The controller at @p EEPROM_BASE holds only control and status;
 *          the array itself is a separate window, read as ordinary memory and
 *          written through the controller. NXP's device header names the
 *          controller but not the window, so the address is stated here.
 */
#define LPC_EEPROM_BASE                     0x40108000U

/**
 * @brief   Size of the on-chip EEPROM, in bytes.
 */
#define LPC_EEPROM_SIZE                     (16U * 1024U)

/**
 * @brief   Number of addressable words.
 */
#define LPC_EEPROM_WORDS                    (LPC_EEPROM_SIZE / 4U)

#ifdef __cplusplus
extern "C" {
#endif
  void lpcEepromStart(void);
  uint32_t lpcEepromRead(uint32_t word);
  bool lpcEepromWrite(uint32_t word, uint32_t value);
#ifdef __cplusplus
}
#endif

#endif /* LPC546XX_EEPROM_H */

/** @} */
