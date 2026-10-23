// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Lighting off, as an effect rather than as an absence.
 *
 * QMK already has a way to have no lighting: mode zero, RGB_MATRIX_NONE, which
 * VIA's menu reaches by sending nothing more than a zero. It is the wrong one
 * for this keyboard, because it takes the six status icons with it. QMK draws
 * indicators from inside the lighting task:
 *
 *     rgb_task_render(effect);
 *     if (effect) {
 *         rgb_matrix_indicators();
 *         rgb_matrix_indicators_advanced(&rgb_effect_params);
 *     }
 *
 * and with no effect running there is no call. The task then stops sending
 * frames altogether -- "we only need to flush once if we are RGB_MATRIX_NONE"
 * -- so there is not even a window left in which another hook could paint
 * them. Num Lock going dark because the key lighting was turned off is not a
 * trade anyone asked for.
 *
 * An effect that paints black is the whole fix. The task keeps running, the
 * indicator hook keeps being called, the frames keep being sent, and what
 * reaches the keyboard is dark keys with the icons still lit. It is what the
 * "Off" entry in the VIA menu selects; see tools/generate-qmk-layout.py, which
 * emits that menu from the same table this effect is listed in.
 *
 * The flags test is what leaves the icons alone here: their layout entries
 * carry no flags, so no effect reaches them, and indicators.c is the only
 * thing that drives them. The eleven unplaced LEDs are skipped for the same
 * reason and have been dark since init.
 */

RGB_MATRIX_EFFECT(K100_LIGHTS_OFF)

#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

/*
 * The number the VIA menu's "Off" entry sends.
 *
 * QMK numbers the effects a build compiles, in the order it includes them, and
 * the generated VIA definition carries that number as a literal -- it cannot
 * ask the firmware. Adding an animation ahead of this one moves it, and the
 * app's Off would then select whatever took the number. This stops the build
 * instead, which is the only moment at which both can still be put right.
 */
_Static_assert(RGB_MATRIX_CUSTOM_K100_LIGHTS_OFF == 4,
               "the VIA definition sends 4 for Off; regenerate it with tools/generate-qmk-layout.py");

bool K100_LIGHTS_OFF(effect_params_t* params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        rgb_matrix_set_color(i, 0, 0, 0);
    }

    return rgb_matrix_check_finished_leds(led_max);
}

#endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS
