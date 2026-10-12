// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * How the IS31FL3743B drivers are reached.
 *
 * Kept behind this seam because the chip driver has no business knowing it. On
 * the K100 the three drivers share one Flexcomm SPI master and are selected by
 * its hardware chip selects, which is not how a board with GPIO chip selects or
 * a different MCU would do it.
 */

/** @brief Brings up the bus. Called once, before any send. */
void is31fl3743b_transport_init(void);

/**
 * @brief Sends one frame to one driver.
 *
 * @param index     which driver, 0 upwards
 * @param frame     the frame, starting with its two-byte header
 * @param length    total bytes including the header
 */
void is31fl3743b_transport_send(uint8_t index, const uint8_t *frame, size_t length);
