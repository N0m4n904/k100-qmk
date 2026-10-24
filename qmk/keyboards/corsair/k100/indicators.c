// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The six icons above the keys.
 *
 * They are single-colour LEDs -- one channel each, so on or off and nothing in
 * between -- and they sit in Corsair's LED map beside the key lighting, which
 * is how they came to be lit white by an effect that had no idea what they
 * were. Their layout entries carry no flags, so no effect reaches them, and
 * this is the only thing that drives them.
 *
 * Which icon is on which LED was read off the keyboard: each was lit alone in
 * turn and the order matched the printed icons, Macro first. Two of them this
 * firmware leaves dark, and deliberately -- an indicator that lights for
 * something it is not reporting is worse than one that never lights.
 *
 * The same three locks the host reports are also shown **on the keys that carry
 * them**, in a colour each that is the user's to pick. That is the same fact in
 * a second place, and the second place is the one somebody is looking at: an
 * icon above the number pad is a long way from the key that was just pressed.
 * Which LED sits under which lock key is generated out of the measured table,
 * and the colours are stored beside the painted ones; see per_key_colour.c.
 */

#include "quantum.h"

#include "k100.h"
#include "lock_leds.h"

/**
 * @brief The status LEDs, in the order they appear in the LED map.
 *
 * The indices run on from the key lighting; they are the last entries in the
 * table and the first ones that are not keys.
 */
enum k100_indicator {
    K100_INDICATOR_MACRO = 173,
    K100_INDICATOR_MUTE,
    K100_INDICATOR_WIN_LOCK,
    K100_INDICATOR_NUM_LOCK,
    K100_INDICATOR_CAPS_LOCK,
    K100_INDICATOR_SCROLL_LOCK,
};

/**
 * @brief   Whether each lock the host reports is on, in the generated order.
 *
 * @details Built *at* the places lock_leds.h defines rather than in the order they are
 *          written here, so that reordering the generator's list moves this with it and a
 *          lock that leaves the list stops the build. QMK's names for the three bits are the
 *          one thing that cannot be generated, so they are the one thing written out.
 */
static bool lock_is_on(uint8_t lock, led_t host) {
    const bool state[K100_LOCK_COUNT] = {
        [K100_LOCK_CAPS]    = host.caps_lock,
        [K100_LOCK_NUMLOCK] = host.num_lock,
        [K100_LOCK_SCROLL]  = host.scroll_lock,
    };

    return state[lock];
}

/**
 * @brief   Paints the key that carries a lock, while that lock is on.
 *
 * @details After the effect and before the probe, because this is a fact about the keyboard
 *          and the effect underneath it is decoration. The stored brightness scales the one
 *          the wheel sets, like every other colour here, so dimming the keyboard dims this
 *          too and a brightness of zero is a user saying they would rather see the effect.
 */
static void show_locks(led_t host) {
    for (uint8_t lock = 0; lock < K100_LOCK_COUNT; lock++) {
        const k100_colour_t colour = k100_lock_colour(lock);

        if (colour.val == 0 || !lock_is_on(lock, host)) {
            continue;
        }

        /* Scaled by hand and converted with the plain conversion: `scale8` and
           rgb_matrix's own hue conversion are both internal to rgb_matrix.c, and
           `hsv_to_rgb` is what that one calls anyway, so the colour comes out the
           same as the effects' do. */
        const hsv_t hsv = {.h = colour.hue, .s = colour.sat,
                           .v = (uint8_t)((uint16_t)colour.val * rgb_matrix_config.hsv.v / UINT8_MAX)};
        const rgb_t rgb = hsv_to_rgb(hsv);

        rgb_matrix_set_color(k100_lock_leds[lock], rgb.r, rgb.g, rgb.b);
    }
}

/** @brief One icon, on or off, since that is all these LEDs can be. */
static void indicator(uint8_t index, bool lit) {
    const uint8_t level = lit ? UINT8_MAX : 0U;

    rgb_matrix_set_color(index, level, level, level);
}

bool rgb_matrix_indicators_kb(void) {
    const led_t host = host_keyboard_led_state();

    indicator(K100_INDICATOR_NUM_LOCK, host.num_lock);
    indicator(K100_INDICATOR_CAPS_LOCK, host.caps_lock);

    /* Driven like the other two and may still never light: whether the host
       reports scroll lock is the host's business, and a Wayland desktop
       generally does not. Measured on this keyboard -- the reported byte held
       at num lock alone through a hundred and fifty samples taken while the key
       was being pressed. */
    indicator(K100_INDICATOR_SCROLL_LOCK, host.scroll_lock);

    /* The lock this icon names is the one QMK has: with it set, the GUI keys
       are suppressed, which is what a Windows lock does. It is the keyboard's
       own state rather than the host's, and the dedicated key beside the iCUE
       wheel -- the one the manual calls the Windows Lock key -- toggles it. */
    indicator(K100_INDICATOR_WIN_LOCK, keymap_config.no_gui);

    /* No macros are recorded here, but the icon marks a mode and this firmware
       has one worth marking: the layer the Fn key holds, which is otherwise
       invisible. */
    indicator(K100_INDICATOR_MACRO, layer_state_is(1));

    /* Dark, because the host tells a keyboard about its locks and nothing
       else. Mute lives in the host's mixer, and a keyboard that lit this from
       its own volume keys would be reporting what it last asked for rather
       than what is so. */
    indicator(K100_INDICATOR_MUTE, false);

    /* The keys that carry the locks, which is the same news as the icons above
       in the place it is being looked for. */
    show_locks(host);

    /* Last, so that a probed LED shows even where an icon would otherwise sit:
       the point of it is to find out what a given index lights. */
    k100_led_probe_paint();

    /* And this hook is also where a frame ends: rgb_matrix calls it once the last
       slice has been worked out, which makes it the one place that can count
       frames without counting slices. See lighting_rate.c. */
    k100_frame_rendered();

    return rgb_matrix_indicators_user();
}
