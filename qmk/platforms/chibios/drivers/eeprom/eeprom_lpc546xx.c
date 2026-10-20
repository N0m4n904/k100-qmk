// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * QMK's EEPROM, on the LPC546xx's own EEPROM.
 *
 * The part has 16 KB of real EEPROM: readable as ordinary memory, written a
 * word at a time by the hardware, and keeping its contents with no power. That
 * makes this one of the plain cases -- there is no flash page to emulate, no
 * wear levelling to arrange and nothing to copy to RAM.
 *
 * What it does have is granularity. A write lands on a whole word, so a byte
 * at a time means read, replace, program. The read costs a load, since the
 * array is memory-mapped, and the program is skipped when the word already
 * holds what it would be given -- which matters here, because every program is
 * a cell write and QMK writes a keymap back a block at a time.
 */

#include <stdint.h>
#include <string.h>

#include <hal.h>

#include "eeprom_driver.h"
#include "eeprom_lpc546xx.h"
#include "lpc546xx_eeprom.h"

_Static_assert(LPC546XX_EEPROM_RESERVED_WORDS * sizeof(uint32_t) +
                   LPC546XX_ONBOARD_EEPROM_SIZE <= LPC_EEPROM_SIZE,
               "the reserved words and this driver's region do not fit the "
               "part's EEPROM array");

_Static_assert(LPC546XX_ONBOARD_EEPROM_SIZE % sizeof(uint32_t) == 0U,
               "the region has to be a whole number of words, because that is "
               "what the part programs");

/**
 * @brief   One word of the region, as an index into the whole array.
 */
static inline uint32_t region_word(uintptr_t offset) {

    return LPC546XX_EEPROM_RESERVED_WORDS + (uint32_t)(offset / sizeof(uint32_t));
}

/**
 * @brief   How much of a transfer falls inside the word holding @p offset.
 */
static inline size_t within_word(uintptr_t offset, size_t remaining) {
    const size_t skipped = (size_t)(offset % sizeof(uint32_t));
    const size_t fits    = sizeof(uint32_t) - skipped;

    return fits < remaining ? fits : remaining;
}

void eeprom_driver_init(void) {

    /* Clocks the cell and brings it out of the power-down it sits in, which
       reads do not do for themselves. */
    lpcEepromStart();
}

void eeprom_driver_erase(void) {

    for (uintptr_t offset = 0; offset < LPC546XX_ONBOARD_EEPROM_SIZE;
         offset += sizeof(uint32_t)) {
        const uint32_t word = region_word(offset);

        /* Already erased is erased. Skipping those is not only faster: it is
           the difference between formatting costing one cell write per word
           that was used and one per word that exists. */
        if (lpcEepromRead(word) != 0U) {
            (void)lpcEepromWrite(word, 0U);
        }
    }
}

void eeprom_read_block(void *buf, const void *addr, size_t len) {
    uint8_t  *out    = (uint8_t *)buf;
    uintptr_t offset = (uintptr_t)addr;

    while (len > 0U) {
        const uint32_t value = lpcEepromRead(region_word(offset));
        const size_t   take  = within_word(offset, len);

        memcpy(out, (const uint8_t *)&value + (offset % sizeof(uint32_t)), take);

        offset += take;
        out += take;
        len -= take;
    }
}

void eeprom_write_block(const void *buf, void *addr, size_t len) {
    const uint8_t *in     = (const uint8_t *)buf;
    uintptr_t      offset = (uintptr_t)addr;

    while (len > 0U) {
        const uint32_t word    = region_word(offset);
        const size_t   take    = within_word(offset, len);
        const uint32_t current = lpcEepromRead(word);
        uint32_t       updated = current;

        memcpy((uint8_t *)&updated + (offset % sizeof(uint32_t)), in, take);

        if (updated != current) {
            (void)lpcEepromWrite(word, updated);
        }

        offset += take;
        in += take;
        len -= take;
    }
}
