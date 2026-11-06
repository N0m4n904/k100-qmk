// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * What happened before the keyboard stopped, in EEPROM where a power cycle cannot clear it.
 * docs/qmk-layout.md has the design.
 */

#include "quantum.h"
#include "eeprom_lpc546xx.h"
#include "hal.h"
#include "usb_device_state.h"

#include "lpc546xx_crash.h"
#include "lpc546xx_eeprom.h"
#include "fault_log.h"
#include "fault_log_ring.h"
#include "k100.h"

/* Right after the region reserved for QMK, so the shape of what QMK stores does not move. */
#define LOG_FIRST_WORD (LPC546XX_EEPROM_RESERVED_WORDS + LPC546XX_ONBOARD_EEPROM_SIZE / 4U)

/* The word positions of an entry. Word zero is the meta word of the ring. */
enum { ENTRY_META, ENTRY_TIME, EVENT_WORDS, CRASH_PC = EVENT_WORDS, CRASH_LR, CRASH_PSR, CRASH_CFSR, CRASH_FAULT_ADDRESS, CRASH_LINK, CRASH_WORDS };

_Static_assert(K100_LOG_EVENT_SLOTS <= UINT8_MAX && K100_LOG_CRASH_SLOTS <= UINT8_MAX,
               "a position in the log has to fit in the byte the tool sends");
_Static_assert(K100_LOG_EVENT_SLOTS <= LOG_SEQUENCE_MASK / 2U && K100_LOG_CRASH_SLOTS <= LOG_SEQUENCE_MASK / 2U,
               "the sequence wraps within one ring, so the newest entry cannot be found");
_Static_assert(LOG_FIRST_WORD + K100_LOG_EVENT_SLOTS * EVENT_WORDS + K100_LOG_CRASH_SLOTS * CRASH_WORDS <= LPC_EEPROM_WORDS,
               "the log does not fit in the EEPROM after the region reserved for QMK");
_Static_assert(K100_EVENT_BOOTLOADER_JUMP < LOG_CODE_ERASED && LPC_CRASH_STALL < LOG_CODE_ERASED,
               "a stored code would read as an erased slot");
_Static_assert(CRASH_WORDS <= LOG_RING_MAX_WORDS, "an entry is longer than the ring code reads");

static const log_ring_t event_ring = {
    .first_word = LOG_FIRST_WORD,
    .slots      = K100_LOG_EVENT_SLOTS,
    .words      = EVENT_WORDS,
    .read_word  = lpcEepromRead,
    .write_word = lpcEepromWrite,
};

static const log_ring_t crash_ring = {
    .first_word = LOG_FIRST_WORD + K100_LOG_EVENT_SLOTS * EVENT_WORDS,
    .slots      = K100_LOG_CRASH_SLOTS,
    .words      = CRASH_WORDS,
    .read_word  = lpcEepromRead,
    .write_word = lpcEepromWrite,
};

/* The link word of a crash: the low bits of the event time, the event sequence, BASEPRI. */
#define LINK_TIME_SHIFT 18U
#define LINK_TIME_MASK (~0UL << LINK_TIME_SHIFT)
#define LINK_SEQUENCE_SHIFT 8U
#define LINK_BASEPRI_MASK 0xFFU

/*
 * Guaranteed by NXP: LPC546xx data sheet Rev. 2.8, table 23, Nendu, checked 2026-11-06. The sheet
 * does not say whether a program wears the word or its page, so this counts the page.
 */
#define EEPROM_ENDURANCE_CYCLES 100000ULL
#define EEPROM_WORDS_PER_PAGE (LPC_EEPROM_WORDS / FSL_FEATURE_EEPROM_PAGE_COUNT)

/* The decision: a host that does nothing but reconnect does not wear out the ring for this long. */
#define LOG_LIFETIME_MS (10ULL * 365ULL * 24ULL * 3600ULL * 1000ULL)

/*
 * Each event programs EVENT_WORDS words of its page, which a page can take this many times, and
 * the ring has this many pages. What the two products leave of the lifetime is the interval.
 */
