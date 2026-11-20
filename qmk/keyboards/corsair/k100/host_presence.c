// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Whether a host is sending frames on the bus, read from the USB frame counter.
 */

#include "quantum.h"
/* For USB_DRIVER, which names the driver instance rather than assuming USBD1,
   and for the HAL accessor that reads its frame number. */
#include "usb_main.h"
#include "hal.h"

#include "host_presence.h"

/**
 * @brief   How long the frames must stop before the host counts as gone.
 *
 * @details How long silence has to last before it is believed, and the floor is
 *          set by silences that are not a host leaving. A bus reset is driven
 *          for at least ten milliseconds and no frame crosses it, so a
 *          threshold of a few would read every bus reset as the host leaving.
 *          USB's own rule is the shortest anything can be: three milliseconds
 *          of an idle bus is a suspend.
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
 * @brief   How long the bus may stay silent after power-up before the host counts as gone.
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

/**
 * @brief   What the frame counter shows about the host, read once per trip round the main loop.
 *
 * @details Not asked of QMK's device state. That state reports
 *          `USB_DEVICE_STATE_SUSPEND` at the end of a chain -- the controller
 *          latches a suspend, the interrupt for it is taken, the ChibiOS driver
 *          raises an event, QMK drains an event queue into a state -- and a host
 *          that goes away does not always set that chain off. A host switched
 *          off while its port stays powered, which is what a self-powered hub
 *          does, leaves the keyboard running with the state never reaching
 *          SUSPEND. Which of the four links fails to deliver was never measured,
 *          and this does not have to know.
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
enum k100_host_presence k100_host_presence_sample(void) {
    static uint32_t frame;
    static uint32_t moved_at;
    static bool     seen;

    const uint32_t now = usbGetFrameNumberX(&USB_DRIVER);

    if (now != frame) {
        frame    = now;
        moved_at = timer_read32();
        seen     = true;
    }

    if (!seen) {
        return timer_read32() > K100_HOST_GRACE_MS ? K100_HOST_GONE : K100_HOST_NOT_SEEN;
    }

    return timer_elapsed32(moved_at) > K100_HOST_GONE_MS ? K100_HOST_GONE : K100_HOST_SENDING;
}
