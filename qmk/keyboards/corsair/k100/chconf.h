// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

/*
 * The DWT cycle counter is not accessible on this keyboard, and ChibiOS's
 * port_init() enables it unconditionally without this.
 *
 * Corsair ships this part with debug access disabled, so the read-modify-write
 * of DWT->CTRL at 0xE0001000 faults. That happens inside chSysInit(), before
 * there is a kernel to report anything, so the whole symptom is a keyboard that
 * starts, runs for a second and a half, and is reset by the watchdog the
 * bootloader left armed -- indistinguishable, from the host, from a bootloader
 * that refuses to start the image at all.
 *
 * Located by putting exactly those two writes in board_init(), which is known to
 * run, with a measurable delay on each side of them. The delay before them
 * completed and the one after did not.
 *
 * Requires chibios/patches/0002-optional-dwt-enable.patch. What it costs is
 * chSysPolledDelayX(), which nothing here uses -- lpc546xx_usb.c has its own
 * loop precisely because that function is unusable this early in boot anyway.
 */
#define CORTEX_ENABLE_DWT FALSE

#include_next <chconf.h>
