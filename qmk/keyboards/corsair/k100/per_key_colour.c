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
 * nothing here reads. Brightness is not part of a colour: it stays the one the
 * wheel sets, which is what a keyboard with a brightness wheel wants.
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
_Static_assert(sizeof(k100_colour_t) == 2,
               "a colour is stored by its offset in the region, so it may not be padded");
_Static_assert(VIA_EEPROM_CUSTOM_CONFIG_SIZE >= K100_PER_KEY_LEDS * sizeof(k100_colour_t),
               "the custom config region is too small for one colour per LED; see config.h");

/*
 * Zero is white, which is what an erased region reads as and what the compiled
 * default colour is, so an untouched keyboard needs nothing seeded here.
 */
static k100_colour_t colours[K100_PER_KEY_LEDS];

/** Where one LED's colour sits in the region. */
static uint16_t offset_of(uint8_t led) {
    return (uint16_t)led * sizeof(k100_colour_t);
}

static void store(uint8_t led) {
    via_update_custom_config(&colours[led], offset_of(led), sizeof(colours[led]));
}

/** Back to white everywhere, which is what an erased region reads as. */
static void forget(void) {
    memset(colours, 0, sizeof(colours));
}

/*
 * What is stored, if anything is.
 *
 * VIA's stored data carries the build date, so a firmware built on another day
 * resets the keymap and the macros -- and the colours go with them, because
 * nothing here can tell "same region, new build" from "the region moved". One
 * rule is easier to live with than a guess: what resets, resets together.
 */
void via_init_kb(void) {
    if (via_eeprom_is_valid()) {
        via_read_custom_config(colours, 0, sizeof(colours));
        return;
    }

    forget();
    via_update_custom_config(colours, 0, sizeof(colours));
}

/*
 * The other way the stored state goes away, and the reason this hook exists.
 *
 * `via_init()` runs *before* `quantum_init()`, which is where QMK notices that
 * EECONFIG_MAGIC_NUMBER has moved and erases the whole EEPROM. So on the boot
 * after a firmware whose stored shape changed, via_init_kb() above has already
 * read the region -- and what it read was the *previous* firmware's bytes,
 * because the erase had not happened yet. Measured, with the table at a new
 * address: LED 1 came back with saturation 41, which is 0x29, which is the
 * low byte of KC_ESC where the old dynamic keymap's second keycode had been.
 *
 * This runs from inside that erase, so it is where the RAM copy is put back in
 * step with a region that has just been zeroed.
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
    return led < K100_PER_KEY_LEDS ? colours[led] : (k100_colour_t){0, 0};
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

    const k100_colour_t wanted = {value_data[2], value_data[3]};

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
