// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

/*
 * Bytes for a reply on the custom VIA channel. Each writes at @p at and returns where it stopped.
 */
uint8_t *k100_put_number(uint8_t *at, uint32_t value);
uint8_t *k100_put_text(uint8_t *at, const char *text);
uint8_t *k100_put_hex(uint8_t *at, uint32_t value);
uint8_t *k100_put_be16(uint8_t *at, uint16_t value);
uint8_t *k100_put_be32(uint8_t *at, uint32_t value);
