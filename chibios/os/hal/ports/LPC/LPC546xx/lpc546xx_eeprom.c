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
 * @file    LPC546xx/lpc546xx_eeprom.c
 * @brief   LPC546xx on-chip EEPROM, one word at a time.
 *
 * @details Deliberately the smallest thing that works. The array is read as
 *          ordinary memory, and a write is one word programmed by the hardware
 *          itself: @p AUTOPROG set to 1 makes a store to the array start and
 *          complete a program cycle, with no page buffer and no erase step.
 *          That is what keeps this safe to call on a part whose EEPROM already
 *          holds somebody else's data -- nothing outside the addressed word is
 *          read, rewritten or erased.
 *
 *          The one figure that cannot be chosen freely is the clock. The EEPROM
 *          requires 1.5 MHz, and @p CLKDIV divides the main clock down to it;
 *          programming it wrong does not fail cleanly, it writes badly. The
 *          divider, the rounding and the clock gate here are all taken from
 *          what the part's own stock firmware does, cross-checked against the
 *          register definitions in NXP's device header:
 *
 *          @code
 *          AHBCLKCTRLSET0 = 1 << 9          // SYSCON_AHBCLKCTRL_EEPROM_MASK
 *          CLKDIV         = DIV_ROUND(main_clock, 1500000) - 1
 *          @endcode
 *
 * @addtogroup LPC546xx_EEPROM
 * @{
 */

#include "hal.h"

#include "lpc546xx_eeprom.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/**
 * @brief   Clock the EEPROM cell needs, in Hz.
 */
#define EEPROM_CLOCK                        1500000U

/**
 * @brief   Program one word on a store to the array, rather than a page.
 *
 * @details @p AUTOPROG is a two-bit field: 0 programs nothing by itself, 1
 *          programs the word just written, 2 programs a whole page. 1 is the
 *          only value that leaves neighbouring words alone.
 */
#define EEPROM_AUTOPROG_WORD                1U

/**
 * @brief   Upper bound on the completion poll, in iterations.
 *
 * @details A safety bound, not a timing specification. A word program takes a
 *          few milliseconds; this is several times longer than that at any clock
 *          this part runs, so it cannot trip on a write that is merely slow. It
 *          exists only so that a completion flag which never arrives ends the
 *          loop instead of the boot.
 */
#define EEPROM_PROG_SPINS                   1000000U

/**
 * @brief   The array itself.
 */
#define EEPROM_ARRAY                                                        \
  ((volatile uint32_t *)LPC_EEPROM_BASE)

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Clocks the EEPROM and sets its divider.
 *
 * @details Idempotent, and cheap enough to call before every write rather than
 *          relying on an init order. @p AHBCLKCTRLSET0 is write-one-to-set, so
 *          this disturbs no other clock.
 */
void lpcEepromStart(void) {

  SYSCON->AHBCLKCTRLSET[0] = SYSCON_AHBCLKCTRL_EEPROM_MASK;

  /* Round to nearest, as the stock firmware does: it compares the remainder
     against half the target before incrementing. A divider one step too large
     underclocks the cell, which is the direction that corrupts a write.*/
  uint32_t divider = (LPC_SYS_FREQUENCY + (EEPROM_CLOCK / 2U)) / EEPROM_CLOCK;
  if (divider == 0U) {
    divider = 1U;
  }
  EEPROM->CLKDIV = divider - 1U;
}

/**
 * @brief   Reads one word.
 *
 * @param[in] word      index into the array, not a byte offset
 * @return              the word, or zero if @p word is out of range
 */
uint32_t lpcEepromRead(uint32_t word) {

  if (word >= LPC_EEPROM_WORDS) {
    return 0U;
  }
  return EEPROM_ARRAY[word];
}

/**
 * @brief   Programs one word and waits for it to complete.
 *
 * @details Polls rather than taking an interrupt: the callers that matter run
 *          either before the kernel is up or on the way to a reset, where there
 *          is nothing left to yield to.
 *
 *          The poll is **bounded**, which is the whole reason this returns a
 *          value. The stock firmware's equivalent spins forever on the same
 *          flag, and it can afford to -- it is one of many things that driver
 *          does. Here it is the recovery path: a caller programs the operating
 *          mode word and resets into the bootloader, and that caller wants to
 *          know when the write did not happen so it can avoid resetting into a
 *          loop. A spin that never ends reports nothing and simply hangs,
 *          throwing away the answer.
 *
 * @param[in] word      index into the array, not a byte offset
 * @param[in] value     the word to program
 * @return              @p true once the hardware reports the program complete;
 *                      @p false if @p word is out of range or the completion
 *                      flag never arrived
 */
bool lpcEepromWrite(uint32_t word, uint32_t value) {

  if (word >= LPC_EEPROM_WORDS) {
    return false;
  }

  lpcEepromStart();

  EEPROM->AUTOPROG = EEPROM_AUTOPROG_WORD;
  EEPROM->INTSTATCLR = EEPROM_INTSTATCLR_PROG_CLR_ST_MASK;

  EEPROM_ARRAY[word] = value;

  for (uint32_t spins = EEPROM_PROG_SPINS; spins > 0U; spins--) {
    if ((EEPROM->INTSTAT & EEPROM_INTSTAT_END_OF_PROG_MASK) != 0U) {
      return true;
    }
  }

  return false;
}

/** @} */
