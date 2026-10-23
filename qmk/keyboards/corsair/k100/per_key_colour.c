// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * A colour of its own for every LED.
 *
 * VIA paints keys: a palette appears beside the keyboard, and dragging across
 * it sends one colour per key. The protocol for that is not in QMK -- the
 * application reaches it on **channel zero, value one**, which is why this
 * keyboard's own values start at two -- and it is read out of VIA's own bundle
 * rather than out of a specification:
 *
 *     get  [0x08, 0, 1, led, 1]           the colour comes back at bytes 5, 6
 *     set  [0x07, 0, 1, led, 1, hue, sat]
 *
 * The one after the index is a byte the application always sends as one and
 * nothing here reads.
 *
 * **Brightness is not in what the application sends**, and a hue and a
 * saturation cannot say "off" -- zero saturation is white. So each LED stores a
 * brightness as well, and the one a painted key gets is a setting of the brush:
 * `K100_VIA_PAINT_VALUE`, a step from full down to off, picked before painting.
 * It scales the brightness the wheel sets rather than replacing it, so the wheel
 * still dims the whole keyboard.
 *
 * **A painted keyboard outlives an update**, which is the whole point of storing
 * it: only a change to the shape of this region clears it, and that is the one
 * case where what is in it cannot be read back. See via_init_kb().
 *
 * **Both lights under a key get the colour.** Four keys have two LEDs -- the
 * wheel button, the profile-switch key, the lock key and the mute key -- and
 * the application paints one index, so the other would stay as it was and the
 * key would be half the colour it was given. Which LEDs share a key is not a
 * table here: two LEDs under one key carry the same position in the layout,
 * and the generator refuses to emit a layout where two LEDs share a position
 * without sharing a key, so the position *is* the answer.
 *
 * Kept in VIA's custom config region, which is what that region is for, and
 * loaded into RAM once because an effect reads every entry on every frame. The
 * region is sized for every LED the firmware has rather than for the keys the
 * application can paint: the bars, the logo and the wheel's ring have no key in
 * the definition and the painter cannot reach them, but they are LEDs like any
 * other, and a menu that sets them is then an addition rather than another
 * change to the EEPROM's shape -- which costs every keyboard its stored keymap.
 *
 * Written through on every colour rather than on the application's save. The
 * part has a real EEPROM, programmed a word at a time, so one colour is one
 * word and the save that follows each one has nothing left to do; via_values.c
 * answers it for that reason.
 */

#include "quantum.h"
#include "via.h"

#include "k100.h"

_Static_assert(K100_PER_KEY_LEDS == RGB_MATRIX_LED_COUNT,
               "the colour table is per LED, so it is as long as the LED map");
_Static_assert(sizeof(k100_colour_t) == 3,
               "a colour is stored by its offset in the region, so it may not be padded");
_Static_assert(VIA_EEPROM_CUSTOM_CONFIG_SIZE >= K100_PER_KEY_LEDS * sizeof(k100_colour_t),
               "the custom config region is too small for one colour per LED; see config.h");

/** What an unpainted LED is: white, and as bright as the wheel is set. */
#define K100_COLOUR_DEFAULT ((k100_colour_t){.hue = 0, .sat = 0, .val = UINT8_MAX})

static k100_colour_t colours[K100_PER_KEY_LEDS];

/*
 * The brightness the next painted key is given.
 *
 * Deliberately not stored. It is a brush and not a property of the keyboard, and
 * a brush that came back set to off after a power cycle would make the next
 * stroke look like a keyboard that had stopped working.
 */
static uint8_t paint_value = UINT8_MAX;

/** Where one LED's colour sits in the region. */
static uint16_t offset_of(uint8_t led) {
    return (uint16_t)led * sizeof(k100_colour_t);
}

static void store(uint8_t led) {
    via_update_custom_config(&colours[led], offset_of(led), sizeof(colours[led]));
}

