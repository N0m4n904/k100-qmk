// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QK_KB_0 0x7E00
#define MIN(a, b) ((a) < (b) ? (a) : (b))

uint32_t timer_read32(void);
