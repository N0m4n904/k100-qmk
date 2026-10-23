// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The three effects this keyboard has of its own.
 *
 * **Two rainbows, because QMK's own read the stored colour.** CYCLE_ALL and
 * CYCLE_LEFT_RIGHT replace the hue -- with the clock and with the LED's
 * position -- and keep the saturation they are handed, which is the one the
 * plain colour is set to. A keyboard set to white therefore has white
 * rainbows: the effect runs, every LED is painted, and nothing about it looks
 * like a rainbow. The colour belongs to the effect that is a colour. These two
 * take the hue the same way and paint it fully saturated, so the one knob the
 * application offers does not reach across all three.
 *
 * **Lighting off, as an effect rather than as an absence.** QMK already has a
 * way to have no lighting: mode zero, RGB_MATRIX_NONE, which VIA's menu
 * reaches by sending nothing more than a zero. It is the wrong one for this
 * keyboard, because it takes the six status icons with it. QMK draws
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
 * emits that menu from the same table these effects are listed in.
 *
 * The flags test is what leaves the icons alone, here and in the two above:
 * their layout entries carry no flags, so no effect reaches them, and
 * indicators.c is the only thing that drives them. The six slots with nothing
 * fitted are skipped for the same reason.
 */

RGB_MATRIX_EFFECT(K100_RAINBOW)
RGB_MATRIX_EFFECT(K100_RAINBOW_SWEEP)
RGB_MATRIX_EFFECT(K100_LIGHTS_OFF)

#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

/*
 * The numbers the VIA menu's entries send.
 *
 * QMK numbers the effects a build compiles, in the order it includes them, and
 * the generated VIA definition carries those numbers as literals -- it cannot
 * ask the firmware. Adding an animation ahead of these, or reordering the
 * declarations above, moves them, and the app's entries would then select
 * whatever took their numbers. This stops the build instead, which is the only
 * moment at which both can still be put right.
 */
_Static_assert(RGB_MATRIX_CUSTOM_K100_RAINBOW == 2,
               "the VIA definition sends 2 for Rainbow; regenerate it with tools/generate-qmk-layout.py");
_Static_assert(RGB_MATRIX_CUSTOM_K100_RAINBOW_SWEEP == 3,
               "the VIA definition sends 3 for Rainbow Sweep; regenerate it with tools/generate-qmk-layout.py");
_Static_assert(RGB_MATRIX_CUSTOM_K100_LIGHTS_OFF == 4,
               "the VIA definition sends 4 for Off; regenerate it with tools/generate-qmk-layout.py");

/** @brief The whole keyboard through the hues together, at full saturation. */
static hsv_t k100_rainbow_math(hsv_t hsv, uint8_t i, uint8_t time) {
    (void)i;
    hsv.h = time;
    hsv.s = UINT8_MAX;
    return hsv;
}

bool K100_RAINBOW(effect_params_t* params) {
    return effect_runner_i(params, &k100_rainbow_math);
}

/**
 * @brief   The hues spread across the keyboard and moving, at full saturation.
 *
 * @details The one effect that reads an LED's position, which is why it is the
 *          one that shows a wrong entry in the layout at a glance: a key whose
 *          coordinate belongs to another key is a dot of the wrong colour in an
 *          otherwise even sweep. See LED_SLOTS in tools/generate-qmk-layout.py
 *          for how the positions were measured.
 */
static hsv_t k100_rainbow_sweep_math(hsv_t hsv, uint8_t i, uint8_t time) {
    hsv.h = g_led_config.point[i].x - time;
    hsv.s = UINT8_MAX;
    return hsv;
}

bool K100_RAINBOW_SWEEP(effect_params_t* params) {
    return effect_runner_i(params, &k100_rainbow_sweep_math);
}

bool K100_LIGHTS_OFF(effect_params_t* params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();
        rgb_matrix_set_color(i, 0, 0, 0);
    }

    return rgb_matrix_check_finished_leds(led_max);
}

#endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS
