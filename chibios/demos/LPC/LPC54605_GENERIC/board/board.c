/*
    ChibiOS - Copyright (C) 2026 k100-qmk contributors

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

/**
 * @brief   Early initialization, before the HAL is up.
 *
 * @details Nothing to do. A real board would configure its external memory
 *          controller here, and anything whose pins must be settled before
 *          drivers start; note that IOCON is not clocked yet at this point,
 *          because that happens in palInit().
 */
void __early_init(void) {
}

/**
 * @brief   Late initialization, with the HAL running.
 */
void boardInit(void) {
}
