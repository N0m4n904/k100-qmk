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
 * Requires chibios/patches/0002-optional-dwt-enable.patch.
 */
#define CORTEX_ENABLE_DWT FALSE

/*
 * ...and because of that, the realtime counter has to come from somewhere else.
 *
 * The line above was first written with a note saying it cost nothing, because
 * chSysPolledDelayX() was the only thing built on the DWT and nothing here
 * called it. That was wrong, and wrong in the direction that hides itself: on
 * this platform QMK's wait_us() *is* chSysPolledDelayX(), so every microsecond
 * delay in the firmware was reading a counter that never advanced. Such a delay
 * does not run short, it never returns -- the function spins until the counter
 * passes a value it can no longer reach. The first wait_us() in the boot path is
 * the settling delay of the first matrix read, which runs before USB is started,
 * so the keyboard hung there and the watchdog reset it a second later. For
 * several rounds of debugging that looked exactly like a crash in USB
 * initialisation, and the search went looking for it in the wrong driver.
 *
 * So the counter is supplied instead by the RIT, a free-running AHB counter
 * ticking at the same rate CYCCNT would. Requires
 * chibios/patches/0007-external-realtime-counter.patch, which routes
 * port_rt_get_counter_value() to it; see lpc546xx_rit.c for why that peripheral
 * and what it has to be told.
 */
#define PORT_USE_EXTERNAL_RT_COUNTER

#include_next <chconf.h>
