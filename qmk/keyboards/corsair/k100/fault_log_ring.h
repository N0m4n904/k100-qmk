// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * One ring of fixed-size entries in word-programmed storage, with no head word.
 *
 * An entry is an array of words whose first is its meta word: check:12, sequence:10, code:5,
 * dropped:5. The sequence needs enough bits to tell apart two entries a full ring apart, and the
 * bits left over go to the check, because a slot of garbage has one chance in 4096 of passing it.
 */
typedef struct {
    uint32_t first_word;
    uint16_t slots;
    uint8_t  words;
    uint32_t (*read_word)(uint32_t word);
    bool (*write_word)(uint32_t word, uint32_t value);
} log_ring_t;

/** Where the entry after the newest goes, and with which sequence. */
typedef struct {
    uint16_t slot;
    uint16_t sequence;
} log_position_t;

/** The longest entry this code reads into a buffer of its own. */
#define LOG_RING_MAX_WORDS 8U

#define LOG_SEQUENCE_MASK 0x3FFU
#define LOG_DROPPED_MAX 0x1FU

/** The codes a slot of all zeros and a slot of all ones decode to, which no entry may use. */
#define LOG_CODE_ZEROED 0U
#define LOG_CODE_ERASED 0x1FU

uint32_t log_meta(uint32_t sequence, uint32_t code, uint32_t dropped);
uint32_t log_meta_sequence(uint32_t meta);
uint32_t log_meta_code(uint32_t meta);
uint32_t log_meta_dropped(uint32_t meta);

/** @return @p false when the slot is empty, erased, garbage or was cut short. */
bool log_ring_read(const log_ring_t *ring, uint16_t slot, uint32_t *words);

/**
 * @brief   Writes an entry whose meta word has no check yet, and the meta word last.
 *
 * @details A write cut short anywhere leaves a meta word that fails its check.
 *
 * @return  @p false at the first word that would not program.
 */
bool log_ring_write(const log_ring_t *ring, uint16_t slot, const uint32_t *words);

/** @brief Makes a slot read as empty. @return @p false when the word would not program. */
bool log_ring_erase(const log_ring_t *ring, uint16_t slot);

log_position_t log_ring_next(const log_ring_t *ring);

/** The slot @p age entries behind the newest, for a ring whose next entry goes at @p next. */
uint16_t log_ring_slot_by_age(const log_ring_t *ring, log_position_t next, uint16_t age);
