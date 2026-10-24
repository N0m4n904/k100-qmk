// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The ring around the iCUE wheel, showing what the wheel is for.
 *
 * Eight lights in a circle around a knob that sets the lighting's brightness,
 * and until now they were decoration: a colour somebody picked, on all the time,
 * saying nothing. A knob with a scale drawn around it should show where on that
 * scale it is, so turning the wheel fills the ring like a gauge and it goes back
 * to whatever the lighting was doing a moment later.
 *
 * **The fill arrives by moving**, which is the difference between a gauge and a
 * lit ring. At the top of the range a full gauge *is* a lit ring, so a static
 * one says nothing at the setting most keyboards sit at; see
 * K100_WHEEL_SWEEP_MS.
 *
 * **Only the brightness, and the roller gets nothing.** The volume is the host's,
 * and a keyboard is never told what it is: `encoders.c` taps KC_VOLU and KC_VOLD,
 * which asks for a change and is not an answer. A ring filled to a level this
 * firmware worked out by counting its own requests would be showing what it last
 * asked for rather than what is so, which is the same reason the mute icon above
 * the keys stays dark. The brightness, by contrast, is the keyboard's own number
 * and is read back from it.
 *
 * **Not scaled by the brightness it is showing**, which is the one thing here
 * that would be wrong to do by habit: every other colour on this keyboard is
 * dimmed by the wheel, and a gauge dimmed by the quantity it measures is
 * unreadable exactly when somebody is turning it down. The status icons are
 * already written unscaled for the same reason; see indicators.c.
 *
 * Painted from the indicator hook rather than from an effect, so it shows under
 * every effect including the one that paints black -- a brightness you cannot
 * see is still a brightness you are setting.
 */

#include "quantum.h"

#include "k100.h"
#include "led_zones.h"

/*
 * How long the ring keeps showing the level after the last detent.
 *
 * Long enough to read after a single click, short enough that it is not still
 * sitting there when attention has moved on. One number; there is nothing to
 * tune it against but the hand.
 */
#define K100_WHEEL_SHOW_MS 1500

/*
 * How long the fill takes to run out to the level when the gauge appears.
 *
 * Not decoration. At the top of the range a full gauge is a lit ring, which is
 * exactly what the ring looks like when it is doing nothing at all -- measured
 * on the keyboard at a brightness of 255, where the gauge was reported as hard
 * to make out, and correctly so: there was nothing to make out. Motion is what
 * the eye catches, so the fill starts at nothing and runs out to wherever the
 * knob is, and the first thing that happens when the wheel is touched is the
 * ring going dark.
 *
 * Once per gesture, not once per detent: it runs from the first click of a turn,
 * so turning on through it tracks the level instead of starting again.
 */
#define K100_WHEEL_SWEEP_MS 180

static uint32_t turned_at;
static uint32_t gesture_at;
static bool     turned;

/** Whether the ring is showing a level rather than whatever the lighting put there. */
static bool showing(void) {
    return turned && timer_elapsed32(turned_at) < K100_WHEEL_SHOW_MS;
}

void k100_wheel_turned(void) {
    if (!showing()) {
        gesture_at = timer_read32();
    }

    turned_at = timer_read32();
    turned    = true;
}

/** How much of the fill has arrived, while it is still arriving. */
static uint16_t swept(uint16_t filled) {
    const uint32_t age = timer_elapsed32(gesture_at);

    if (age >= K100_WHEEL_SWEEP_MS) {
        return filled;
    }

    return (uint16_t)((uint32_t)filled * age / K100_WHEEL_SWEEP_MS);
}

/**
 * @brief   How lit one segment of the gauge is, from how far the fill has passed it.
 *
 * @details A segment the fill has gone by is full, one it has not reached is
 *          dark, and the one it stops inside is lit by however far in it got --
 *          which is what makes eight lights read as a continuous level rather
 *          than as eight steps.
 */
static uint8_t segment_level(uint16_t filled, uint8_t segment) {
    const uint16_t starts = (uint16_t)segment * UINT8_MAX;

    if (filled <= starts) {
        return 0;
    }
    if (filled - starts >= UINT8_MAX) {
        return UINT8_MAX;
    }

    return (uint8_t)(filled - starts);
}

void k100_wheel_ring_paint(void) {
#ifdef RGB_MATRIX_ENABLE
    if (!showing()) {
        return;
    }

    const k100_zone_t ring = k100_zones[K100_ZONE_CTRLWHEEL];

    /* Against the ceiling the brightness is actually set against, rather than
       against 255: a firmware built with a lower one would otherwise have a
       gauge that never fills. */
    const uint16_t filled = swept(
        (uint16_t)((uint32_t)rgb_matrix_get_val() * ring.count * UINT8_MAX /
                   RGB_MATRIX_MAXIMUM_BRIGHTNESS));

    /* The colour the ring is set to, so the gauge is in the user's own colour and
       not in one chosen here. Its stored brightness is deliberately not read: the
       level is what the brightness means here. */
    const k100_colour_t colour = k100_per_key_colour(ring.first);

    for (uint8_t at = 0; at < ring.count; at++) {
        const hsv_t hsv = {.h = colour.hue, .s = colour.sat,
                           .v = segment_level(filled, at)};
        const rgb_t rgb = hsv_to_rgb(hsv);

        rgb_matrix_set_color(ring.first + at, rgb.r, rgb.g, rgb.b);
    }
#endif
}
