// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "via_text.h"

/** Decimal, by hand: printf costs more code than a few numbers justify. */
uint8_t *k100_put_number(uint8_t *at, uint32_t value) {
    uint8_t digits[10];
    uint8_t count = 0;

    do {
        digits[count++] = '0' + value % 10;
        value /= 10;
    } while (value > 0);

    while (count > 0) {
        *at++ = digits[--count];
    }

    return at;
}

uint8_t *k100_put_text(uint8_t *at, const char *text) {
    while (*text != '\0') {
        *at++ = (uint8_t)*text++;
    }

    return at;
}

uint8_t *k100_put_hex(uint8_t *at, uint32_t value) {
    for (int8_t shift = 28; shift >= 0; shift -= 4) {
        *at++ = "0123456789abcdef"[(value >> shift) & 0xFU];
    }

    return at;
}

uint8_t *k100_put_be16(uint8_t *at, uint16_t value) {
    *at++ = (uint8_t)(value >> 8);
    *at++ = (uint8_t)value;

    return at;
}

uint8_t *k100_put_be32(uint8_t *at, uint32_t value) {
    for (int8_t shift = 24; shift >= 0; shift -= 8) {
        *at++ = (uint8_t)(value >> shift);
    }

    return at;
}
