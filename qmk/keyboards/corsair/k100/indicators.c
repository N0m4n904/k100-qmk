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
 */

#include "quantum.h"

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

    return rgb_matrix_indicators_user();
}