#define LOG_EVENTS_IN_LIFETIME (EEPROM_ENDURANCE_CYCLES / EVENT_WORDS * (K100_LOG_EVENT_SLOTS * EVENT_WORDS / EEPROM_WORDS_PER_PAGE))
#define WRITE_EARNED_MS ((uint32_t)(LOG_LIFETIME_MS / LOG_EVENTS_IN_LIFETIME))

/* Covers what a sleep and a wake do several times over. */
#define WRITES_IN_A_BURST 32U

static log_position_t event_position;

/* A failed write ends writing until the next boot, so a dead cell costs one wait. */
static bool log_writable;

static uint8_t  dropped_events;
static uint32_t write_budget;
static uint32_t budget_counted_at;

/** Read by the crash report, which may not read the clock itself: that acquires the kernel's lock. */
static volatile uint32_t last_pass_ms;

#define CLEAR_ENTRIES (K100_LOG_EVENT_SLOTS + K100_LOG_CRASH_SLOTS)

/** The next entry a clear has to look at, or CLEAR_ENTRIES when no clear is running. */
static uint16_t clear_cursor = CLEAR_ENTRIES;

/* As deep as the queue QMK drains into the hook below, in one pass of the main loop. */
#define USB_STATE_QUEUE_LENGTH 16U

static usb_configure_state_t usb_state_queue[USB_STATE_QUEUE_LENGTH];
static uint8_t               usb_state_head;
static uint8_t               usb_state_count;
static usb_configure_state_t usb_state_queued = USB_DEVICE_STATE_NO_INIT;

static bool event_kind_from_code(uint32_t code, enum k100_log_event_kind *kind) {
    switch (code) {
        case K100_EVENT_BOOT:
        case K100_EVENT_USB_RESET:
        case K100_EVENT_USB_CONFIGURED:
        case K100_EVENT_USB_SUSPEND:
        case K100_EVENT_USB_WAKEUP:
        case K100_EVENT_HOST_GONE:
        case K100_EVENT_HOST_BACK:
        case K100_EVENT_BOOTLOADER_JUMP:
            *kind = (enum k100_log_event_kind)code;
            return true;
        default:
            return false;
    }
}

static bool crash_cause_from_code(uint32_t code, lpc_crash_cause_t *cause) {
    switch (code) {
        case LPC_CRASH_HARD:
        case LPC_CRASH_MEM:
        case LPC_CRASH_BUS:
        case LPC_CRASH_USAGE:
        case LPC_CRASH_STALL:
            *cause = (lpc_crash_cause_t)code;
            return true;
        default:
            return false;
    }
}

/** Whether an event may be written now, with the budget brought up to date. */
static bool spend_write_budget(uint32_t now) {
    const uint32_t earned = (now - budget_counted_at) / WRITE_EARNED_MS;

    if (earned > 0U) {
        write_budget = MIN(write_budget + earned, WRITES_IN_A_BURST);
        budget_counted_at += earned * WRITE_EARNED_MS;
    }

    if (write_budget == 0U) {
        return false;
    }

    write_budget--;
    return true;
}

void k100_log_event(enum k100_log_event_kind kind) {
    if (!log_writable) {
        return;
    }

    const uint32_t now = timer_read32();

    if (k100_log_clear_is_running() || !spend_write_budget(now)) {
        dropped_events = MIN(dropped_events + 1U, LOG_DROPPED_MAX);
        return;
    }

    const uint32_t words[EVENT_WORDS] = {
        [ENTRY_META] = log_meta(event_position.sequence, kind, dropped_events),
        [ENTRY_TIME] = now,
    };

    log_writable = log_ring_write(&event_ring, event_position.slot, words);

    dropped_events          = 0;
    event_position.slot     = (event_position.slot + 1U) % K100_LOG_EVENT_SLOTS;
    event_position.sequence = (event_position.sequence + 1U) & LOG_SEQUENCE_MASK;
}

void k100_log_start(void) {
    /* A read from a cell that has not been powered up faults, and this is the first read. */
    lpcEepromStart();

    event_position = log_ring_next(&event_ring);
    write_budget   = WRITES_IN_A_BURST;
    log_writable   = true;

    k100_log_event(K100_EVENT_BOOT);

    lpcCrashCaptureStart();
}

