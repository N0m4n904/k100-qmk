// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The fault log against an EEPROM in an array. fault_log.c is included, so that a test can do what
 * a reset does to its RAM.
 *
 * With --replies it builds a log with known contents and prints the VIA replies, for
 * check_replies.py to decode with the tool. Otherwise it runs the checks and prints what they found.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quantum.h"
#include "hal.h"
#include "usb_device_state.h"
#include "eeprom_lpc546xx.h"
#include "lpc546xx_eeprom.h"
#include "lpc546xx_crash.h"
#include "fault_log.h"
#include "via_fault_log.h"
#include "k100.h"

#define REQUIRE(condition)                                                     \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/* The EEPROM. A power cut lets a number of writes through and loses the rest. */
#define LOG_WORDS_FIRST 544U
#define LOG_WORDS_END 1088U
#define NO_POWER_CUT UINT32_MAX

static uint32_t cells[LPC_EEPROM_WORDS];
static uint32_t programs[LPC_EEPROM_WORDS];
static uint32_t total_programs;
static uint32_t writes_until_power_cut = NO_POWER_CUT;
static bool     writes_fail;

void lpcEepromStart(void) {}

uint32_t lpcEepromRead(uint32_t word) {
    REQUIRE(word < LPC_EEPROM_WORDS);
    return cells[word];
}

bool lpcEepromAwaitProgram(void) {
    return true;
}

bool lpcEepromWrite(uint32_t word, uint32_t value) {
    REQUIRE(word >= LOG_WORDS_FIRST && word < LOG_WORDS_END);

    if (writes_fail) {
        return false;
    }
    if (writes_until_power_cut == 0U) {
        return true;
    }
    if (writes_until_power_cut != NO_POWER_CUT) {
        writes_until_power_cut--;
    }

    cells[word] = value;
    programs[word]++;
    total_programs++;
    return true;
}

/* What the rest of the keyboard provides. */
static uint32_t now_ms;
static bool     host_gone;

uint32_t timer_read32(void) {
    return now_ms;
}

void lpcCrashCaptureStart(void) {}

void notify_usb_device_state_change_user(struct usb_device_state state) {
    (void)state;
}

bool k100_host_is_gone(void) {
    return host_gone;
}

#include "fault_log.c"

/* Everything a reset clears, with the EEPROM left as it is. */
static void boot(void) {
    log_writable           = false;
    dropped_events         = 0;
    budget_counted_at      = 0;
    last_pass_ms           = 0;
    clear_cursor           = CLEAR_ENTRIES;
    usb_state_head         = 0;
    usb_state_count        = 0;
    usb_state_queued       = USB_DEVICE_STATE_NO_INIT;
    now_ms                 = 100;
    host_gone              = false;
    writes_until_power_cut = NO_POWER_CUT;
    k100_log_start();
}

static void erase_everything(uint32_t value) {
    for (size_t word = 0; word < LPC_EEPROM_WORDS; word++) {
        cells[word] = value;
    }
}

static k100_log_event_t event_at(uint8_t age) {
    k100_log_event_t event;
    REQUIRE(k100_log_read_event(age, &event));
    return event;
}

static bool event_is_empty(uint8_t age) {
    k100_log_event_t event;
    return !k100_log_read_event(age, &event);
}

/* An event past whatever budget is left, at a time no event has had: a multiple of the interval
   would give the low bits of the time a period the sequence shares. */
static void record_event(enum k100_log_event_kind kind) {
    now_ms += WRITE_EARNED_MS + 1U;
    k100_log_event(kind);
}

static lpc_crash_t crash_with_pc(lpc_crash_cause_t cause, uint32_t pc) {
    return (lpc_crash_t){.cause = cause, .pc = pc, .lr = pc + 4U, .psr = 0x61000000U, .basepri = 0x60U, .cfsr = 0x10000U, .fault_address = 7U};
}

