// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Everything the keyboard remembers about its lighting.
 *
 * It owns VIA's custom configuration region, which is this keyboard's whole share of the
 * part's EEPROM beyond the keymap and the macros: a colour for every LED, a colour for each
 * lock key, what the reactive effects do, and how long the lighting waits before it rests.
 * One file, because one region -- what sits at which offset is a single question, and two
 * files answering it separately is how a stored layout comes apart.
 *
 * It was called per_key_colour.c while the colours were all there was, which stopped being
 * true some features ago.
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
#include "led_zones.h"
#include "lock_leds.h"

_Static_assert(K100_PER_KEY_LEDS == RGB_MATRIX_LED_COUNT,
               "the colour table is per LED, so it is as long as the LED map");
_Static_assert(sizeof(k100_colour_t) == 3,
               "a colour is stored by its offset in the region, so it may not be padded");
_Static_assert(K100_VIA_LOCK_FIRST >= K100_VIA_ZONE_FIRST + 2 * K100_ZONE_COUNT,
               "the lock controls start inside the zones' run of values");

/** What an unpainted LED is: white, and as bright as the wheel is set. */
#define K100_COLOUR_DEFAULT ((k100_colour_t){.hue = 0, .sat = 0, .val = UINT8_MAX})

static k100_colour_t colours[K100_PER_KEY_LEDS];

/*
 * What a key shows while its lock is on.
 *
 * Stored beside the per-LED colours and written the same way, because it is the same kind of
 * thing: a colour somebody chose, which has to survive a power cycle to be worth choosing.
 * Red at full to begin with -- an unpainted keyboard is white, so a default that cannot be
 * seen against it would read as a feature that does not work.
 */
static k100_colour_t locks[K100_LOCK_COUNT];

#define K100_LOCK_COLOUR_DEFAULT ((k100_colour_t){.hue = 0, .sat = UINT8_MAX, .val = UINT8_MAX})

/** Where the lock colours sit in the region: after every LED's own. */
#define K100_LOCKS_OFFSET (K100_PER_KEY_LEDS * sizeof(k100_colour_t))

/*
 * What the reactive effects light a key in.
 *
 * Two things, and both belong here because both are colours somebody chose: where the
 * reaction takes its colour from, and the one colour it takes when that is not the painted
 * one.
 *
 * The painted keyboard is the default source and is the richer answer -- somebody who has
 * painted their keyboard sees it react in their own colours, and nothing new had to be stored
 * to make that true. It is not an answer somebody who has painted nothing can *change*,
 * though, and a reaction nobody can recolour is a feature with a decision missing.
 *
 * A hue and a saturation, and no brightness. Deciding the brightness is what a reaction *is*,
 * so one stored here would be a second control fighting the first. The painted entry's own
 * brightness is still read even so, because it is the only way a single key is turned off,
 * and a key somebody switched off has no business lighting up under a different effect.
 *
 * And two more bytes, for what else takes part. The four buttons above the keys are keys, so
 * a reaction had them flashing with the typing, which is not what a strip of mode buttons
 * above the function row is for; they stay lit unless asked. The three bars and the logo are
 * not keys at all, so only a wave crossing the board can reach them -- a fade happens on the
 * key that was pressed, and none of them is one.
 *
 * Five bytes, out of the slack this region was given for exactly this: so that the next small
 * thing to need a few bytes would not cost anybody their keymap. Two are left, and the next
 * reshape should take the chance to leave rather more. See config.h.
 */
enum k100_reaction_source {
    K100_REACTION_PAINTED,
    K100_REACTION_CHOSEN,
};

typedef struct {
    uint8_t source;
    uint8_t hue;
    uint8_t sat;
    uint8_t buttons;
    uint8_t lights;
} k100_reaction_t;

_Static_assert(sizeof(k100_reaction_t) == 5,
               "the reaction is stored by its offset in the region, so it may not be padded");

/** Painted, white, and the buttons and the bars staying lit -- which is what an erased region
    already reads as, so a keyboard that has never been told any of this behaves exactly as
    one that was told the defaults. */
static k100_reaction_t reaction;

#define K100_REACTION_OFFSET (K100_LOCKS_OFFSET + K100_LOCK_COUNT * sizeof(k100_colour_t))

/*
 * How many minutes of being left alone before the lighting rests, and zero for never.
 *
 * Minutes rather than a scale, because that is the unit the question is asked in. Zero is the
 * default and reads out of an erased region for free -- a keyboard that goes dark on its own
 * without being asked to looks broken, and "it turned itself off" is a bug report rather than
 * a feature working. See lighting_sleep.c, which also turns it off while the host sleeps, and
 * that one is not a preference.
 */
