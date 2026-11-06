// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * What happened before the keyboard stopped, kept where a power cycle cannot clear it.
 *
 * Nothing else survives the failure this exists for: the main loop stops, the watchdog takes
 * the keyboard into Corsair's bootloader, and somebody unplugs it. So USB events go into the
 * part's EEPROM as they happen, and where the core was goes there just before the reset.
 *
 * **Where.** Right after QMK's region, which is the only place that costs nobody their keymap:
 * see lighting_store.c for what moving the shape of that region does. If that region is ever
 * sized differently the log moves with it and the old one is simply not found. Two rings, so
 * that a host bouncing the bus cannot push the one crash out of the log that the log is for:
 *
 *     events    240 slots of 2 words    meta, time
 *     crashes     8 slots of 8 words    meta, time, pc, lr, psr, cfsr, fault address, link
 *
 * That is 544 words, from word 544 to word 1087 at the sizes in eeprom_lpc546xx.h.
 *
 * **No head word.** Each entry carries a sequence number and the newest is found by reading
 * them, so nothing is written twice as often as anything else. The meta word holds the sequence,
 * what the entry is, how many events were dropped before it and a check of the whole entry, and
 * goes last: an entry cut short by a reset fails the check and reads as empty, and so does an
 * erased or garbage slot. The sequence is what orders entries, across boots too, and an entry's
 * time is milliseconds since its boot. A boot is the boot event in front of it. The time wraps
 * after 49.7 days of uptime, which only shows in what is printed.
 *
 * **Wear.** NXP guarantee 100000 cycles per word (LPC546xx data sheet Rev. 2.8, table 23,
 * checked 2026-11-06). At one event per WRITE_EARNED_MS the 240 slots last 91 years of a
 * host doing nothing but bouncing the bus. What was refused is counted and carried by the next
 * entry that is written, so a storm still shows, at the size it was.
 *
 * **Cost.** An event is two words at about 6 ms each, 4 of them lpcEepromStart() waking the
 * cell and 2 the program itself, so about three matrix scans, on the rare trip round the loop
 * where one happens. Nothing is written otherwise, and a clear writes one slot per trip.
 *
 * **It must not be what breaks the keyboard.** The first write that fails ends writing until
 * the next boot, so a dead cell costs one wait and not one per event, and nothing here is
 * ever waited on or retried.
 */

#include "quantum.h"
#include "eeprom_lpc546xx.h"
#include "hal.h"
#include "usb_device_state.h"

#include "lpc546xx_crash.h"
#include "lpc546xx_eeprom.h"
#include "k100.h"

#define LOG_FIRST_WORD (LPC546XX_EEPROM_RESERVED_WORDS + LPC546XX_ONBOARD_EEPROM_SIZE / 4U)

/* The reply says which position it answers in one byte, hence the limit on these. */
#define EVENT_SLOTS 240U
#define CRASH_SLOTS 8U

/*
 * Every word of a crash and the first two of an event, which are all the fields an event has.
 * Only the words a ring holds are ever read or written.
 */
typedef struct {
    uint32_t meta;
    uint32_t time_ms;
    uint32_t pc;
    uint32_t lr;
    uint32_t psr;
    uint32_t cfsr;
    uint32_t fault_address;
    /* The sequence of the last event before the crash, over BASEPRI in the low byte. */
    uint32_t link;
} log_entry_t;

#define EVENT_WORDS 2U
#define CRASH_WORDS (sizeof(log_entry_t) / sizeof(uint32_t))

_Static_assert(EVENT_SLOTS <= UINT8_MAX && CRASH_SLOTS <= UINT8_MAX,
               "a position in the log has to fit the byte the tool asks for it with");
_Static_assert(LOG_FIRST_WORD + EVENT_SLOTS * EVENT_WORDS + CRASH_SLOTS * CRASH_WORDS <=
                   LPC_EEPROM_WORDS,
               "the log does not fit the part's EEPROM after QMK's region");

typedef struct {
    uint32_t first_word;
    uint16_t slots;
    uint8_t  words;
} log_ring_t;

