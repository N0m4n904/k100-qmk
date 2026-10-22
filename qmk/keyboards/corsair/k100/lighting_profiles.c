// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The lighting the wheel button steps through.
 *
 * The button in the middle of the iCUE wheel carried the same keycode as the
 * mute key beside it -- the generator's default for a key with no HID meaning
 * of its own, which left two keys doing one thing and the wheel button doing
 * nothing of its own.
 *
 * Which profile is showing is read back out of rgb_matrix rather than kept
 * here. A copy would have to survive a reflash and a settings reset to keep
 * agreeing with what the keyboard is actually doing, and rgb_matrix already
 * stores the mode and the colour -- so a press works out where it is from what
 * is on, and moves on from there. Nothing to keep in step, and nothing to be
 * wrong after someone sets a colour from VIA.
 */

#include "quantum.h"

#include "k100.h"

#ifdef RGB_MATRIX_ENABLE

/** @brief One profile: a mode, and the colour it is driven with. */
typedef struct {
    uint8_t mode;
    uint8_t hue;
    uint8_t sat;
} k100_profile_t;

/*
 * White, red, rainbow.
 *
 * The rainbow is CYCLE_ALL, which moves the whole keyboard through the hues
 * together rather than spreading them across it. That is not a matter of
 * taste. A spread one reads each LED's position, and the positions this
 * keyboard carries do not line up with the LEDs they are attached to -- the
 * geometry is dense over what ckb-next places while the channel table is
 * indexed by the firmware's own map, and docs/pin-mapping.md has the
 * measurements. CYCLE_ALL reads no position, so it is right whatever that
 * turns out to be; a spread rainbow is worth having once it is settled.
 */
static const k100_profile_t profiles[] = {
    {RGB_MATRIX_SOLID_COLOR, 0, 0},
    {RGB_MATRIX_SOLID_COLOR, 0, UINT8_MAX},
    {RGB_MATRIX_CYCLE_ALL, 0, UINT8_MAX},
};

/**
 * @brief   The profile a press moves to.
 *
 * @details The one after whichever is showing, or the first if none of them
 *          is -- which is where a settings reset leaves it, and where any
 *          colour set from VIA leaves it too.
 *
 *          Brightness is not compared, and is not part of a profile at all.
 *          The wheel is meant to set it, and a profile that carried one would
 *          undo that on every press.
 */
static uint8_t next_profile(void) {
    const uint8_t mode = rgb_matrix_get_mode();
    const hsv_t   hsv  = rgb_matrix_get_hsv();

    for (uint8_t i = 0; i < ARRAY_SIZE(profiles); i++) {
        if (profiles[i].mode == mode && profiles[i].hue == hsv.h && profiles[i].sat == hsv.s) {
            return (i + 1) % ARRAY_SIZE(profiles);
        }
    }

    return 0;
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (keycode == K100_RGB_PROFILE && record->event.pressed) {
        const uint8_t          brightness = rgb_matrix_get_hsv().v;
        const k100_profile_t  *profile    = &profiles[next_profile()];

        rgb_matrix_sethsv(profile->hue, profile->sat, brightness);
        rgb_matrix_mode(profile->mode);
        return false;
    }

    return process_record_user(keycode, record);
}

#endif /* RGB_MATRIX_ENABLE */
