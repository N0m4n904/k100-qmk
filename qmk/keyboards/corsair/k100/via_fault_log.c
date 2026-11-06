// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The fault log as VIA reads it: a line of text for each label, and entries for the tool.
 *
 * The names come from the enums the entries are stored with, lowercase and without the prefix, and
 * tools/k100-fault-log.py derives the same names from the same headers.
 */

#include <stdbool.h>

#include "fault_log.h"
#include "via_fault_log.h"
#include "via_text.h"

/*
 * The replies, all big-endian. The first byte of the reply is the position the request asked for,
 * and the second is the entry code or an enum k100_log_reply_status.
 *
 *     event    position:8 code:8 dropped:8 sequence:16 time:32
 *     crash    position:8 code:8 after:16 time:32 pc:32 lr:32 psr:32 basepri:8 cfsr:32 address:32
 */
#define VIA_VALUE_BYTES 29U
#define CRASH_REPLY_BYTES (1U + 1U + 2U + 4U + 4U + 4U + 4U + 1U + 4U + 4U)

_Static_assert(CRASH_REPLY_BYTES <= VIA_VALUE_BYTES, "a crash does not fit in a VIA reply");

static const char *event_name(enum k100_log_event_kind kind) {
    switch (kind) {
        case K100_EVENT_BOOT:
            return "boot";
        case K100_EVENT_USB_RESET:
            return "usb reset";
        case K100_EVENT_USB_CONFIGURED:
            return "usb configured";
        case K100_EVENT_USB_SUSPEND:
            return "usb suspend";
        case K100_EVENT_USB_WAKEUP:
            return "usb wakeup";
        case K100_EVENT_HOST_GONE:
            return "host gone";
        case K100_EVENT_HOST_BACK:
            return "host back";
        case K100_EVENT_BOOTLOADER_JUMP:
            return "bootloader jump";
    }
    return "?";
}

static const char *crash_name(lpc_crash_cause_t cause) {
    switch (cause) {
        case LPC_CRASH_STALL:
            return "stall";
        case LPC_CRASH_HARD:
            return "hard";
        case LPC_CRASH_MEM:
            return "mem";
        case LPC_CRASH_BUS:
            return "bus";
        case LPC_CRASH_USAGE:
            return "usage";
    }
    return "?";
}

/** " +1234 s", counting seconds since the most recent boot event before the entry. */
static uint8_t *put_seconds(uint8_t *at, uint32_t time_ms) {
    at = k100_put_text(at, " +");
    at = k100_put_number(at, time_ms / 1000U);

    return k100_put_text(at, " s");
}

/* "bootloader jump +4294967 s", at the 28 characters a VIA label shows. */
void k100_via_last_event_get(uint8_t *value_data) {
    k100_log_event_t event;
    uint8_t         *at = value_data;

    if (k100_log_read_event(0, &event)) {
        at = k100_put_text(at, event_name(event.kind));
        at = put_seconds(at, event.time_ms);
    } else {
        at = k100_put_text(at, "none");
    }

    *at = '\0';
}

/* "stall pc 00012a3c +4294967 s", at the same 28 characters. The tool prints the rest. */
void k100_via_last_crash_get(uint8_t *value_data) {
    k100_log_crash_t crash;
    uint8_t         *at = value_data;

    if (k100_log_read_crash(0, &crash)) {
        at = k100_put_text(at, crash_name(crash.cause));
        at = k100_put_text(at, " pc ");
        at = k100_put_hex(at, crash.pc);
        at = put_seconds(at, crash.time_ms);
    } else {
        at = k100_put_text(at, "none");
    }

    *at = '\0';
}

void k100_via_log_event_get(uint8_t *value_data) {
    const uint8_t    age = value_data[0];
    k100_log_event_t event;
    uint8_t         *at = &value_data[1];

    if (age >= K100_LOG_EVENT_SLOTS) {
        *at = K100_LOG_REPLY_PAST_END;
    } else if (!k100_log_read_event(age, &event)) {
        *at = K100_LOG_REPLY_EMPTY;
    } else {
        *at++ = (uint8_t)event.kind;
        *at++ = event.dropped;
        at    = k100_put_be16(at, event.sequence);
        (void)k100_put_be32(at, event.time_ms);
    }
}

void k100_via_log_crash_get(uint8_t *value_data) {
    const uint8_t    age = value_data[0];
    k100_log_crash_t crash;
    uint8_t         *at = &value_data[1];

    if (age >= K100_LOG_CRASH_SLOTS) {
        *at = K100_LOG_REPLY_PAST_END;
    } else if (!k100_log_read_crash(age, &crash)) {
        *at = K100_LOG_REPLY_EMPTY;
    } else {
        *at++ = (uint8_t)crash.cause;
        at    = k100_put_be16(at, crash.after_sequence);
        at    = k100_put_be32(at, crash.time_ms);
        at    = k100_put_be32(at, crash.pc);
        at    = k100_put_be32(at, crash.lr);
        at    = k100_put_be32(at, crash.psr);
        *at++ = crash.basepri;
        at    = k100_put_be32(at, crash.cfsr);
        (void)k100_put_be32(at, crash.fault_address);
    }
}
