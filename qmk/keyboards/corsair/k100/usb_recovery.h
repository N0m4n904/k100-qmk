// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "host_presence.h"

/** When a host first sent frames, and whether one has configured the keyboard since. */
typedef struct {
    /** The clock at the first frame, never zero once set; zero while no frame has arrived. */
    uint32_t first_frame_at;
    bool     configured;
} k100_usb_recovery_t;

/**
 * @brief   Whether a host has sent frames for longer than the timeout without configuring the
 *          keyboard, so that this firmware has to hand it to the bootloader. Once per trip round
 *          the main loop.
 */
bool k100_usb_recovery_is_due(k100_usb_recovery_t *recovery, enum k100_host_presence presence,
                              bool configured, uint32_t now_ms);
