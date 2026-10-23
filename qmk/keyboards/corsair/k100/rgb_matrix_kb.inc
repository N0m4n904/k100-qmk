// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The four effects this keyboard has of its own.
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
 * **The painted one** is what makes VIA's key painter visible: it gives every
 * LED the colour stored for it, at the brightness the wheel sets. It is last
 * because the numbers VIA sends are the order these are declared in, and
 * appending is the one change that moves nothing already in use.
 *
 * The flags test is what leaves the icons alone, in all of them:
 * their layout entries carry no flags, so no effect reaches them, and
 * indicators.c is the only thing that drives them. The six slots with nothing
 * fitted are skipped for the same reason.
 */

RGB_MATRIX_EFFECT(K100_RAINBOW)
RGB_MATRIX_EFFECT(K100_RAINBOW_SWEEP)
RGB_MATRIX_EFFECT(K100_LIGHTS_OFF)
RGB_MATRIX_EFFECT(K100_PER_KEY)

#ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

/*
 * For the painted effect's colours, and inside the guard rather than at the top
 * of the file: rgb_matrix.c includes this three times, and one of those is from
 * *inside* the enum of effect names. A header included there is a declaration
 * inside an enum body, which is what "expected identifier before 'enum'" means
 * when it comes out of a file that compiles perfectly well on its own.
 */
#include "k100.h"

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
_Static_assert(RGB_MATRIX_CUSTOM_K100_PER_KEY == 5,
               "the VIA definition sends 5 for Per Key; regenerate it with tools/generate-qmk-layout.py");

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

/**
 * @brief   What the key painter put on each LED.
 *
 * @details The colours come from per_key_colour.c, which is where they are
 *          stored and how the application sets them. Brightness is not one of
 *          them: it is the one the wheel sets, so the whole keyboard dims
 *          together and a painted colour keeps its hue while it does.
 *
 *          A key nobody has painted is white, because an untouched colour is
 *          zero and zero saturation is white -- the same thing the keyboard
 *          shows with no per-key colour set at all.
 */
bool K100_PER_KEY(effect_params_t* params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    hsv_t hsv = {.v = rgb_matrix_config.hsv.v};

    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();

        const k100_colour_t colour = k100_per_key_colour(i);

        hsv.h     = colour.hue;
        hsv.s     = colour.sat;
        rgb_t rgb = rgb_matrix_hsv_to_rgb(hsv);

        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }

    return rgb_matrix_check_finished_leds(led_max);
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