static void test_empty_storage(void) {
    k100_log_crash_t crash;

    /* Before the log starts, nothing is written. */
    erase_everything(0U);
    log_writable = false;
    k100_log_event(K100_EVENT_USB_RESET);
    lpc_crash_t before = crash_with_pc(LPC_CRASH_STALL, 1U);
    lpcCrashReport(&before);
    REQUIRE(total_programs == 0U);

    /* All zeros and all ones are both empty. */
    for (int pass = 0; pass < 2; pass++) {
        erase_everything(pass == 0 ? 0U : UINT32_MAX);
        log_position_t events = log_ring_next(&event_ring);
        log_position_t crashes = log_ring_next(&crash_ring);
        REQUIRE(events.slot == 0U && events.sequence == 0U);
        REQUIRE(crashes.slot == 0U && crashes.sequence == 0U);
        boot();
        REQUIRE(event_at(0).kind == K100_EVENT_BOOT);
        REQUIRE(event_is_empty(1));
        REQUIRE(!k100_log_read_crash(0, &crash));
    }

    /* Garbage is empty too, but for the odd slot that passes a twelve-bit check. */
    for (uint32_t word = 0; word < LPC_EEPROM_WORDS; word++) {
        cells[word] = (word * 2654435761U) ^ 0x5BD1E995U;
    }
    unsigned accepted = 0;
    uint32_t words[LOG_RING_MAX_WORDS];
    for (uint16_t slot = 0; slot < K100_LOG_EVENT_SLOTS; slot++) {
        accepted += log_ring_read(&event_ring, slot, words);
    }
    REQUIRE(accepted <= 1U);
}

static void test_order_survives_a_boot(void) {
    erase_everything(0U);
    boot();
    record_event(K100_EVENT_USB_RESET);
    REQUIRE(event_at(0).kind == K100_EVENT_USB_RESET);
    REQUIRE(event_at(1).kind == K100_EVENT_BOOT);

    const uint16_t sequence = event_at(0).sequence;

    boot();
    REQUIRE(event_at(0).kind == K100_EVENT_BOOT);
    REQUIRE(event_at(0).sequence == sequence + 1U);
    REQUIRE(event_at(1).kind == K100_EVENT_USB_RESET);
}

static void test_usb_states_become_events(void) {
    erase_everything(0U);

    /* QMK reports its first state before the log starts, and that is no event. */
    usb_state_queued = USB_DEVICE_STATE_NO_INIT;
    log_writable     = false;
    notify_usb_device_state_change_kb((struct usb_device_state){.configure_state = USB_DEVICE_STATE_INIT});
    const usb_configure_state_t queued_before_start = usb_state_queued;
    boot();
    usb_state_queued = queued_before_start;
    k100_log_task();
    REQUIRE(event_at(0).kind == K100_EVENT_BOOT);

    static const usb_configure_state_t states[] = {
        USB_DEVICE_STATE_CONFIGURED, USB_DEVICE_STATE_CONFIGURED, USB_DEVICE_STATE_SUSPEND,
        USB_DEVICE_STATE_CONFIGURED, USB_DEVICE_STATE_INIT,
    };
    static const enum k100_log_event_kind expected[] = {
        K100_EVENT_USB_CONFIGURED, K100_EVENT_USB_SUSPEND, K100_EVENT_USB_WAKEUP, K100_EVENT_USB_RESET,
    };

    for (size_t state = 0; state < sizeof states / sizeof states[0]; state++) {
        notify_usb_device_state_change_kb((struct usb_device_state){.configure_state = states[state]});
    }

    /* Queued in the hook and written by the task, in order, with nothing between them lost. */
    const uint16_t sequence = event_at(0).sequence;
    REQUIRE(usb_state_count == 4U);
    k100_log_task();
    REQUIRE(usb_state_count == 0U);
    for (uint8_t age = 0; age < 4U; age++) {
        REQUIRE(event_at(age).kind == expected[3U - age]);
        REQUIRE(event_at(age).sequence == sequence + 4U - age);
    }
}

static void test_host_presence_becomes_events(void) {
    erase_everything(0U);
    boot();

    k100_log_task();
    REQUIRE(event_at(0).kind == K100_EVENT_BOOT);

    host_gone = true;
    k100_log_task();
    k100_log_task();
    REQUIRE(event_at(0).kind == K100_EVENT_HOST_GONE);
    REQUIRE(event_at(1).kind == K100_EVENT_BOOT);

    host_gone = false;
    k100_log_task();
    REQUIRE(event_at(0).kind == K100_EVENT_HOST_BACK);
    REQUIRE(event_at(1).kind == K100_EVENT_HOST_GONE);
}

static void test_budget(void) {
    erase_everything(0U);
    boot();

    /* The interval is what the stated decision and the rated cycles leave. */
    REQUIRE(WRITE_EARNED_MS == 420480U);

    /* A storm is bounded, and what it could not write goes into the next entry. */
    const uint32_t before = total_programs;
    for (int event = 0; event < 100000; event++) {
        now_ms += (event % 1000 == 0) ? 1U : 0U;
        k100_log_event(K100_EVENT_USB_RESET);
    }
    REQUIRE(total_programs - before <= EVENT_WORDS * (WRITES_IN_A_BURST - 1U));

    now_ms = budget_counted_at + WRITE_EARNED_MS - 1U;
    k100_log_event(K100_EVENT_USB_RESET);
    REQUIRE(event_at(0).kind == K100_EVENT_USB_RESET);
    REQUIRE(event_at(0).dropped == 0U);

    now_ms = budget_counted_at + WRITE_EARNED_MS;
    k100_log_event(K100_EVENT_HOST_GONE);
    REQUIRE(event_at(0).kind == K100_EVENT_HOST_GONE);
    REQUIRE(event_at(0).dropped == LOG_DROPPED_MAX);

    /* The next one has nothing to report, and the one after it no budget. */
    now_ms = budget_counted_at + WRITE_EARNED_MS;
    k100_log_event(K100_EVENT_HOST_BACK);
    REQUIRE(event_at(0).kind == K100_EVENT_HOST_BACK);
    REQUIRE(event_at(0).dropped == 0U);
    k100_log_event(K100_EVENT_USB_RESET);
    REQUIRE(event_at(0).kind == K100_EVENT_HOST_BACK);

    /* Refilled no further than the burst, however long it waited. */
    now_ms += 100U * WRITES_IN_A_BURST * WRITE_EARNED_MS;
    const uint32_t after_wait = total_programs;
    for (uint32_t event = 0; event < 2U * WRITES_IN_A_BURST; event++) {
        k100_log_event(K100_EVENT_USB_RESET);
    }
    REQUIRE(total_programs - after_wait <= EVENT_WORDS * WRITES_IN_A_BURST);
}

static void test_ring_and_sequence_wrap(void) {
    erase_everything(0U);
    memset(programs, 0, sizeof programs);
    boot();

    /* Past the ten bits of the sequence several times, checking the search on the way. */
    for (int event = 0; event < 3000; event++) {
        record_event(event % 2 == 0 ? K100_EVENT_USB_SUSPEND : K100_EVENT_USB_WAKEUP);

        const log_position_t found = log_ring_next(&event_ring);
        REQUIRE(found.slot == event_position.slot);
        REQUIRE(found.sequence == event_position.sequence);
        REQUIRE(event_at(0).sequence == ((event_position.sequence - 1U) & LOG_SEQUENCE_MASK));
    }

    /* Each slot written as often as any other. */
    uint32_t most = 0, least = UINT32_MAX;
    for (uint32_t word = LOG_FIRST_WORD; word < LOG_FIRST_WORD + K100_LOG_EVENT_SLOTS * EVENT_WORDS; word++) {
        most  = programs[word] > most ? programs[word] : most;
        least = programs[word] < least ? programs[word] : least;
    }
    REQUIRE(most - least <= 2U);

    /* The positions behind the newest are in order, all the way round. */
    for (uint8_t age = 1; age < K100_LOG_EVENT_SLOTS; age++) {
        REQUIRE(event_at(age).sequence == ((event_at(age - 1U).sequence - 1U) & LOG_SEQUENCE_MASK));
    }
}

