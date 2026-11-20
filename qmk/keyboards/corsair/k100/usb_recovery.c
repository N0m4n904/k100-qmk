// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * When a firmware that runs but never became usable on USB hands the keyboard to the bootloader.
 * bootloader.c carries out the handover; docs/recovery.md has the history.
 */

#include "usb_recovery.h"

/*
 * How long a host may send frames without configuring the keyboard before this
 * firmware counts as unusable.
 *
 * Generous on purpose. A host that is slow to enumerate, or a hub that resets
 * the port, must not be mistaken for broken firmware: a false trigger leaves
 * the user holding Corsair's bootloader, which is recoverable but is also the
 * exact failure this path exists to prevent. The clock starts at the first
 * frame, so what is left to allow for is a host that starts talking and then
 * takes its time. Thirty seconds costs nothing on the recovery path and is far
 * outside anything an enumeration does.
 */
#ifndef K100_USB_WATCHDOG_MS
#    define K100_USB_WATCHDOG_MS 30000U
#endif

/*
 * The countdown starts at the first frame a host sends, not when this firmware
 * boots, and runs on from there.
 *
 * Measured on a cold boot: the motherboard powers the keyboard long before the
 * operating system reaches its USB stack, and a timeout counted from boot
 * expired in between. The host's first sight of the keyboard was then the
 * bootloader -- `bcdDevice 0.0a` at the first enumeration in the kernel log --
 * and it stayed there.
 *
 * Frames on the bus are what separate the two cases the device cannot otherwise
 * tell apart, because both look like "no USB yet" from here. A host that has
 * sent frames was present and talking, so failing to become configured after
 * that is this firmware's fault and worth acting on -- also once the host gives
 * up and the frames stop. A bus with nothing on it is not a fault at all, and
 * handing the keyboard to the bootloader there achieves nothing anyway, since
 * the bootloader's whole purpose is to be reflashed by a host that has not
 * arrived. So a firmware that never attaches to the bus is not caught here:
 * Escape held at power-on is the way back for that one.
 *
 * Only a configuration ends it. QMK reports a suspend whether or not a host
 * ever configured the keyboard, and an idle bus reports one 79 ms after
 * power-up.
 */
bool k100_usb_recovery_is_due(k100_usb_recovery_t *recovery, enum k100_host_presence presence,
                              bool configured, uint32_t now_ms) {
    recovery->configured = recovery->configured || configured;

    if (recovery->configured) {
        return false;
    }

    if (recovery->first_frame_at == 0U) {
        if (presence == K100_HOST_SENDING) {
            /* A clock that reads zero is one tick late here, which the timeout cannot notice. */
            recovery->first_frame_at = now_ms | 1U;
        }
        return false;
    }

    return now_ms - recovery->first_frame_at > K100_USB_WATCHDOG_MS;
}
