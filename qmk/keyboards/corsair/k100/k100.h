// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Keycodes this keyboard adds, for keys with no HID meaning of their own.
 *
 * In QMK's keyboard range, so VIA can remap them like any other and a keymap
 * that wants the key to do something else is free to say so.
 *
 * This enum is also where the VIA definition gets its labels' *order*: VIA's
 * `customKeycodes` array is positional, its first entry describing the first
 * `QK_KB` keycode. tools/generate-qmk-layout.py reads the declarations below
 * for that order and refuses to emit a definition that leaves one of them
 * unlabelled, so adding a keycode here means adding its label there.
 */
enum k100_keycodes {
    /** Steps the lighting through the effects this firmware has. See lighting_keys.c. */
    K100_RGB_EFFECT = QK_KB_0,

    /** Turns the key lighting off and back on, status icons included. */
    K100_LIGHTS_TOGGLE,
};

/**
 * @brief Values this keyboard answers on VIA's custom channel. See via_values.c.
 *
 * The numbers are VIA's to send and ours to choose, and the generated
 * definition sends them as literals -- tools/generate-qmk-layout.py reads them
 * out of this enum rather than carrying its own copy, so there is one place
 * where each is decided.
 */
enum k100_via_value {
    /**
     * One LED's own colour, which is **not** a number this keyboard chose.
     *
     * VIA's key painter reaches per-key colours on channel zero, value one,
     * and that is fixed in the application. This keyboard's own values
     * therefore start at two; the version used to be here, which would have
     * answered the painter with a version string. See per_key_colour.c.
     */
    K100_VIA_PER_KEY_COLOUR = 1,
    /** The firmware version, as text for VIA to display. */
    K100_VIA_VERSION = 2,
    /** Whether the LED probe is lighting one LED. */
    K100_VIA_LED_PROBE = 3,
    /** Which LED it lights, by the index an effect would use. */
    K100_VIA_LED_INDEX = 4,
    /** How bright the next painted key is, which is also how one is turned off. */
    K100_VIA_PAINT_VALUE = 5,
    /**
     * The first of the zone controls: a colour and a brightness for each light that is not
     * a key.
     *
     * A run rather than a value each, two values per zone -- the colour at an even offset
     * from here and the brightness at the odd one after it. The zones themselves are
     * generated from the measured LED table into led_zones.h, and the definition numbers its
     * controls from here in the same order, so neither side carries a list the other has to
     * agree with. The numbers from here to K100_VIA_ZONE_FIRST + 2 * K100_ZONE_COUNT are
     * therefore taken.
     *
     * Both act at once, on the whole zone, which is the difference between a zone and a
     * painted key: a key is one of a hundred and takes the brush it was painted with, while
     * a bar is one thing with one colour and one brightness, and a control for either is
     * useless if setting it does not show.
     */
    K100_VIA_ZONE_FIRST = 6,
    /**
     * The first of the lock colours: a colour and a brightness for each lock a key reports.
     *
     * Numbered like the zones, two values each in the order lock_leds.h lists them, and
     * placed after the zones' run -- which the firmware checks rather than trusts, because
     * two controls on one number would be one control doing two things.
     */
    K100_VIA_LOCK_FIRST = 16,
    /**
     * How fast the lighting and the main loop are running, as text.
     *
     * After the locks' run rather than in the gap the zones left, because a value that moves
     * when a zone is added is a value the definition and the firmware would have to be kept
     * in step about by hand. See lighting_rate.c for what the two numbers are.
     */
    K100_VIA_LIGHTING_RATE = 22,
    /** Whether a reactive effect lights a key in what it is painted or in one chosen colour. */
    K100_VIA_REACTION_SOURCE = 23,
    /** That one colour, for when it is the one chosen. */
    K100_VIA_REACTION_COLOUR = 24,
};

/**
 * @brief   Asks the configuration application to read one of its values again.
 *
 * @details For the state this keyboard changes by itself. VIA reads the values
 *          its menus show when it connects and afterwards knows only what it
 *          wrote, so a key or a wheel that changes one has to say so or the
 *          application's idea of the keyboard quietly stops being true.
 *
 *          The channel and value are the numbers the application sends to ask
 *          for that value, from `enum via_channel_id` and its per-channel
 *          counterparts in via.h. See via_ui_sync.c.
 */
void k100_via_sync_value(uint8_t channel, uint8_t value);

/** How many LEDs carry a colour of their own: every LED the firmware has. */
#define K100_PER_KEY_LEDS 190

/**
 * One LED's stored colour, including how bright it is.
 *
 * The brightness is what makes a single key dark, which is the one thing a hue
 * and a saturation cannot say: zero saturation is white, not off. It scales the
 * one the wheel sets rather than replacing it, so the wheel still dims the whole
 * keyboard and a key set to half stays half of whatever that is.
 */
typedef struct {
    uint8_t hue;
    uint8_t sat;
    uint8_t val;
} k100_colour_t;

/** @brief What the key painter has put on this LED. See per_key_colour.c. */
k100_colour_t k100_per_key_colour(uint8_t led);

/**
 * @brief   Answers VIA's per-key colour on channel zero, value one.
 *
 * @details Both take the command's payload from its index byte on, which is
 *          where the application puts the LED and, for a write, the colour.
 */
