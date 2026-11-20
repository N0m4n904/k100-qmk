// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * When the lighting goes dark by itself.
 *
 * Two reasons, and one mechanism, because they are the same statement: nothing
 * is using the lighting, so stop drawing it.
 *
 *   the host is gone       190 LEDs lit through the night, in a dark room, for
 *                          a computer that is asleep or switched off
 *   nobody is here         the same thing while somebody is away from the desk,
 *                          for whoever would rather it stopped
 *
 * The first is not a preference and has no setting. The second is one, and it is
 * off until somebody asks for it: a keyboard that goes dark on its own without
 * being told to looks broken, and "it turned itself off" is a bug report.
 *
 * **Why this is a file and not a define.** QMK already has both: a
 * suspend_power_down_kb() hook and an RGB_MATRIX_TIMEOUT. Neither is reachable
 * here.
 *
 * The hook is called only from the loop in protocol_pre_task() that spins while
 * the host is suspended, and that loop is compiled out on this keyboard by
 * NO_USB_STARTUP_CHECK -- for a reason that is not negotiable: it never reaches
 * housekeeping_task_kb(), which is where the watchdog this firmware inherited
 * from Corsair's bootloader is fed, and where the recovery that hands a keyboard
 * back to that bootloader lives. A keyboard that slept through its own watchdog
 * would reset instead of resting. See config.h.
 *
 * And RGB_MATRIX_TIMEOUT is a compile-time constant, which cannot be a setting.
 *
 * So the decision is made here, once per trip round the main loop, where the
 * watchdog keeps being fed either way.
 */

#include "quantum.h"
#include "k100.h"

/** One minute, in the milliseconds last_input_activity_elapsed() counts. */
#define K100_MINUTE_MS 60000UL

static bool resting;

/**
 * @brief   Whether the keyboard has been left alone for as long as was asked.
 *
 * @details Keys *and* both rotaries: QMK's own idea of input activity covers the
 *          encoders, which matters on a keyboard whose wheel sets the
 *          brightness. Turning it up and watching the lighting go out a minute
 *          later would be the kind of correct-but-useless behaviour nobody
 *          forgives.
 */
static bool left_alone(void) {
    const uint8_t minutes = k100_sleep_after();

    return minutes != 0 &&
           last_input_activity_elapsed() > (uint32_t)minutes * K100_MINUTE_MS;
}

void k100_lighting_sleep_task(enum k100_host_presence presence) {
#ifdef RGB_MATRIX_ENABLE
    const bool wanted = presence == K100_HOST_GONE || left_alone();

    /*
     * Only on a change. Turning it on renders a black frame and flushes it,
     * which is work, and the lighting task stops flushing afterwards -- so
     * saying it every millisecond would undo the saving it exists for.
     */
    if (wanted != resting) {
        rgb_matrix_set_suspend_state(wanted);
        resting = wanted;
    }
#else
    (void)presence;
#endif
}
