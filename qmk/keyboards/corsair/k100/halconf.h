// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

/* The LED drivers hang off SPI3. */
#define HAL_USE_SPI TRUE

/*
 * Chip select is carried in each data word by this part's SPI peripheral rather
 * than being a pin the HAL toggles, so the low level driver owns selection. The
 * default, SPI_SELECT_MODE_PAD, would have the HAL drive a GPIO instead, and
 * the three LED drivers are on hardware chip selects -- see
 * is31fl3743b_transport.c.
 */
#define SPI_SELECT_MODE SPI_SELECT_MODE_LLD

#include_next <halconf.h>