/**
 * @brief   Back to white and fully lit everywhere, in RAM and in the region.
 *
 * @details Written and not merely remembered, because the default is no longer
 *          what an erased region reads as: a colour carries a brightness now,
 *          and zero everywhere is a keyboard that is off everywhere. The driver
 *          skips words that already hold what is being written, so this costs
 *          nothing on a region that is already in this state.
 */
static void forget(void) {
    for (uint16_t led = 0; led < K100_PER_KEY_LEDS; led++) {
        colours[led] = K100_COLOUR_DEFAULT;
    }
    via_update_custom_config(colours, 0, sizeof(colours));
}

/*
 * What is stored, and it is read whatever VIA makes of its own data.
 *
 * VIA's stored data carries the *build date*, so a firmware built on another
 * day resets the keymap and the macros -- which is VIA's decision and not one
 * this keyboard can argue with. The colours do not follow it. They live in a
 * region of this keyboard's own, whose shape is governed by
 * EECONFIG_MAGIC_NUMBER, and nothing about a new build date makes what is in it
 * wrong. Resetting them with the keymap is what happened first, and losing a
 * painted keyboard to a build on the next day is not a thing to ship.
 *
 * So the only thing that clears them is the thing that has to: a change to the
 * shape of what is stored, which erases the EEPROM and arrives in
 * eeconfig_init_kb() below.
 */
void via_init_kb(void) {
    via_read_custom_config(colours, 0, sizeof(colours));
}

/*
 * The other way the stored state goes away, and the reason this hook exists.
 *
 * Also where the defaults are put back after the EEPROM is wiped under our
 * feet, which the erase leaves as zeros -- off, for a colour that has a
 * brightness.
 *
 * `via_init()` runs *before* `quantum_init()`, which is where QMK notices that
 * EECONFIG_MAGIC_NUMBER has moved and erases the whole EEPROM. So on the boot
 * after a firmware whose stored shape changed, via_init_kb() above has already
 * read the region -- and what it read was the *previous* firmware's bytes,
 * because the erase had not happened yet. Measured, with the table at a new
 * address: LED 1 came back with saturation 41, which is 0x29, which is the
 * low byte of KC_ESC where the old dynamic keymap's second keycode had been.
 *
 * This runs from inside that erase, after it, so it is where both the RAM copy
 * and the region are put back to what an unpainted keyboard is.
 */
void eeconfig_init_kb(void) {
    forget();

    // Both of these are what the default does. Overriding it replaces it, so a
    // keyboard that forgets them takes the user hook and its own eeconfig word
    // down with it.
    eeconfig_update_kb(0);
    eeconfig_init_user();
}

k100_colour_t k100_per_key_colour(uint8_t led) {
    return led < K100_PER_KEY_LEDS ? colours[led] : K100_COLOUR_DEFAULT;
}

void k100_paint_value_get(uint8_t *value_data) {
    value_data[0] = paint_value;
}

void k100_paint_value_set(uint8_t *value_data) {
    paint_value = value_data[0];
}

void k100_per_key_colour_get(uint8_t *value_data) {
    const uint8_t led = value_data[0];
    if (led >= K100_PER_KEY_LEDS) {
        return;
    }

    value_data[2] = colours[led].hue;
    value_data[3] = colours[led].sat;
}

void k100_per_key_colour_set(uint8_t *value_data) {
    const uint8_t led = value_data[0];
    if (led >= K100_PER_KEY_LEDS) {
        return;
    }

    const k100_colour_t wanted = {.hue = value_data[2], .sat = value_data[3],
                                  .val = paint_value};

    colours[led] = wanted;
    store(led);

    // The key's other light, if it has one. See the note at the top.
    for (uint8_t other = 0; other < K100_PER_KEY_LEDS; other++) {
        if (other != led && (g_led_config.flags[other] & LED_FLAG_KEYLIGHT) &&
            g_led_config.point[other].x == g_led_config.point[led].x &&
            g_led_config.point[other].y == g_led_config.point[led].y) {
            colours[other] = wanted;
            store(other);
        }
    }
}
