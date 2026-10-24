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
     * The first of the zone colours, one per light that is not a key.
     *
     * A run rather than a value each: the zones are generated from the measured LED table
     * into led_zones.h, and the definition numbers its colour controls from here in the
     * same order, so neither side carries a list the other has to agree with. The numbers
     * after this one up to K100_ZONE_COUNT are therefore taken.
     */
    K100_VIA_ZONE_FIRST = 6,
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
