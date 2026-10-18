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

/*
 * Periodic system tick, not tickless.
 *
 * QMK defaults to CH_CFG_ST_TIMEDELTA 2, which asks ChibiOS for a free-running
 * timer with a comparator. The shared LPC system-timer driver cannot provide
 * one: it is SysTick, a 24-bit *down*-counter with no compare register. Its
 * free-running entry points reflect that -- st_lld_get_counter() returns
 * SysTick->VAL, which counts downwards and wraps at LOAD rather than increasing
 * monotonically, and st_lld_set_alarm() writes that same current-count register
 * as though it were a comparator.
 *
 * The driver knows it cannot do this and tries to say so, but its guard reads
 *
 *     #if OSAL_ST_MODE_PERIODIC == OSAL_ST_MODE_FREERUNNING
 *
 * which compares two distinct constants, 1 and 2, and is therefore never true.
 * So the #error never fires, the build succeeds, and what you get is a kernel
 * whose clock does not advance: every chThdSleep() blocks for ever.
 *
 * That is worth spelling out because of how it presents. Nothing faults, no
 * loop in any source file is unbounded, and the firmware runs perfectly until
 * the first sleep -- which, in a QMK boot, is inside init_usb_driver(). For
 * several days that made this look like a USB bug. wait_us() kept working
 * throughout, because it polls the RIT and never involves the scheduler.
 *
 * Periodic mode uses SysTick the way SysTick works, and the driver supports it.
 * chibios/patches/0008-systick-freerunning-guard.patch fixes the guard so the
 * next person is told instead of having to find this.
 */
#define CH_CFG_ST_TIMEDELTA 0

/* 100 us, which is the usual choice for a periodic tick. QMK converts through
   TIME_MS2I and friends, so it does not care what this is. */
#define CH_CFG_ST_FREQUENCY 10000

#include_next <chconf.h>