static void test_write_cut_short_reads_as_empty(void) {
    /* An event, at each word it can be cut after. */
    for (uint32_t written = 0; written < EVENT_WORDS; written++) {
        erase_everything(0U);
        boot();
        record_event(K100_EVENT_USB_RESET);

        const uint16_t sequence = event_at(0).sequence;
        const uint16_t slot     = event_position.slot;

        writes_until_power_cut = written;
        record_event(K100_EVENT_USB_SUSPEND);
        REQUIRE(event_is_empty(0));

        /* Cut short, the slot is the one used next, and the entry before it is the newest. */
        boot();
        REQUIRE(event_at(0).kind == K100_EVENT_BOOT);
        REQUIRE(event_at(0).sequence == sequence + 1U);
        REQUIRE(event_at(1).kind == K100_EVENT_USB_RESET);
        REQUIRE(((event_position.slot - 1U) % K100_LOG_EVENT_SLOTS) == slot);
    }

    /* A crash, the same. */
    for (uint32_t written = 0; written < CRASH_WORDS; written++) {
        erase_everything(0U);
        boot();
        last_pass_ms = 1234U; /* A word of zero over an empty slot would not be a write to cut. */
        lpc_crash_t first = crash_with_pc(LPC_CRASH_STALL, 0x1000U);
        lpcCrashReport(&first);

        writes_until_power_cut = written;
        lpc_crash_t second     = crash_with_pc(LPC_CRASH_HARD, 0x2000U);
        lpcCrashReport(&second);
        writes_until_power_cut = NO_POWER_CUT;

        k100_log_crash_t crash;
        REQUIRE(k100_log_read_crash(0, &crash));
        REQUIRE(crash.pc == 0x1000U);
        REQUIRE(log_ring_next(&crash_ring).slot == 1U);
    }
}

static void test_crash_ring_and_link(void) {
    erase_everything(0U);
    boot();
    record_event(K100_EVENT_USB_RESET);

    last_pass_ms = 123456U;
    for (uint32_t number = 0; number < K100_LOG_CRASH_SLOTS + 3U; number++) {
        const lpc_crash_t report = crash_with_pc(number % 2U == 0U ? LPC_CRASH_STALL : LPC_CRASH_HARD, 0x10000U + number);
        lpcCrashReport(&report);
    }

    k100_log_crash_t crash;
    REQUIRE(k100_log_read_crash(0, &crash));
    REQUIRE(crash.cause == LPC_CRASH_STALL);
    REQUIRE(crash.pc == 0x10000U + K100_LOG_CRASH_SLOTS + 2U);
    REQUIRE(crash.lr == crash.pc + 4U);
    REQUIRE(crash.basepri == 0x60U);
    REQUIRE(crash.time_ms == 123456U);
    REQUIRE(crash.after_sequence == event_at(0).sequence);

    /* The ring holds only the newest crashes, and events never push one out. */
    REQUIRE(k100_log_read_crash(K100_LOG_CRASH_SLOTS - 1U, &crash));
    REQUIRE(crash.pc == 0x10003U);
    for (uint32_t event = 0; event < K100_LOG_EVENT_SLOTS - 1U; event++) {
        record_event(K100_EVENT_USB_SUSPEND);
    }
    REQUIRE(k100_log_read_crash(0, &crash));
    REQUIRE(crash.after_sequence != K100_LOG_NO_EVENT);

    /* Its event overwritten, a crash links to nothing. */
    record_event(K100_EVENT_USB_SUSPEND);
    record_event(K100_EVENT_USB_SUSPEND);
    REQUIRE(k100_log_read_crash(0, &crash));
    REQUIRE(crash.after_sequence == K100_LOG_NO_EVENT);

    /* And stays so when the ten bits come round to the same number. */
    for (uint32_t event = 0; event < LOG_SEQUENCE_MASK + 1U; event++) {
        record_event(K100_EVENT_USB_WAKEUP);
        REQUIRE(k100_log_read_crash(0, &crash));
        REQUIRE(crash.after_sequence == K100_LOG_NO_EVENT);
    }
}