static const log_ring_t event_ring = {LOG_FIRST_WORD, EVENT_SLOTS, EVENT_WORDS};
static const log_ring_t crash_ring = {LOG_FIRST_WORD + EVENT_SLOTS * EVENT_WORDS, CRASH_SLOTS, CRASH_WORDS};

/*
 * The meta word: check:12, sequence:10, code:5, dropped:5. A sequence has to tell apart entries
 * a ring's length apart, and what is left goes to the check, because a slot of garbage has one
 * chance in 4096 of passing it.
 */
#define SEQUENCE_MASK 0x3FFU
#define CODE_MASK 0x1FU
#define DROPPED_MAX 0x1FU

_Static_assert(EVENT_SLOTS <= SEQUENCE_MASK / 2U && CRASH_SLOTS <= SEQUENCE_MASK / 2U,
               "the newest entry in a ring cannot be told from the sequence");

static uint32_t meta_check(uint32_t meta) {
    return meta >> 20;
}

static uint32_t meta_sequence(uint32_t meta) {
    return (meta >> 10) & SEQUENCE_MASK;
}

static uint32_t meta_code(uint32_t meta) {
    return (meta >> 5) & CODE_MASK;
}

static uint32_t meta_dropped(uint32_t meta) {
    return meta & DROPPED_MAX;
}

static uint32_t pack_meta(uint32_t sequence, uint32_t code, uint32_t dropped) {
    return (sequence & SEQUENCE_MASK) << 10 | (code & CODE_MASK) << 5 | dropped;
}

/* The codes a slot of all zeros and a slot of all ones decode to, which no entry may use. */
#define CODE_ZEROED 0U
#define CODE_ERASED CODE_MASK

/*
 * How many events may be written at once, which covers everything a sleep and a wake do several
 * times over, and how long it takes to earn the next one after that.
 */
#define WRITES_IN_A_BURST 32U
#define WRITE_EARNED_MS (2UL * 60UL * 1000UL)

typedef struct {
    uint16_t slot;
    uint16_t sequence;
} log_position_t;

static log_position_t event_position;

static bool log_writable;
static uint8_t dropped_events;

static uint32_t write_budget;
static uint32_t budget_counted_at;

/** Read by the crash report, which may not read the clock itself: that takes the kernel's lock. */
static volatile uint32_t last_pass_ms;

#define CLEAR_ENTRIES (EVENT_SLOTS + CRASH_SLOTS)

/** The next entry a clear has to look at, or CLEAR_ENTRIES when no clear is running. */
static uint16_t clear_cursor = CLEAR_ENTRIES;

/** One FNV-1 pass over the words the check covers, of which the top twelve bits are the check. */
static uint32_t check_of(const log_ring_t *ring, const log_entry_t *entry) {
    const uint32_t *words = (const uint32_t *)entry;
    uint32_t        hash  = (2166136261UL ^ (entry->meta & 0xFFFFFUL)) * 16777619UL;

    for (uint8_t word = 1; word < ring->words; word++) {
        hash = (hash ^ words[word]) * 16777619UL;
    }

    return hash >> 20;
}

static bool read_entry(const log_ring_t *ring, uint16_t slot, log_entry_t *entry) {
    uint32_t      *words = (uint32_t *)entry;
    const uint32_t first = ring->first_word + slot * ring->words;

    for (uint8_t word = 0; word < ring->words; word++) {
        words[word] = lpcEepromRead(first + word);
    }

    const uint32_t code = meta_code(entry->meta);

    return code != CODE_ZEROED && code != CODE_ERASED &&
           meta_check(entry->meta) == check_of(ring, entry);
}

/** Skipping a word that already holds the value is one write less on the cell. */
static void write_word(uint32_t word, uint32_t value) {
    if (log_writable && lpcEepromRead(word) != value && !lpcEepromWrite(word, value)) {
        log_writable = false;
    }
}

