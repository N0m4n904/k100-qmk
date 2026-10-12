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

#include "hal.h"

void __early_init(void) {
  /* Nothing. IOCON is not clocked yet; palInit() does that and runs after. */
}

void boardInit(void) {

  /* Left dark rather than lit, so that the first thing the application does
     with the LED is visible as a change. */
  palSetLineMode(LINE_LED, PAL_MODE_OUTPUT_PUSHPULL);
#if BOARD_LED_ACTIVE_LOW
  palSetLine(LINE_LED);
#else
  palClearLine(LINE_LED);
#endif

  palSetLineMode(LINE_BUTTON, PAL_MODE_INPUT_PULLUP);
}
