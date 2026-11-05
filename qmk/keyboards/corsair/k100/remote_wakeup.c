// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Touching the keyboard wakes a sleeping host.
 *
 * The device end of it was always there: QMK's configuration descriptor
 * declares `USB_CONFIG_ATTR_REMOTEWAKEUP`, so a host that wants the feature
 * enables it with SET_FEATURE and ChibiOS records that in its driver. What was
 * missing is the keyboard ever sending the request.
 *
 * **Why this is a file and not a define.** QMK sends it from exactly one
 * place: the loop in protocol_pre_task() that spins while the host is
 * suspended, which this keyboard compiles out with NO_USB_STARTUP_CHECK --
 * nothing on that path reaches housekeeping_task_kb(), where the watchdog
 * inherited from Corsair's bootloader is fed and where the recovery that hands
 * a keyboard back to that bootloader lives. See config.h. The same reason put
 * lighting_sleep.c in the main loop, and this is its other half: that file
 * stops drawing when the host goes, this one wakes it.
 *
 * **What QMK does there, and what is left out of it.** That loop scans the
 * matrix itself through `suspend_wakeup_condition()` and repairs its idea of
 * what is held down afterwards, because while it spins nothing else scans. Here
 * the main loop is running: the matrix is scanned, debounced and reported as
 * usual, and the key that wakes the host is also typed on it, like on any other
 * keyboard. So all that is left is the call.
 *
 * **Whether the host is asleep is not decided here.** `usbWakeupHost()` signals
 * resume from a driver that is suspended and does nothing otherwise, and that
 * is the right layer to settle it in: a host that is plainly gone and never
 * reaches that state is a port that misses the suspend, to be found and fixed
 * where the suspend is read. Which is why this is the one thing in the keyboard
 * that still depends on that chain, where the lighting no longer does.
 */

#include "quantum.h"
/* For USB_DRIVER, and for usbWakeupHost(), which is the request. */
#include "usb_main.h"
#include "hal.h"

#include "k100.h"

/**
 * @brief   The bit that says the host has allowed the keyboard to wake it.
 *
 * @details Bit one of the device status word, from USB 2.0 section 9.4.5 and
 *          its figure 9-4, which ChibiOS stores in @p USBDriver::status: set
 *          from SET_FEATURE(DEVICE_REMOTE_WAKEUP), cleared by CLEAR_FEATURE and
 *          by every bus reset. A device that signals resume without it is doing
 *          something it was not permitted to do, so the bit is read first.
 *
 *          Spelled out rather than taken from a header because no header in
 *          scope has it: ChibiOS names only the feature selector and writes the
 *          bit as a bare 2, and QMK's own constant is private to the
 *          translation unit holding the loop this keyboard does not compile.
 */
#define USB_STATUS_REMOTE_WAKEUP 2U

void k100_remote_wakeup_task(void) {
    /** The input already acted on, so one press sends one request. */
    static uint32_t answered;

    /*
     * Keys and both rotaries, the same clock the idle timeout in
     * lighting_sleep.c reads: anything deliberate enough to be meant. The
     * timestamp rather than the time since, because what matters is that it
     * moved -- "touched within the last so many milliseconds" would send the
     * request again on every trip round the loop for as long as that lasted.
     */
    const uint32_t activity = last_input_activity_time();

    if (activity == answered) {
        return;
    }
    answered = activity;

    if (USB_DRIVER.status & USB_STATUS_REMOTE_WAKEUP) {
        usbWakeupHost(&USB_DRIVER);
    }
}