static void write_entry(const log_ring_t *ring, uint16_t slot, const log_entry_t *entry) {
    const uint32_t *words = (const uint32_t *)entry;
    const uint32_t  first = ring->first_word + slot * ring->words;

    for (uint8_t word = ring->words - 1U; word > 0U; word--) {
        write_word(first + word, words[word]);
    }

    write_word(first, entry->meta | check_of(ring, entry) << 20);
}

/** Half the range apart is as far as this can tell, and a ring is far smaller than that. */
static bool is_newer(uint32_t sequence, uint32_t than) {
    const uint32_t ahead = (sequence - than) & SEQUENCE_MASK;

    return ahead != 0U && ahead <= SEQUENCE_MASK / 2U;
}

/** Where the entry after the newest goes, and with which sequence. */
static log_position_t find_next_position(const log_ring_t *ring) {
    log_position_t next   = {.slot = 0, .sequence = 0};
    uint32_t       newest = 0;
    bool           found  = false;

    for (uint16_t slot = 0; slot < ring->slots; slot++) {
        log_entry_t entry;

        if (read_entry(ring, slot, &entry) && (!found || is_newer(meta_sequence(entry.meta), newest))) {
            found         = true;
            newest        = meta_sequence(entry.meta);
            next.slot     = (slot + 1U) % ring->slots;
            next.sequence = (newest + 1U) & SEQUENCE_MASK;
        }
    }

    return next;
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

    if (clear_cursor < CLEAR_ENTRIES || !spend_write_budget(now)) {
        dropped_events = MIN(dropped_events + 1U, DROPPED_MAX);
        return;
    }

    const log_entry_t entry = {
        .meta    = pack_meta(event_position.sequence, kind, dropped_events),
        .time_ms = now,
    };

    write_entry(&event_ring, event_position.slot, &entry);

    dropped_events          = 0;
    event_position.slot     = (event_position.slot + 1U) % EVENT_SLOTS;
    event_position.sequence = (event_position.sequence + 1U) & SEQUENCE_MASK;
}

void k100_log_start(void) {
    /* Reads fault on a cell that has not been powered up, and this is the first one. */
    lpcEepromStart();

    event_position = find_next_position(&event_ring);
    write_budget   = WRITES_IN_A_BURST;
    log_writable   = true;

    k100_log_event(K100_EVENT_BOOT);

    lpcCrashCaptureStart();
}

/*
 * From the port, in an exception and maybe above the kernel's mask: no kernel call, and that
 * includes the clock, so the time is that of the last trip round the main loop, which is a
 * better answer anyway. Nothing from RAM is trusted about where the ring is.
 */
void lpcCrashReport(const lpc_crash_t *crash) {
    if (log_writable) {
        /* The main loop may have been in the middle of programming a word. */
        (void)lpcEepromAwaitProgram();

        const log_position_t next  = find_next_position(&crash_ring);
        const log_entry_t    entry = {
               .meta          = pack_meta(next.sequence, crash->cause, 0),
               .time_ms       = last_pass_ms,
               .pc            = crash->pc,
               .lr            = crash->lr,
               .psr           = crash->psr,
               .cfsr          = crash->cfsr,
               .fault_address = crash->fault_address,
               .link          = ((event_position.sequence - 1U) & SEQUENCE_MASK) << 8 | crash->basepri,
        };

        write_entry(&crash_ring, next.slot, &entry);
    }

#ifdef K100_USB_DIAGNOSTIC
    if (crash->cause != LPC_CRASH_WATCHDOG_WARNING) {
        k100_diagnostic_fault(crash->cause - LPC_CRASH_HARD_FAULT + 1U);
    }
#endif
}

/*
 * QMK also calls this from the USB interrupt, for a change of protocol, idle rate or LEDs.
 * Those leave the state alone, and a write there would hold the interrupt for the length of it.
 */