/*
 * The word that says which event a crash came after. Ten bits of sequence come round, and the low
 * bits of the time tell that event from a later one with the same number.
 */
static uint32_t crash_link(uint32_t basepri) {
    const log_position_t next = log_ring_next(&event_ring);
    uint32_t             words[EVENT_WORDS];

    /* An empty ring leaves a time no later read matches, which is the answer for it too. */
    (void)log_ring_read(&event_ring, log_ring_slot_by_age(&event_ring, next, 0), words);

    return words[ENTRY_TIME] << LINK_TIME_SHIFT | ((next.sequence - 1U) & LOG_SEQUENCE_MASK) << LINK_SEQUENCE_SHIFT |
           (basepri & LINK_BASEPRI_MASK);
}

/*
 * Called from the port in an exception, maybe above the kernel's mask, so nothing here may call
 * the kernel, the clock included. From RAM this needs only log_writable and last_pass_ms: the
 * positions come from the EEPROM, since the main loop may have been between two of its updates.
 */
void lpcCrashReport(const lpc_crash_t *crash) {
    if (log_writable) {
        /* The main loop may have been in the middle of programming a word. */
        (void)lpcEepromAwaitProgram();

        const log_position_t next = log_ring_next(&crash_ring);
        const uint32_t       words[CRASH_WORDS] = {
            [ENTRY_META]          = log_meta(next.sequence, crash->cause, 0),
            [ENTRY_TIME]          = last_pass_ms,
            [CRASH_PC]            = crash->pc,
            [CRASH_LR]            = crash->lr,
            [CRASH_PSR]           = crash->psr,
            [CRASH_CFSR]          = crash->cfsr,
            [CRASH_FAULT_ADDRESS] = crash->fault_address,
            [CRASH_LINK]          = crash_link(crash->basepri),
        };

        (void)log_ring_write(&crash_ring, next.slot, words);
    }

#ifdef K100_USB_DIAGNOSTIC
    k100_diagnostic_fault(crash->cause);
#endif
}

bool k100_log_read_event(uint8_t age, k100_log_event_t *event) {
    uint32_t words[EVENT_WORDS];

    const uint16_t slot = log_ring_slot_by_age(&event_ring, event_position, age);

    if (!log_ring_read(&event_ring, slot, words) ||
        !event_kind_from_code(log_meta_code(words[ENTRY_META]), &event->kind)) {
        return false;
    }

    event->dropped  = (uint8_t)log_meta_dropped(words[ENTRY_META]);
    event->sequence = (uint16_t)log_meta_sequence(words[ENTRY_META]);
    event->time_ms  = words[ENTRY_TIME];

    return true;
}

/** The sequence a link names, if the event ring still has that event. */
static uint16_t linked_sequence(uint32_t link) {
    const uint32_t sequence = (link >> LINK_SEQUENCE_SHIFT) & LOG_SEQUENCE_MASK;
    const uint32_t age      = (event_position.sequence - 1U - sequence) & LOG_SEQUENCE_MASK;
    uint32_t       words[EVENT_WORDS];

    if (age >= K100_LOG_EVENT_SLOTS) {
        return K100_LOG_NO_EVENT;
    }

    const uint16_t slot = log_ring_slot_by_age(&event_ring, event_position, (uint16_t)age);

    if (!log_ring_read(&event_ring, slot, words) || log_meta_sequence(words[ENTRY_META]) != sequence ||
        words[ENTRY_TIME] << LINK_TIME_SHIFT != (link & LINK_TIME_MASK)) {
        return K100_LOG_NO_EVENT;
    }

    return (uint16_t)sequence;
}

