// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include_next <mcuconf.h>

/*
 * SPI3 is Flexcomm 3, which is the block the stock firmware drives the three
 * IS31FL3743B LED drivers with; see docs/pin-mapping.md.
 */
#undef LPC_SPI_USE_SPI3
#define LPC_SPI_USE_SPI3 TRUE