void notify_usb_device_state_change_kb(struct usb_device_state state) {
    static usb_configure_state_t recorded = USB_DEVICE_STATE_NO_INIT;

    if (__get_IPSR() == 0U && state.configure_state != recorded) {
        switch (state.configure_state) {
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
        recorded = state.configure_state;
    }

    notify_usb_device_state_change_user(state);
}

/** Clears the next entry that holds anything, leaving the rest of the trip to the main loop. */
static void clear_next_entry(void) {
    while (clear_cursor < CLEAR_ENTRIES) {
        const bool        in_events = clear_cursor < EVENT_SLOTS;
        const log_ring_t *ring      = in_events ? &event_ring : &crash_ring;
        const uint16_t    slot      = in_events ? clear_cursor : clear_cursor - EVENT_SLOTS;
        log_entry_t       entry;

        clear_cursor++;

        if (read_entry(ring, slot, &entry)) {
            write_word(ring->first_word + slot * ring->words, 0U);
            return;
        }
    }
}

void k100_log_clear(void) {
    clear_cursor = 0;
}

#ifdef K100_DIAGNOSTIC_HANG
/*
 * Proving the chain on the keyboard, with nobody unplugging anything.
 *
 * Off by default, because a keyboard should not stop when a host asks it to. Built with
 *
 *     qmk/test/build-k100.sh <dir> EXTRAFLAGS=-DK100_DIAGNOSTIC_HANG
 *
 * `tools/k100-fault-log.py --hang` then stops the main loop. The watchdog's warning has to
 * find the core inside one of these three functions, whichever is asked for, and the
 * bootloader has to keep the keyboard afterwards. A normal image written over it leaves the
 * EEPROM alone, and the log then shows a PC that addr2line puts inside the function asked for.
 * Separate functions and never inlined, so that it can only be that one.
 *
 * On the next trip round rather than in the command, so that the command is answered first.
 */
enum { HANG_KERNEL_UNLOCKED = 1, HANG_KERNEL_LOCKED = 2, FAULT_ON_REQUEST = 3 };

static uint8_t requested_hang;

void k100_diagnostic_request_hang(uint8_t how) {
    requested_hang = how;
}

__attribute__((noinline)) static void hang_with_kernel_unlocked(void) {
    for (;;) {
    }
}

__attribute__((noinline)) static void hang_with_kernel_locked(void) {
    chSysLock();
    for (;;) {
    }
}

__attribute__((noinline)) static void fault_on_purpose(void) {
    __builtin_trap();
}

static void hang_if_requested(void) {
    switch (requested_hang) {
        case HANG_KERNEL_UNLOCKED:
            hang_with_kernel_unlocked();
            break;
        case HANG_KERNEL_LOCKED:
            hang_with_kernel_locked();
            break;
        case FAULT_ON_REQUEST:
            fault_on_purpose();
            break;
        default:
            break;
    }
}
#endif

void k100_log_task(void) {
    last_pass_ms = timer_read32();

    if (clear_cursor < CLEAR_ENTRIES) {
        clear_next_entry();
    }

#ifdef K100_DIAGNOSTIC_HANG
    hang_if_requested();
#endif
}

/** Where the entry @p age places behind the next one is. */
static uint16_t slot_by_age(const log_ring_t *ring, log_position_t next, uint8_t age) {
    return (next.slot + ring->slots - 1U - age) % ring->slots;
}

static uint8_t *put_hex(uint8_t *at, uint32_t value) {
    for (int8_t shift = 28; shift >= 0; shift -= 4) {
        *at++ = "0123456789abcdef"[(value >> shift) & 0xFU];
    }

    return at;
}

static uint8_t *put_be32(uint8_t *at, uint32_t value) {
    for (int8_t shift = 24; shift >= 0; shift -= 8) {
        *at++ = (uint8_t)(value >> shift);
    }

    return at;
}

static const char *event_name(uint32_t code) {
    switch (code) {
        case K100_EVENT_BOOT:
            return "boot";
        case K100_EVENT_USB_RESET:
            return "reset";
        case K100_EVENT_USB_CONFIGURED:
            return "configured";
        case K100_EVENT_USB_SUSPEND:
            return "suspend";
        case K100_EVENT_USB_WAKEUP:
            return "wakeup";
        case K100_EVENT_HOST_GONE:
            return "host gone";
        case K100_EVENT_HOST_BACK:
            return "host back";
        case K100_EVENT_BOOTLOADER_JUMP:
            return "bootloader";
        default:
            return "unknown";
    }
}

static const char *crash_name(uint32_t code) {
    switch (code) {
        case LPC_CRASH_WATCHDOG_WARNING:
            return "stall";
        case LPC_CRASH_HARD_FAULT:
            return "hard";
        case LPC_CRASH_MEM_MANAGE:
            return "mem";
        case LPC_CRASH_BUS_FAULT:
            return "bus";
        case LPC_CRASH_USAGE_FAULT:
            return "usage";
        default:
            return "?";
    }
}

/** "suspend +1234 s", which is seconds since that entry's boot. Fits VIA's 28 characters. */
static uint8_t *put_seconds(uint8_t *at, uint32_t time_ms) {
    at = k100_put_text(at, " +");
    at = k100_put_number(at, time_ms / 1000U);

    return k100_put_text(at, " s");
}

void k100_log_last_event_get(uint8_t *value_data) {
    log_entry_t entry;
    uint8_t    *at = value_data;

    if (read_entry(&event_ring, slot_by_age(&event_ring, event_position, 0), &entry)) {
        at = k100_put_text(at, event_name(meta_code(entry.meta)));
        at = put_seconds(at, entry.time_ms);
    } else {
        at = k100_put_text(at, "none");
    }

    *at = '\0';
}

/** "stall pc 00012a3c +1234 s". The tool has the rest of it. */
void k100_log_last_crash_get(uint8_t *value_data) {
    log_entry_t entry;
    uint8_t    *at = value_data;

    if (read_entry(&crash_ring, slot_by_age(&crash_ring, find_next_position(&crash_ring), 0), &entry)) {
        at = k100_put_text(at, crash_name(meta_code(entry.meta)));
        at = k100_put_text(at, " pc ");
        at = put_hex(at, entry.pc);
        at = put_seconds(at, entry.time_ms);
    } else {
        at = k100_put_text(at, "none");
    }

    *at = '\0';
}

/*
 * The replies, all big-endian. The position stays where the request put it, in the first byte,
 * and a code of zero says that position holds nothing.
 *
 *     event    code:8 dropped:8 sequence:16 time:32
 *     crash    code:8 after:16 time:32 pc:32 lr:32 psr:32 basepri:8 cfsr:32 address:32
 */
bool k100_log_event_get(uint8_t *value_data) {
    const uint8_t age = value_data[0];
    log_entry_t   entry;
    uint8_t      *at = &value_data[1];

    if (age >= EVENT_SLOTS) {
        return false;
    }

    if (!read_entry(&event_ring, slot_by_age(&event_ring, event_position, age), &entry)) {
        *at = 0;
        return true;
    }

    *at++ = (uint8_t)meta_code(entry.meta);
    *at++ = (uint8_t)meta_dropped(entry.meta);
    *at++ = (uint8_t)(meta_sequence(entry.meta) >> 8);
    *at++ = (uint8_t)meta_sequence(entry.meta);
    (void)put_be32(at, entry.time_ms);

    return true;
}

bool k100_log_crash_get(uint8_t *value_data) {
    const uint8_t age = value_data[0];
    log_entry_t   entry;
    uint8_t      *at = &value_data[1];

    if (age >= CRASH_SLOTS) {
        return false;
    }

    if (!read_entry(&crash_ring, slot_by_age(&crash_ring, find_next_position(&crash_ring), age), &entry)) {
        *at = 0;
        return true;
    }

    *at++ = (uint8_t)meta_code(entry.meta);
    *at++ = (uint8_t)(entry.link >> 16);
    *at++ = (uint8_t)(entry.link >> 8);
    at    = put_be32(at, entry.time_ms);
    at    = put_be32(at, entry.pc);
    at    = put_be32(at, entry.lr);
    at    = put_be32(at, entry.psr);
    *at++ = (uint8_t)entry.link;
    at    = put_be32(at, entry.cfsr);
    (void)put_be32(at, entry.fault_address);

    return true;
}