static uint8_t sleep_after;

#define K100_SLEEP_OFFSET (K100_REACTION_OFFSET + sizeof(k100_reaction_t))

/* Stated here rather than with the other checks at the top, because this is the first point
   at which everything the region holds has been declared. */
_Static_assert(VIA_EEPROM_CUSTOM_CONFIG_SIZE >= K100_SLEEP_OFFSET + sizeof(sleep_after),
               "the custom config region is too small for what it is asked to hold; "
               "see VIA_EEPROM_CUSTOM_CONFIG_SIZE in config.h");

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

static void store_lock(uint8_t lock) {
    via_update_custom_config(&locks[lock], K100_LOCKS_OFFSET + lock * sizeof(locks[0]),
                             sizeof(locks[0]));
}

static void store_reaction(void) {
    via_update_custom_config(&reaction, K100_REACTION_OFFSET, sizeof(reaction));
}

static void store_sleep(void) {
    via_update_custom_config(&sleep_after, K100_SLEEP_OFFSET, sizeof(sleep_after));
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

    for (uint8_t lock = 0; lock < K100_LOCK_COUNT; lock++) {
        locks[lock] = K100_LOCK_COLOUR_DEFAULT;
        store_lock(lock);
    }

    reaction = (k100_reaction_t){.source = K100_REACTION_PAINTED, .hue = 0, .sat = 0,
                                 .buttons = false, .lights = false};
    store_reaction();

    sleep_after = 0;
    store_sleep();
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
    via_read_custom_config(locks, K100_LOCKS_OFFSET, sizeof(locks));
    via_read_custom_config(&reaction, K100_REACTION_OFFSET, sizeof(reaction));
    via_read_custom_config(&sleep_after, K100_SLEEP_OFFSET, sizeof(sleep_after));
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

/*
 * A zone is the same table underneath, written a run at a time, and what comes back is its
 * first LED -- a zone has one colour and one brightness, so its first LED is both.
 *
 * Unlike a painted key it does not take the brush. A bar is one thing with two properties,
 * and each control sets its own: the colour sets hue and saturation, the brightness sets how
 * bright, and both show the moment they are used. The brush belongs to the painter, where a
 * stroke covers keys one at a time and has to be told what to carry.
 */
static void paint_zone(uint8_t zone, k100_colour_t colour) {
    for (uint8_t at = 0; at < k100_zones[zone].count; at++) {
        const uint8_t led = k100_zones[zone].first + at;

        colours[led] = colour;
        store(led);
    }
}

void k100_zone_colour_get(uint8_t zone, uint8_t *value_data) {
    if (zone >= K100_ZONE_COUNT) {
        return;
    }

    const k100_colour_t colour = k100_per_key_colour(k100_zones[zone].first);

    value_data[0] = colour.hue;
    value_data[1] = colour.sat;
}

void k100_zone_colour_set(uint8_t zone, uint8_t *value_data) {
    if (zone >= K100_ZONE_COUNT) {
        return;
    }

    k100_colour_t wanted = k100_per_key_colour(k100_zones[zone].first);

    wanted.hue = value_data[0];
    wanted.sat = value_data[1];

    // A colour set on a zone that is off turns it on, because the alternative is a control
    // that appears to do nothing. Dimming and switching off stay with the brightness beside
    // it, which is where someone looks for them.
    if (wanted.val == 0) {
        wanted.val = UINT8_MAX;
    }

    paint_zone(zone, wanted);
}

void k100_zone_value_get(uint8_t zone, uint8_t *value_data) {
    if (zone >= K100_ZONE_COUNT) {
        return;
    }

    value_data[0] = k100_per_key_colour(k100_zones[zone].first).val;
}

void k100_zone_value_set(uint8_t zone, uint8_t *value_data) {
    if (zone >= K100_ZONE_COUNT) {
        return;
    }

    k100_colour_t wanted = k100_per_key_colour(k100_zones[zone].first);

    wanted.val = value_data[0];

    paint_zone(zone, wanted);
}

void k100_per_key_colour_get(uint8_t *value_data) {
    const uint8_t led = value_data[0];
    if (led >= K100_PER_KEY_LEDS) {
        return;
    }

    value_data[2] = colours[led].hue;
    value_data[3] = colours[led].sat;

    /*
     * The brightness goes in the byte after the two VIA asks for, which it
     * never looks at: it reads the hue and the saturation out of a reply by
     * position and ignores the rest of the report.
     *
     * Free, and the difference between a backup of a painted keyboard and
     * half of one. The only thing that clears these colours is a change to
     * the shape of the region they live in, and what makes such a change
     * survivable is being able to read all of it out first -- which, without
     * this, nothing could: the brightness is stored and was not readable.
     */
    value_data[4] = colours[led].val;
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

/*
 * The lock colours, which are read and written like a zone's and for the same reasons: a
 * colour control for the colour, a brightness beside it that acts at once, and zero
 * brightness as the way to say "do not show it on the key at all".
 *
 * Unlike a zone there is nothing to repaint here. What shows them is the indicator hook,
 * which runs every frame and reads these as it goes; see indicators.c.
 */
k100_colour_t k100_lock_colour(uint8_t lock) {
    return lock < K100_LOCK_COUNT ? locks[lock] : (k100_colour_t){0, 0, 0};
}

void k100_lock_colour_get(uint8_t lock, uint8_t *value_data) {
    if (lock >= K100_LOCK_COUNT) {
        return;
    }

    value_data[0] = locks[lock].hue;
    value_data[1] = locks[lock].sat;
}

void k100_lock_colour_set(uint8_t lock, uint8_t *value_data) {
    if (lock >= K100_LOCK_COUNT) {
        return;
    }

    locks[lock].hue = value_data[0];
    locks[lock].sat = value_data[1];

    // The same courtesy a zone gets: a colour set on something switched off turns it on,
    // because a control that appears to do nothing is worse than one that does too much.
    if (locks[lock].val == 0) {
        locks[lock].val = UINT8_MAX;
    }

    store_lock(lock);
}

void k100_lock_value_get(uint8_t lock, uint8_t *value_data) {
    if (lock >= K100_LOCK_COUNT) {
        return;
    }

    value_data[0] = locks[lock].val;
}

void k100_lock_value_set(uint8_t lock, uint8_t *value_data) {
    if (lock >= K100_LOCK_COUNT) {
        return;
    }

    locks[lock].val = value_data[0];
    store_lock(lock);
}

k100_colour_t k100_reaction_colour(uint8_t led) {
    const k100_colour_t painted = k100_per_key_colour(led);

    if (reaction.source == K100_REACTION_PAINTED) {
        return painted;
    }

    // The painted brightness and not the painted colour: a key turned off stays off.
    return (k100_colour_t){.hue = reaction.hue, .sat = reaction.sat, .val = painted.val};
}

void k100_reaction_source_get(uint8_t *value_data) {
    value_data[0] = reaction.source;
}

void k100_reaction_source_set(uint8_t *value_data) {
    reaction.source = value_data[0] == K100_REACTION_PAINTED ? K100_REACTION_PAINTED
                                                             : K100_REACTION_CHOSEN;
    store_reaction();
}

void k100_reaction_colour_get(uint8_t *value_data) {
    value_data[0] = reaction.hue;
    value_data[1] = reaction.sat;
}

void k100_reaction_colour_set(uint8_t *value_data) {
    reaction.hue = value_data[0];
    reaction.sat = value_data[1];

    /*
     * And picking a colour is how the reaction comes to use one, which is the same courtesy
     * the zones get: a control whose use shows nothing is worse than one that does a little
     * more than it says. Going back to the painted keyboard is the choice beside it, which is
     * where somebody looks for it.
     */
    reaction.source = K100_REACTION_CHOSEN;
    store_reaction();
}

bool k100_buttons_react(void) {
    return reaction.buttons != 0;
}

void k100_buttons_react_get(uint8_t *value_data) {
    value_data[0] = reaction.buttons;
}

void k100_buttons_react_set(uint8_t *value_data) {
    reaction.buttons = value_data[0] != 0;
    store_reaction();
}

bool k100_lights_react(void) {
    return reaction.lights != 0;
}

void k100_lights_react_get(uint8_t *value_data) {
    value_data[0] = reaction.lights;
}

void k100_lights_react_set(uint8_t *value_data) {
    reaction.lights = value_data[0] != 0;
    store_reaction();
}

uint8_t k100_sleep_after(void) {
    return sleep_after;
}

void k100_sleep_after_get(uint8_t *value_data) {
    value_data[0] = sleep_after;
}

void k100_sleep_after_set(uint8_t *value_data) {
    sleep_after = value_data[0];
    store_sleep();
}
