// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

/**
 * Where this driver's region starts, in words of the part's EEPROM array.
 *
 * The lower words are not ours. On the K100 the bootloader reads word 1 and
 * writes words 0 and 1, the stock application keeps its operating mode in word
 * 2, and the accessor both of them go through range-checks every index it is
 * given against 31 -- so the whole of words 0 to 31 is the vendor's space.
 * Handing any of it to QMK would let a keymap write decide what the keyboard
 * does at its next power-up.
 */
#ifndef LPC546XX_EEPROM_RESERVED_WORDS
#    define LPC546XX_EEPROM_RESERVED_WORDS 32U
#endif

/**
 * How much of what is left this driver offers QMK.
 *
 * Not the rest of the array, which is 16 KB. The part programs one word at a
 * time and each program is a cell write of its own, so a region's size is also
 * how long it takes to format and how much of the cell's endurance a keymap
 * write spends. This is sized for what sits in it -- QMK's configuration block
 * and a VIA dynamic keymap for an 8 by 18 matrix, which is a little over
 * 1200 bytes -- with room to grow, rather than for the space being free.
 *
 * A keyboard that needs more can define it, up to the array's own size.
 */
#ifndef LPC546XX_ONBOARD_EEPROM_SIZE
#    define LPC546XX_ONBOARD_EEPROM_SIZE 2048U
#endif
