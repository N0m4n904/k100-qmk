// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lpc546xx_crash.h"

/*
 * What happened before the keyboard stopped. docs/qmk-layout.md has the design.
 */

/**
 * @brief   What the log records as an event.
 *
 * @details These numbers are stored and read by tools/k100-fault-log.py, so they do not move and
 *          zero is not one of them.
 */
enum k100_log_event_kind {
    K100_EVENT_BOOT            = 1,
    /** The host reset the bus, or took the configuration away. */
    K100_EVENT_USB_RESET       = 2,
    K100_EVENT_USB_CONFIGURED  = 3,
    K100_EVENT_USB_SUSPEND     = 4,
    K100_EVENT_USB_WAKEUP      = 5,
    /** The frames stopped, as lighting_sleep.c reads them, which a suspend may never report. */
    K100_EVENT_HOST_GONE       = 6,
    K100_EVENT_HOST_BACK       = 7,
    /** This firmware handed the keyboard to the bootloader on request. */
    K100_EVENT_BOOTLOADER_JUMP = 8,
};

/** How many positions each ring has. A request names a position in one byte. */
#define K100_LOG_EVENT_SLOTS 240U
#define K100_LOG_CRASH_SLOTS 8U

/** A stored event, with the code converted. */
typedef struct {
    enum k100_log_event_kind kind;
    uint8_t                  dropped;
    uint16_t                 sequence;
    uint32_t                 time_ms;
} k100_log_event_t;

/** @p after_sequence of a crash whose event has been overwritten. */
#define K100_LOG_NO_EVENT 0xFFFFU

/** A stored crash, with the code converted. */
typedef struct {
    lpc_crash_cause_t cause;
    uint16_t          after_sequence;
    uint8_t           basepri;
    uint32_t          time_ms;
    uint32_t          pc;
    uint32_t          lr;
    uint32_t          psr;
    uint32_t          cfsr;
    uint32_t          fault_address;
} k100_log_crash_t;

/**
 * @brief   Finds where the log left off and starts watching for a crash.
 *
 * @details Needs the system clock to be the real one, for the EEPROM divider, and the kernel to be
 *          running. Until this has run, events and crashes are not recorded.
 */
void k100_log_start(void);

/** @brief Records an event, if the write budget allows. From the main thread only. */
void k100_log_event(enum k100_log_event_kind kind);

/** @brief The log's work in one pass of the main loop. */
void k100_log_task(void);

/** @brief Asks for the log to be emptied, one slot per pass through the main loop. */
void k100_log_clear(void);

/** @brief Whether a clear asked for has not finished yet. */
bool k100_log_clear_is_running(void);

/** @brief The entry @p age places behind the newest. @return @p false when it is empty. */
bool k100_log_read_event(uint8_t age, k100_log_event_t *event);
bool k100_log_read_crash(uint8_t age, k100_log_crash_t *crash);

/** What `tools/k100-fault-log.py --hang` can request, in a build with K100_DIAGNOSTIC_HANG. */
enum k100_hang_how {
    K100_HANG_UNLOCKED = 1,
    K100_HANG_LOCKED   = 2,
    K100_HANG_FAULT    = 3,
};

/** @brief Stops the main loop on its next pass. See fault_diagnostic_hang.c. */
void k100_diagnostic_request_hang(uint8_t how);
void k100_diagnostic_hang_task(void);

/** @brief Reports a fault by the length of a marker; see bootloader.c. */
void k100_diagnostic_fault(lpc_crash_cause_t cause);
