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
 * @file    LPC546xx/lpc546xx_wwdt.c
 * @brief   LPC546xx windowed watchdog, for an application that inherits one.
 *
 * @details Nothing here starts a watchdog. This exists for the opposite
 *          situation: an application handed control by a bootloader that has
 *          already started one, which must then keep it fed or be reset.
 *
 *          It cannot be handed back. NXP's description of @p WDEN is explicit --
 *          "once this bit is set to one and a watchdog feed is performed, the
 *          watchdog timer will run permanently" -- so an application in this
 *          position has no option but to feed it, for as long as it runs.
 *
 *          Discovering this the hard way is worth recording, because the failure
 *          is so misleading. Firmware that does not feed an inherited watchdog
 *          starts, runs for a second or two, and is reset. From the host that
 *          looks exactly like a bootloader refusing to start the image: the
 *          keyboard reappears almost immediately, still in bootloader mode,
 *          having apparently never run. A marker delay placed early in boot
 *          reports the same thing, because the reset cuts the marker short too.
 *          The only way to tell the two apart is to feed the watchdog *while*
 *          marking time, at which point the marker completes and the firmware is
 *          revealed to have been running the whole while.
 *
 * @addtogroup LPC546xx_WWDT
 * @{
 */

#include "hal.h"

#include "lpc546xx_wwdt.h"

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/**
 * @name    The feed sequence
 * @details Two writes, in this order and with nothing between them, reload the
 *          timer from @p TC. Any other value, or a single write, is ignored.
 * @{
 */
#define WWDT_FEED_FIRST                     0xAAU
#define WWDT_FEED_SECOND                    0x55U

/**
 * @brief   A value that is not part of the sequence.
 * @details Writing it is not a no-op. The part treats a feed it cannot
 *          complete as a watchdog event in its own right, which is what
 *          @p lpcWwdtForceReset() is built on. Zero rather than any other
 *          wrong value because it says plainly that no sequence was meant.
 */
#define WWDT_FEED_INVALID                   0x00U
/** @} */

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Is a watchdog running that this application has to service?
 *
 * @return              @p true when @p WDEN is set.
 *
 * @notapi
 */
bool lpcWwdtIsRunning(void) {

  return (WWDT->MOD & WWDT_MOD_WDEN_MASK) != 0U;
}

/**
 * @brief   Reloads the watchdog, if one is running.
 *
 * @details Guarded on @p WDEN rather than fed unconditionally, and that
 *          distinction matters in the one direction nobody wants: on this part
 *          it is the *feed* that commits @p WDEN and @p WDRESET, so feeding a
 *          watchdog that is merely configured and not yet running is how you
 *          start one. A caller that does not know whether it inherited a
 *          watchdog is exactly the caller who must not do that.
 *
 * @notapi
 */
void lpcWwdtFeed(void) {

  if (!lpcWwdtIsRunning()) {
    return;
  }

  WWDT->FEED = WWDT_FEED_FIRST;
  WWDT->FEED = WWDT_FEED_SECOND;
}

/**
 * @brief   Resets the part at once, through the watchdog.
 *
 * @details An invalid feed is a watchdog event in its own right, so the part
 *          resets immediately rather than at the end of a timeout. That makes
 *          this the cheapest reset there is: it needs nothing initialised and
 *          no peripheral beyond the one already running.
 *
 *          It is not interchangeable with @p NVIC_SystemReset(), and the
 *          difference is the reason to have it. A reset from here sets
 *          @p WDTOF, which survives the reset, so a boot ROM or a bootloader
 *          that reads that flag can tell this reset from an ordinary one and
 *          act on it. On this keyboard that is the whole mechanism by which an
 *          application hands control back: the bootloader keeps the keyboard
 *          when it finds @p WDTOF set, and starts the application when it does
 *          not.
 *
 *          @p WDRESET is checked as well as @p WDEN, because with it clear the
 *          same write raises an interrupt instead of resetting, and a caller
 *          relying on not returning would then carry on.
 *
 * @return              @p false when there is no running watchdog configured
 *                      to reset, in which case nothing was written. It does
 *                      not return otherwise.
 *
 * @notapi
 */
bool lpcWwdtForceReset(void) {

  if (!lpcWwdtIsRunning()) {
    return false;
  }

  if ((WWDT->MOD & WWDT_MOD_WDRESET_MASK) == 0U) {
    return false;
  }

  WWDT->FEED = WWDT_FEED_INVALID;
  WWDT->FEED = WWDT_FEED_INVALID;

  while (true) {
    /* The reset is immediate; this only says so to the compiler. */
  }
}

/**
 * @brief   Lengthens an inherited watchdog to its maximum timeout.
 *
 * @details Separate from feeding, and far more restricted. @p WDEN cannot be
 *          cleared once set, but the timeout constant behind it is ordinary
 *          state, and NXP gates writes to it with @p WDPROTECT: clear, and
 *          @p TC takes any value; set, and it may only be raised inside the
 *          feed window. This refuses rather than guesses when protection is on.
 *
 *          The reason to want it is diagnosis. An application that inherits a
 *          watchdog it did not configure cannot tell a hang from a stretch of
 *          work that simply takes longer than the previous owner allowed for --
 *          both end as a reset in the same place, and both look from outside
 *          like firmware that will not start. Raising the timeout separates
 *          them: what was only slow runs to completion, and what was hung still
 *          hangs.
 *
 * @return              Whether the timeout was raised.
 *
 * @notapi
 */
bool lpcWwdtStretch(void) {

  if (!lpcWwdtIsRunning()) {
    return false;
  }

  if ((WWDT->MOD & WWDT_MOD_WDPROTECT_MASK) != 0U) {
    return false;
  }

  WWDT->TC = WWDT_TC_COUNT_MASK;

  /* The counter reloads from TC on a feed, not on the write above, so without
     this the new timeout does not take effect until something else feeds. */
  lpcWwdtFeed();

  return true;
}

/** @} */
