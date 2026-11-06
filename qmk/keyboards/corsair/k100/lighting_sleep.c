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
/* For USB_DRIVER, which names the driver instance rather than assuming USBD1,
   and for the HAL accessor that reads its frame number. */
#include "usb_main.h"
#include "hal.h"

#include "k100.h"

/** One minute, in the milliseconds last_input_activity_elapsed() counts. */
#define K100_MINUTE_MS 60000UL

/**
 * @brief   How long the frames must stop before the host counts as gone.
 *
 * @details How long silence has to last before it is believed, and the floor is
 *          set by silences that are not a host leaving. A bus reset is driven
 *          for at least ten milliseconds and no frame crosses it, so a
 *          threshold of a few would put the lighting out every time a host
 *          re-enumerates the keyboard. USB's own rule is the shortest anything
 *          can be: three milliseconds of an idle bus is a suspend.
 *
 *          A slow trip round the loop cannot be mistaken for silence, which is
 *          the thing to worry about on a board whose matrix alone costs four
 *          milliseconds a scan. The deadline below is only consulted when two
 *          samples read the *same* frame number, and a trip that took longer
 *          finds the counter further on, not still.
 *
 *          What bounds it from above is the counter's wrap: eleven bits come
 *          round every 2048 ms, so two samples exactly that far apart would
 *          read alike and silence would be read into a running bus. That cannot
 *          happen here -- the trip that takes this sample is also the trip that
 *          feeds the inherited watchdog, and about a second and a half without
 *          one resets the part. See bootloader.c.
 */
#define K100_HOST_GONE_MS 250U

/**
 * @brief   How long the lighting stays on while no host has ever answered.
 *
 * @details Without a debug probe, 190 lit LEDs are this board's only sign that
 *          the firmware is running at all: a controller that never connects
 *          produces no observable, which is why bootloader.c carries a whole
 *          diagnostic channel made of how long the keyboard stays off the bus.
 *          A keyboard that came up dark and stayed dark would look like a dead
 *          one, so the lighting is given long enough to be seen and goes out if
 *          nothing ever answers. A working image is back on the bus in under a
 *          second.
 */
#define K100_HOST_GRACE_MS 5000U

static bool resting;
static bool host_is_gone;

/**
 * @brief   Whether the host has stopped using the keyboard, read off the bus.
 *
 * @details Not asked of QMK's device state, which is where this started and
 *          what it got wrong. That state reports `USB_DEVICE_STATE_SUSPEND` at
 *          the end of a chain -- the controller latches a suspend, the
 *          interrupt for it is taken, the ChibiOS driver raises an event, QMK
 *          drains an event queue into a state -- and a host that goes away does
 *          not always set that chain off. A host switched off while its port
 *          stays powered, which is what a self-powered hub does, leaves the
 *          keyboard running with the state never reaching SUSPEND. Which of the
 *          four links fails to deliver was never measured, and this does not
 *          have to know.
 *
 *          A host that is there sends a frame every millisecond, and the
 *          controller counts them in `INFO.FRAME_NR` by itself, without an
 *          interrupt to enable or anything to acknowledge. That matters here:
 *          QMK registers no SOF callback, so the counter is the only sight this
 *          firmware has of the traffic. A frame number that has stopped moving
 *          is the one fact every way of leaving has in common -- asleep,
 *          switched off, unplugged, or a hub that disabled the port -- and the
 *          controller's own suspend bit is derived from the same silence.
 *
 *          The register was watched doing exactly this during the USB
 *          bring-up: counting under Corsair's firmware, and standing at zero
 *          when the controller never saw the bus. See the table in
 *          docs/usb-bringup.md.
 *
 *          `usbGetFrameNumberX()` is plain ChibiOS HAL, implemented by the LPC
 *          driver, so nothing here touches a register.
 */
static bool host_gone(void) {
    static uint32_t frame;
    static uint32_t moved_at;
    static bool     seen;

    const uint32_t now = usbGetFrameNumberX(&USB_DRIVER);

    if (now != frame) {
        frame    = now;
        moved_at = timer_read32();
        seen     = true;
    }

    /* Until the first frame, `moved_at` is still zero and the elapsed time is
       therefore the time since the clock started, which is the grace above
       measured from boot. */
    return timer_elapsed32(moved_at) > (seen ? K100_HOST_GONE_MS : K100_HOST_GRACE_MS);
}

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

bool k100_host_is_gone(void) {
    return host_is_gone;
}

void k100_lighting_sleep_task(void) {
#ifdef RGB_MATRIX_ENABLE
    /* Sampled on its own line and every trip, because host_gone() is what reads
       the counter: behind a `||` it would stop being read as soon as the other
       reason was true, and the clock it keeps would be the one that suffered. */
    host_is_gone      = host_gone();
    const bool wanted = host_is_gone || left_alone();

    /*
     * Only on a change. Turning it on renders a black frame and flushes it,
     * which is work, and the lighting task stops flushing afterwards -- so
     * saying it every millisecond would undo the saving it exists for.
     */
    if (wanted != resting) {
        rgb_matrix_set_suspend_state(wanted);
        resting = wanted;
    }
#endif
}
