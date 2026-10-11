// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hal.h"

/*
 * QMK overrides both of these with its own weak definitions where a keyboard
 * needs to, which is why they are plain and empty here.
 */

void __early_init(void) {
    // Nothing. Note that IOCON is not clocked yet at this point: palInit()
    // enables its clock, and it runs after this.
}

void boardInit(void) {}