static void test_clear(void) {
    erase_everything(0U);
    boot();
    for (int event = 0; event < 20; event++) {
        record_event(K100_EVENT_USB_SUSPEND);
    }
    lpc_crash_t report = crash_with_pc(LPC_CRASH_STALL, 0x1000U);
    lpcCrashReport(&report);

    /* A slot per pass at most, with events counted meanwhile. */
    k100_log_clear();
    REQUIRE(k100_log_clear_is_running());
    const uint32_t before = total_programs;
    k100_log_task();
    REQUIRE(total_programs - before == 1U);
    record_event(K100_EVENT_USB_RESET);
    REQUIRE(total_programs - before == 1U);
    while (k100_log_clear_is_running()) {
        const uint32_t pass = total_programs;
        k100_log_task();
        REQUIRE(total_programs - pass <= 1U);
    }

    for (uint8_t age = 0; age < K100_LOG_EVENT_SLOTS; age++) {
        REQUIRE(event_is_empty(age));
    }
    k100_log_crash_t crash;
    REQUIRE(!k100_log_read_crash(0, &crash));

    /* Writing goes on, and the count of what was dropped arrives with it. */
    record_event(K100_EVENT_HOST_BACK);
    REQUIRE(event_at(0).kind == K100_EVENT_HOST_BACK);
    REQUIRE(event_at(0).dropped == 1U);

    /* Cleared and booted, the log has only what came after. */
    const uint16_t sequence = event_at(0).sequence;
    boot();
    REQUIRE(event_at(0).kind == K100_EVENT_BOOT);
    REQUIRE(event_at(0).sequence == sequence + 1U);
    REQUIRE(event_at(1).kind == K100_EVENT_HOST_BACK);
    REQUIRE(event_is_empty(2));
}

static void test_clear_interrupted_by_a_reset(void) {
    erase_everything(0U);
    boot();
    for (int event = 0; event < 20; event++) {
        record_event(K100_EVENT_USB_SUSPEND);
    }
    const uint16_t sequence = event_at(0).sequence;

    /* The oldest slots go first, so what is left is still the newest part, still in order. */
    k100_log_clear();
    for (int pass = 0; pass < 5; pass++) {
        k100_log_task();
    }
    boot();
    REQUIRE(event_at(0).kind == K100_EVENT_BOOT);
    REQUIRE(event_at(0).sequence == sequence + 1U);
    REQUIRE(event_at(1).sequence == sequence);
    REQUIRE(event_at(16).sequence == sequence - 15U);
    REQUIRE(event_is_empty(17));
}

static void test_failed_write_stops_writing(void) {
    erase_everything(0U);
    boot();
    const uint32_t before = total_programs;

    writes_fail = true;
    record_event(K100_EVENT_USB_RESET);
    REQUIRE(!log_writable);
    writes_fail = false;

    record_event(K100_EVENT_USB_RESET);
    lpc_crash_t report = crash_with_pc(LPC_CRASH_STALL, 0x1000U);
    lpcCrashReport(&report);
    REQUIRE(total_programs == before);
}

