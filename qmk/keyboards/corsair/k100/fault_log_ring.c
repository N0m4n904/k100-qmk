// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "fault_log_ring.h"

#define META_CHECK_SHIFT 20U
#define META_SEQUENCE_SHIFT 10U
#define META_CODE_SHIFT 5U
#define META_CODE_MASK 0x1FU
#define META_UNCHECKED_MASK ((1UL << META_CHECK_SHIFT) - 1UL)

uint32_t log_meta(uint32_t sequence, uint32_t code, uint32_t dropped) {
    return (sequence & LOG_SEQUENCE_MASK) << META_SEQUENCE_SHIFT | (code & META_CODE_MASK) << META_CODE_SHIFT | (dropped & LOG_DROPPED_MAX);
}

uint32_t log_meta_sequence(uint32_t meta) {
    return (meta >> META_SEQUENCE_SHIFT) & LOG_SEQUENCE_MASK;
}

uint32_t log_meta_code(uint32_t meta) {
    return (meta >> META_CODE_SHIFT) & META_CODE_MASK;
}

uint32_t log_meta_dropped(uint32_t meta) {
    return meta & LOG_DROPPED_MAX;
}

/** One FNV-1 pass over the entry, of which the top twelve bits are the check. */
static uint32_t log_ring_check(const log_ring_t *ring, const uint32_t *words) {
    uint32_t hash = (2166136261UL ^ (words[0] & META_UNCHECKED_MASK)) * 16777619UL;

    for (uint8_t word = 1; word < ring->words; word++) {
        hash = (hash ^ words[word]) * 16777619UL;
    }

    return hash >> META_CHECK_SHIFT;
}

static uint32_t log_ring_first_word(const log_ring_t *ring, uint16_t slot) {
    return ring->first_word + (uint32_t)slot * ring->words;
}

/** Skipping a word that already contains the value saves one write on the cell. */
static bool log_ring_program(const log_ring_t *ring, uint32_t word, uint32_t value) {
    return ring->read_word(word) == value || ring->write_word(word, value);
}

bool log_ring_read(const log_ring_t *ring, uint16_t slot, uint32_t *words) {
    const uint32_t first = log_ring_first_word(ring, slot);

    for (uint8_t word = 0; word < ring->words; word++) {
        words[word] = ring->read_word(first + word);
    }

    const uint32_t code = log_meta_code(words[0]);

    return code != LOG_CODE_ZEROED && code != LOG_CODE_ERASED && words[0] >> META_CHECK_SHIFT == log_ring_check(ring, words);
}

bool log_ring_write(const log_ring_t *ring, uint16_t slot, const uint32_t *words) {
    const uint32_t first = log_ring_first_word(ring, slot);

    for (uint8_t word = ring->words - 1U; word > 0U; word--) {
        if (!log_ring_program(ring, first + word, words[word])) {
            return false;
        }
    }

    return log_ring_program(ring, first, words[0] | log_ring_check(ring, words) << META_CHECK_SHIFT);
}

bool log_ring_erase(const log_ring_t *ring, uint16_t slot) {
    return log_ring_program(ring, log_ring_first_word(ring, slot), 0U);
}

/** Sequences up to half the range apart can be ordered, and a ring is far smaller than that. */
static bool log_sequence_is_newer(uint32_t sequence, uint32_t than) {
    const uint32_t ahead = (sequence - than) & LOG_SEQUENCE_MASK;

    return ahead != 0U && ahead <= LOG_SEQUENCE_MASK / 2U;
}

log_position_t log_ring_next(const log_ring_t *ring) {
    log_position_t next   = {.slot = 0, .sequence = 0};
    uint32_t       newest = 0;
    bool           found  = false;
    uint32_t       words[LOG_RING_MAX_WORDS];

    for (uint16_t slot = 0; slot < ring->slots; slot++) {
        if (log_ring_read(ring, slot, words) && (!found || log_sequence_is_newer(log_meta_sequence(words[0]), newest))) {
            found         = true;
            newest        = log_meta_sequence(words[0]);
            next.slot     = (slot + 1U) % ring->slots;
            next.sequence = (newest + 1U) & LOG_SEQUENCE_MASK;
        }
    }

    return next;
}

uint16_t log_ring_slot_by_age(const log_ring_t *ring, log_position_t next, uint16_t age) {
    return (next.slot + ring->slots - 1U - age) % ring->slots;
}
