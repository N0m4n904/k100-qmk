// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Proving the fault log on the keyboard, with nobody unplugging anything.
 *
 * Built only with K100_DIAGNOSTIC_HANG=yes, because a keyboard should not stop when a host asks it
 * to. `tools/k100-fault-log.py --hang` then stops the main loop. The watchdog warning has to find
 * the core inside one of these three functions, whichever was requested, and the keyboard has to
 * end up in the bootloader afterwards. They are separate functions and never inlined, so that a PC
 * in the log can only mean the one requested. docs/qmk-layout.md has the procedure.
 *
 * It runs on the next pass through the main loop, so the command is answered first.
 */

#include "hal.h"

#include "fault_log.h"

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

void k100_diagnostic_hang_task(void) {
    switch (requested_hang) {
        case K100_HANG_UNLOCKED:
            hang_with_kernel_unlocked();
            break;
        case K100_HANG_LOCKED:
            hang_with_kernel_locked();
            break;
        case K100_HANG_FAULT:
            fault_on_purpose();
            break;
        default:
            break;
    }
}