void k100_per_key_colour_get(uint8_t *value_data);
void k100_per_key_colour_set(uint8_t *value_data);

/**
 * @brief   The brightness a painted key is given, which VIA cannot send.
 *
 * @details Its palette carries a hue and a saturation and nothing else, so the
 *          brightness is a setting of the brush rather than of the stroke: it
 *          is picked once and every key painted after it takes it. Off is the
 *          bottom of the same control, because a key that is off is a key whose
 *          brightness is zero and no second mechanism is needed to say so.
 */
void k100_paint_value_get(uint8_t *value_data);
void k100_paint_value_set(uint8_t *value_data);

/**
 * @brief   One whole light bar, the wheel's ring or the logo, in one colour.
 *
 * @details What VIA's painter cannot reach, because it paints keys and these have no key to
 *          click on. The zone is an index into the generated table in led_zones.h, in the
 *          order the menu offers them; the colour arrives and is read back the way a painted
 *          key's is, and takes the same brush brightness.
 */
void k100_zone_colour_get(uint8_t zone, uint8_t *value_data);
void k100_zone_colour_set(uint8_t zone, uint8_t *value_data);

/**
 * @brief   How bright one whole zone is, applied as it is set.
 *
 * @details A zone's own, not the brush's: a bar is one thing, and a brightness that only
 *          took effect the next time its colour was set would be a control that does
 *          nothing when it is used. Zero is off, and the colour the zone is set to survives
 *          being turned off and on again.
 */
void k100_zone_value_get(uint8_t zone, uint8_t *value_data);
void k100_zone_value_set(uint8_t zone, uint8_t *value_data);

/**
 * @brief   The colour a key shows while its lock is on, and how bright.
 *
 * @details The lock is an index into the generated table in lock_leds.h. Brightness zero is
 *          how someone says they would rather the key did not show it at all, which is why
 *          the pair is the same shape as a zone's.
 */
void k100_lock_colour_get(uint8_t lock, uint8_t *value_data);
void k100_lock_colour_set(uint8_t lock, uint8_t *value_data);
void k100_lock_value_get(uint8_t lock, uint8_t *value_data);
void k100_lock_value_set(uint8_t lock, uint8_t *value_data);

/** @brief What to paint on a lock's key, as the colour it is stored with. */
k100_colour_t k100_lock_colour(uint8_t lock);

/**
 * @brief   What a key lights up in when it is pressed under a reactive effect.
 *
 * @details Either what the key painter put on it -- which is the default, and makes a painted
 *          keyboard react in its own colours -- or the one colour chosen for the reaction.
 *          Its brightness is the painted one either way, because that is how a single key is
 *          turned off and a key that is off should stay off. See per_key_colour.c.
 */
k100_colour_t k100_reaction_colour(uint8_t led);

void k100_reaction_source_get(uint8_t *value_data);
void k100_reaction_source_set(uint8_t *value_data);
void k100_reaction_colour_get(uint8_t *value_data);
void k100_reaction_colour_set(uint8_t *value_data);

/**
 * @brief   Counts one finished lighting frame. See lighting_rate.c.
 *
 * @details Called from the indicator hook, which rgb_matrix runs exactly once per frame --
 *          after the last slice has been worked out and before it is sent to the drivers. A
 *          frame that is not rendered is not counted, so the rate falls to zero when the
 *          lighting is off, which is the truth about it.
 */
void k100_frame_rendered(void);

/** @brief The frame and scan rates, as the line of text VIA displays. */
void k100_lighting_rate_get(uint8_t *value_data);

/**
 * Lights the one LED the probe names, if it is on.
 *
 * Called from the indicator hook rather than from an effect, so that it shows
 * under any lighting including none; see via_values.c for what it is for.
 */
void k100_led_probe_paint(void);

/*
 * Matrix internals shared with the bootloader-entry check.
 *
 * The pin numbers, pad modes and timing all stay in matrix.c, which owns them.
 * What the bootloader check needs is the ability to read one row before QMK has
 * initialised the matrix, so these two are exposed rather than duplicated.
 */

/** Configures the strobes as outputs and the senses as pulled-up inputs. */
void k100_matrix_configure_pins(void);

/**
 * Asserts one strobe, waits out the settling time and returns the raw senses.
 *
 * Raw in both senses that matter: no debounce, and no polarity correction, so
 * bit N is the level on sense N rather than "key N is pressed". The bootloader
 * check wants exactly that, because it works out the pressed level for itself
 * instead of trusting MATRIX_SENSE_ACTIVE_LOW.
 */
uint32_t k100_matrix_sample_row(uint8_t row);

/**
 * Writes Corsair's operating mode and restarts into the bootloader.
 *
 * Does not return on success. Returns false if the EEPROM word could not be
 * programmed, in which case resetting would only loop back here -- see the
 * read-back in bootloader.c. Also reached through QMK's QK_BOOT keycode, which
 * calls bootloader_jump().
 */
bool k100_enter_bootloader(void);

#ifdef K100_USB_DIAGNOSTIC
/**
 * Marks the first matrix scan of the main loop; see bootloader.c. Diagnostic
 * only, and compiled out with the rest of the delay channel.
 */
void k100_diagnostic_mark_first_scan(void);
#endif