static void test_labels_fit(void) {
    uint8_t value[29];

    erase_everything(0U);
    boot();

    k100_via_last_crash_get(value);
    REQUIRE(strcmp((const char *)value, "none") == 0);

    last_pass_ms = UINT32_MAX;
    lpc_crash_t report = crash_with_pc(LPC_CRASH_STALL, 0x12A3CU);
    lpcCrashReport(&report);
    memset(value, 0xAA, sizeof value);
    k100_via_last_crash_get(value);
    REQUIRE(strcmp((const char *)value, "stall pc 00012a3c +4294967 s") == 0);

    now_ms = UINT32_MAX;
    k100_log_event(K100_EVENT_BOOTLOADER_JUMP);
    memset(value, 0xAA, sizeof value);
    k100_via_last_event_get(value);
    REQUIRE(strcmp((const char *)value, "bootloader jump +4294967 s") == 0);
}

/* Replies, in the 32 bytes the host reads. */
static void print_reply(const char *ring, void (*get)(uint8_t *), uint8_t position) {
    uint8_t reply[32] = {0};

    reply[0] = 0x08;
    reply[2] = 0;
    reply[3] = position;
    get(&reply[3]);

    printf("%s %u ", ring, position);
    for (size_t byte = 0; byte < sizeof reply; byte++) {
        printf("%02x", reply[byte]);
    }
    printf("\n");
}

static void print_label(const char *what, unsigned code, void (*get)(uint8_t *)) {
    uint8_t value[29] = {0};

    get(value);
    printf("label %s %u %s\n", what, code, (const char *)value);
}

static void print_replies(void) {
    static const enum k100_log_event_kind kinds[] = {
        K100_EVENT_BOOT, K100_EVENT_USB_RESET, K100_EVENT_USB_CONFIGURED, K100_EVENT_USB_SUSPEND,
        K100_EVENT_USB_WAKEUP, K100_EVENT_HOST_GONE, K100_EVENT_HOST_BACK, K100_EVENT_BOOTLOADER_JUMP,
    };
    static const lpc_crash_cause_t causes[] = {
        LPC_CRASH_HARD, LPC_CRASH_MEM, LPC_CRASH_BUS, LPC_CRASH_USAGE, LPC_CRASH_STALL,
    };

    erase_everything(0U);
    boot();
    for (size_t kind = 0; kind < sizeof kinds / sizeof kinds[0]; kind++) {
        now_ms = 1000U * (kind + 1U);
        k100_log_event(kinds[kind]);
        print_label("event", kinds[kind], k100_via_last_event_get);
    }

    /* The crash in the middle of the event ring, and one whose event is gone at the end. */
    last_pass_ms = 8500U;
    for (size_t cause = 0; cause < sizeof causes / sizeof causes[0]; cause++) {
        const lpc_crash_t report = crash_with_pc(causes[cause], 0x114D8U + cause);
        lpcCrashReport(&report);
        print_label("crash", causes[cause], k100_via_last_crash_get);
    }

    now_ms = 9000U;
    k100_log_event(K100_EVENT_USB_RESET);

    for (unsigned position = 0; position <= K100_LOG_EVENT_SLOTS; position++) {
        print_reply("event", k100_via_log_event_get, (uint8_t)position);
    }
    for (unsigned position = 0; position <= K100_LOG_CRASH_SLOTS; position++) {
        print_reply("crash", k100_via_log_crash_get, (uint8_t)position);
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--replies") == 0) {
        print_replies();
        return 0;
    }

    test_empty_storage();
    test_order_survives_a_boot();
    test_usb_states_become_events();
    test_host_presence_becomes_events();
    test_budget();
    test_ring_and_sequence_wrap();
    test_write_cut_short_reads_as_empty();
    test_crash_ring_and_link();
    test_clear();
    test_clear_interrupted_by_a_reset();
    test_failed_write_stops_writing();
    test_labels_fit();

    puts("ok: the fault log keeps its order through wraps, reboots, power cuts, storms and clears");
    return 0;
}
