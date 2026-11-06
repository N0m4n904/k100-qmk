// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

#define FSL_FEATURE_EEPROM_PAGE_COUNT 128U

typedef uint32_t syssts_t;

static inline syssts_t chSysGetStatusAndLockX(void) {
    return 0U;
}

static inline void chSysRestoreStatusX(syssts_t status) {
    (void)status;
}
