// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

/**
 * @brief   What the byte after the position says, when it is no entry code.
 *
 * @details tools/k100-fault-log.py reads this from here, and it never reaches the EEPROM.
 */
enum k100_log_reply_status {
    K100_LOG_REPLY_EMPTY    = 0,
    K100_LOG_REPLY_PAST_END = 255,
};

/** @brief The newest entries as one line of text each, for VIA labels. */
void k100_via_last_crash_get(uint8_t *value_data);
void k100_via_last_event_get(uint8_t *value_data);

/** @brief One entry for the tool, by how far it is from the newest. See via_fault_log.c. */
void k100_via_log_event_get(uint8_t *value_data);
void k100_via_log_crash_get(uint8_t *value_data);
