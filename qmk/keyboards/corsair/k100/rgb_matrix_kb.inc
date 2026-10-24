// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The six effects this keyboard has of its own.
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
 * LED the colour stored for it, at the brightness the wheel sets.
 *
 * **The two reactive ones** light a key because it was pressed, and what they
 * decide is how bright it is and when -- not what colour it is. The colour comes
 * from the reaction's own setting: the painted table by default, so that a
 * keyboard somebody has painted reacts in their own colours, or one colour
 * chosen for the reaction. Either way it is not the colour the plain-colour
 * effect uses, which is the mistake this keyboard already made once: one knob
 * reaching across three effects. See per_key_colour.c for both.
 *
 * Both leave the lights that are not keys showing their painted colour, which is
 * the only honest answer: no key press ever lands on a light bar, the ring or the
 * logo, so there is nothing for them to react to. Someone who wants them dark
 * under a reactive effect has the control for it already -- each zone's own
 * brightness, `Off` included.
 *
 * New effects are appended rather than inserted, because the numbers VIA sends
 * are the order these are declared in, and appending is the one change that
 * moves nothing already in use.
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
RGB_MATRIX_EFFECT(K100_REACTIVE)
RGB_MATRIX_EFFECT(K100_REACTIVE_SPLASH)

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
_Static_assert(RGB_MATRIX_CUSTOM_K100_REACTIVE == 6,
               "the VIA definition sends 6 for Reactive; regenerate it with tools/generate-qmk-layout.py");
_Static_assert(RGB_MATRIX_CUSTOM_K100_REACTIVE_SPLASH == 7,
               "the VIA definition sends 7 for Reactive Splash; regenerate it with tools/generate-qmk-layout.py");

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
 * @brief   What the key painter put on one LED, as light.
 *
 * @details The colours come from per_key_colour.c, which is where they are
 *          stored and how the application sets them. Each carries its own
 *          brightness, and the wheel's scales it rather than replacing it: the
 *          whole keyboard dims together, a key set to half stays half of
 *          whatever that is, and a key set to zero is off at any setting.
 *
 *          A key nobody has painted is white and fully lit, which is the same
 *          thing the keyboard shows with no per-key colour set at all.
 *
 *          Three effects start here, which is what stops them disagreeing about
 *          what colour the keyboard is.
 */
static hsv_t k100_light(k100_colour_t colour) {
    return (hsv_t){.h = colour.hue, .s = colour.sat,
                   .v = scale8(colour.val, rgb_matrix_config.hsv.v)};
}

bool K100_PER_KEY(effect_params_t* params) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();

        const rgb_t rgb = rgb_matrix_hsv_to_rgb(k100_light(k100_per_key_colour(i)));

        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }

    return rgb_matrix_check_finished_leds(led_max);
}

/*
 * The reactive pair, and the one piece of arithmetic both are made of.
 *
 * A press is a wave. It starts at the key that was pressed and travels outwards
 * at a fixed speed, bright at the front and fading behind it, and an LED is lit
 * by how far behind that front it is. The two effects differ in one thing only:
 * how far an LED counts as being from the press.
 *
 *   Reactive         the key that was pressed is at nothing, and every other
 *                    LED is out of reach -- so the wave is a fade, in place.
 *   Reactive Splash  the distance is the real one, across the layout, so the
 *                    wave is a ring crossing the keyboard.
 *
 * Which is why the front is worked out in the same units as the positions, and
 * why the reach below is 255: the far corner of this keyboard is 233 units from
 * the near one, so a wave that has travelled 255 has crossed all of it, and a
 * fade is then exactly "255 minus how far the wave got".
 */

/** How far a press travels in one reaction's length. */
#define K100_WAVE_REACH UINT8_MAX

/** Past this a wave has left the keyboard: all of it, plus the wave's own width. */
#define K100_WAVE_GONE (K100_WAVE_REACH + UINT8_MAX)

/*
 * How long a reaction lasts, which is what the speed control sets.
 *
 * Not QMK's own mapping, which is 65535 / (speed + 1) milliseconds and reaches
 * 65 seconds at the slow end: a fade that outlasts the typing is a keyboard that
 * is simply on, which is the effect this is not. Both ends of this range are
 * still a reaction, and the control reaches them from the application's slider
 * and from Fn+Left and Fn+Right alike.
 */
#define K100_REACT_FASTEST_MS 150
#define K100_REACT_SLOWEST_MS 1500

static uint16_t k100_react_ms(void) {
    const uint16_t span = K100_REACT_SLOWEST_MS - K100_REACT_FASTEST_MS;

    return K100_REACT_SLOWEST_MS -
           (uint16_t)((uint32_t)rgb_matrix_config.speed * span / UINT8_MAX);
}

/**
 * @brief   How bright one LED is from one press, given where that press has got to.
 *
 * @details Nothing in front of the wave, the front itself at full, and what is
 *          behind it dimmer the further behind it is -- until the wave's width
 *          has passed, after which this press is no longer lighting anything.
 */
static uint8_t k100_wave(uint16_t front, uint8_t away) {
    if (front < away) {
        return 0;
    }

    const uint16_t behind = front - away;

    return behind > UINT8_MAX ? 0 : (uint8_t)(UINT8_MAX - behind);
}

/**
 * @brief   Where each remembered press has got to.
 *
 * @details Worked out once for a slice of a frame rather than once per LED per
 *          press: it is a fact about the press and not about any LED, and 190
 *          LEDs by eight presses is the difference between eight divisions and
 *          fifteen hundred. The ring is double buffered and does not move under
 *          a frame, so one answer holds for every slice of it.
 */
static void k100_wave_fronts(uint16_t *front, uint16_t react_ms) {
    for (uint8_t hit = 0; hit < g_last_hit_tracker.count; hit++) {
        const uint32_t reached =
            (uint32_t)K100_WAVE_REACH * g_last_hit_tracker.tick[hit] / react_ms;

        front[hit] = reached > K100_WAVE_GONE ? K100_WAVE_GONE : (uint16_t)reached;
    }
}

/**
 * @brief   Whether a remembered press was on the key this LED sits under.
 *
 * @details Matched **by position rather than by index**, which is what gets the
 *          four keys with two lights right. The ring records the one LED the
 *          matrix map names, so the second light under the wheel button, the
 *          profile-switch key, the Windows-lock key and the mute key would never
 *          see the press -- a key half lit on every stroke. Two LEDs under one
 *          key carry the same position in the layout, and the generator refuses
 *          to emit a layout where two *keys* share a position, so the position is
 *          the key. per_key_colour.c pairs them off the same way.
 */
static bool k100_hit_here(uint8_t led, uint8_t hit) {
    return g_last_hit_tracker.x[hit] == g_led_config.point[led].x &&
           g_last_hit_tracker.y[hit] == g_led_config.point[led].y;
}

/** How far an LED is from a remembered press, in the layout's own units. */
static uint8_t k100_away(uint8_t led, uint8_t hit) {
    const int16_t dx = (int16_t)g_led_config.point[led].x - g_last_hit_tracker.x[hit];
    const int16_t dy = (int16_t)g_led_config.point[led].y - g_last_hit_tracker.y[hit];

    return sqrt16((uint16_t)(dx * dx + dy * dy));
}

static uint8_t k100_fade_level(uint8_t led, const uint16_t *front) {
    uint8_t level = 0;

    for (uint8_t hit = 0; hit < g_last_hit_tracker.count; hit++) {
        if (k100_hit_here(led, hit)) {
            level = qadd8(level, k100_wave(front[hit], 0));
        }
    }

    return level;
}

static uint8_t k100_splash_level(uint8_t led, const uint16_t *front) {
    uint8_t level = 0;

    for (uint8_t hit = 0; hit < g_last_hit_tracker.count; hit++) {
        level = qadd8(level, k100_wave(front[hit], k100_away(led, hit)));
    }

    return level;
}

/**
 * @brief   Paints the painted keyboard, with the keys as bright as a reaction says.
 *
 * @details The lights that are not keys keep their painted colour and their
 *          painted brightness, because nothing was ever pressed on them -- the
 *          bars, the ring and the logo are set in their own menu and this leaves
 *          them alone.
 *
 *          A key takes the colour the reaction is set to, which is its painted
 *          one unless somebody has chosen otherwise, and takes its brightness
 *          from the wave. See per_key_colour.c for that choice; the effect itself
 *          has no opinion about which colour it is painting, and that is the
 *          point of the split.
 */
static bool k100_run_reactive(effect_params_t *params,
                              uint8_t (*level)(uint8_t led, const uint16_t *front)) {
    RGB_MATRIX_USE_LIMITS(led_min, led_max);

    uint16_t front[LED_HITS_TO_REMEMBER];
    k100_wave_fronts(front, k100_react_ms());

    for (uint8_t i = led_min; i < led_max; i++) {
        RGB_MATRIX_TEST_LED_FLAGS();

        const bool key = (g_led_config.flags[i] & LED_FLAG_KEYLIGHT) != 0;

        hsv_t hsv = k100_light(key ? k100_reaction_colour(i) : k100_per_key_colour(i));

        if (key) {
            hsv.v = scale8(hsv.v, level(i, front));
        }

        const rgb_t rgb = rgb_matrix_hsv_to_rgb(hsv);

        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }

    return rgb_matrix_check_finished_leds(led_max);
}

bool K100_REACTIVE(effect_params_t* params) {
    return k100_run_reactive(params, &k100_fade_level);
}

bool K100_REACTIVE_SPLASH(effect_params_t* params) {
    return k100_run_reactive(params, &k100_splash_level);
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
