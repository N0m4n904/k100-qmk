// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

/*
 * 190 LEDs across three IS31FL3743B drivers, as the firmware's LedInfoMap
 * declares: 184 RGB keys and bars plus 6 single-colour indicators. The table
 * itself is generated; see tools/generate-led-tables.py.
 *
 * Not yet RGB_MATRIX_LED_COUNT, because the generated QMK layout covers the 174
 * positions that have a key or a known place on the board, and reconciling the
 * two is its own piece of work -- see docs/pin-mapping.md on the LED counts.
 */
#define IS31FL3743B_LED_COUNT 190
#define IS31FL3743B_DRIVER_COUNT 3