bool k100_log_read_crash(uint8_t age, k100_log_crash_t *crash) {
    uint32_t words[CRASH_WORDS];

    const uint16_t slot = log_ring_slot_by_age(&crash_ring, log_ring_next(&crash_ring), age);

    if (!log_ring_read(&crash_ring, slot, words) ||
        !crash_cause_from_code(log_meta_code(words[ENTRY_META]), &crash->cause)) {
        return false;
    }

    crash->after_sequence = linked_sequence(words[CRASH_LINK]);
    crash->basepri        = (uint8_t)(words[CRASH_LINK] & LINK_BASEPRI_MASK);
    crash->time_ms        = words[ENTRY_TIME];
    crash->pc             = words[CRASH_PC];
    crash->lr             = words[CRASH_LR];
    crash->psr            = words[CRASH_PSR];
    crash->cfsr           = words[CRASH_CFSR];
    crash->fault_address  = words[CRASH_FAULT_ADDRESS];

    return true;
}

/*
 * QMK calls this on the main thread for the state changes and from the USB interrupt for LEDs,
 * idle rate and protocol. A write would block the interrupt for as long as it takes, so this only
 * queues, and only before k100_log_start() does it let the state go unrecorded.
 */
void notify_usb_device_state_change_kb(struct usb_device_state state) {
    const syssts_t status = chSysGetStatusAndLockX();

    if (state.configure_state != usb_state_queued) {
        usb_state_queued = state.configure_state;

        if (log_writable && usb_state_count < USB_STATE_QUEUE_LENGTH) {
            usb_state_queue[(usb_state_head + usb_state_count) % USB_STATE_QUEUE_LENGTH] = state.configure_state;
            usb_state_count++;
        }
    }

    chSysRestoreStatusX(status);

    notify_usb_device_state_change_user(state);
}

static bool next_queued_usb_state(usb_configure_state_t *state) {
    const syssts_t status = chSysGetStatusAndLockX();
    const bool     queued = usb_state_count > 0U;

    if (queued) {
        *state         = usb_state_queue[usb_state_head];
        usb_state_head = (usb_state_head + 1U) % USB_STATE_QUEUE_LENGTH;
        usb_state_count--;
    }

    chSysRestoreStatusX(status);

    return queued;
}

static void record_usb_states(void) {
    static usb_configure_state_t recorded = USB_DEVICE_STATE_NO_INIT;
    usb_configure_state_t        state;

    while (next_queued_usb_state(&state)) {
        switch (state) {
            case USB_DEVICE_STATE_CONFIGURED:
                k100_log_event(recorded == USB_DEVICE_STATE_SUSPEND ? K100_EVENT_USB_WAKEUP : K100_EVENT_USB_CONFIGURED);
                break;
            case USB_DEVICE_STATE_SUSPEND:
                k100_log_event(K100_EVENT_USB_SUSPEND);
                break;
            default:
                k100_log_event(K100_EVENT_USB_RESET);
                break;
        }
        recorded = state;
    }
}

static void record_host_presence(void) {
    static bool recorded = false;
    const bool  gone     = k100_host_is_gone();

    if (gone != recorded) {
        k100_log_event(gone ? K100_EVENT_HOST_GONE : K100_EVENT_HOST_BACK);
        recorded = gone;
    }
}

/** Clears the next entry that contains anything, and leaves the rest of the pass to the main loop. */
static void clear_next_entry(void) {
    while (k100_log_clear_is_running()) {
        const bool        in_events = clear_cursor < K100_LOG_EVENT_SLOTS;
        const log_ring_t *ring      = in_events ? &event_ring : &crash_ring;
        const uint16_t    slot      = in_events ? clear_cursor : clear_cursor - K100_LOG_EVENT_SLOTS;
        uint32_t          words[LOG_RING_MAX_WORDS];

        clear_cursor++;

        if (log_ring_read(ring, slot, words)) {
            log_writable = log_writable && log_ring_erase(ring, slot);
            return;
        }
    }
}

void k100_log_clear(void) {
    clear_cursor = 0;
}

bool k100_log_clear_is_running(void) {
    return clear_cursor < CLEAR_ENTRIES;
}

void k100_log_task(void) {
    last_pass_ms = timer_read32();

    record_usb_states();
    record_host_presence();

    if (k100_log_clear_is_running()) {
        clear_next_entry();
    }
}
