// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The two keys that drive the lighting, and why neither of them owns a colour.
 *
 * The button in the middle of the iCUE wheel and the profile-switch key beside
 * it have no HID meaning of their own. Both carried the keycode of the key next
 * to them -- the generator's default for a key it cannot name -- which left two
 * keys doing one thing and these two doing nothing, so what they do is decided
 * here.
 *
 * Neither keeps any state of its own beyond the one thing that cannot be read
 * back. What is showing is asked of rgb_matrix, because VIA writes the same
 * state from the other side: a copy kept here would be wrong the moment the
 * application set an effect, and it would have to survive a reflash and a
 * settings reset to stay in step.
 *
 * **The wheel button steps the effect and nothing else.** It used to step
 * through three profiles -- white, red, rainbow -- which set the colour as
 * well, and that fought the application over one piece of state. Two ways, both
 * seen on the keyboard: a press overwrote a colour set in VIA, and a colour set
 * in VIA left the state matching no profile at all, so the next press fell back
 * to the first one and the keyboard went white. The effect is this key's to
 * step. The colour is the application's to set, for every effect that reads
 * one -- which is all of them but the black one, because the rainbows take
 * their saturation from it and paint white without it.
 */

#include "quantum.h"

#include "k100.h"

#ifdef RGB_MATRIX_ENABLE

/*
 * What the lighting was showing before it was turned off.
 *
 * The one thing here that cannot be read back: once the lighting is off, the
 * mode that is showing *is* the off effect, and nothing anywhere remembers what
 * it replaced. Mode zero never appears here -- it is RGB_MATRIX_NONE, which
 * this keyboard never selects, see rgb_matrix_kb.inc -- so it is free to mean
 * "nothing remembered", and a press after a restart lands on the plain colour
 * instead.
 *
 * Deliberately not stored in EEPROM. The mode itself already is, so a keyboard
 * that was off when it was unplugged comes back off, which is what was asked of
 * it; what it came from is not worth a write on every press.
 */
static uint8_t before_off = 0;

/**
 * @brief   The next effect, skipping the one that paints black.
 *
 * @details `rgb_matrix_step()` walks the effects the firmware was *built* with
 *          and wraps past the last one, which is exactly the list VIA's
 *          dropdown holds -- so the key and the application agree on what there
 *          is to step through without either of them carrying a list of it.
 *
 *          The black effect is skipped because turning the lighting off has a
 *          key of its own: a step that landed there would read as a press that
 *          did nothing. Stepping *from* it is how it is left, so the wheel
 *          brings the lighting back as well.
 */
static void step_effect(void) {
    rgb_matrix_step();
    if (rgb_matrix_get_mode() == RGB_MATRIX_CUSTOM_K100_LIGHTS_OFF) {
        rgb_matrix_step();
    }
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (keycode == K100_RGB_EFFECT && record->event.pressed) {
        step_effect();
        return false;
    }

    if (keycode == K100_LIGHTS_TOGGLE && record->event.pressed) {
        if (rgb_matrix_get_mode() == RGB_MATRIX_CUSTOM_K100_LIGHTS_OFF) {
            rgb_matrix_mode(before_off ? before_off : RGB_MATRIX_SOLID_COLOR);
        } else {
            before_off = rgb_matrix_get_mode();
            rgb_matrix_mode(RGB_MATRIX_CUSTOM_K100_LIGHTS_OFF);
        }
        return false;
    }

    return process_record_user(keycode, record);
}

#endif /* RGB_MATRIX_ENABLE */
