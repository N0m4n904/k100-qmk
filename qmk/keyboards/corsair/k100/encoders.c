// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The two things on this keyboard that turn.
 *
 * The iCUE wheel sets the lighting's brightness, which is why the lighting
 * profiles beside it carry no brightness of their own -- a profile that did
 * would undo this on every press. The volume roller does what it is printed
 * to do.
 *
 * **Which of the two is which is not yet established.** The pins are:
 * P1_28/P1_29 and P1_30/P1_31, read out of the stock firmware's INPUTMUX
 * configuration, where they are attached to four pin interrupts and sit
 * outside the matrix. That is solid. Which pair is the wheel and which the
 * roller is not, and neither is the direction, because both need someone to
 * turn one and say what happened. docs/pin-mapping.md has the measurements
 * and the test.
 *
 * So the assignment below is a guess that costs one turn to check, and
 * swapping it is swapping two cases.
 */

#include "quantum.h"

enum k100_encoder {
    K100_ENCODER_WHEEL = 0,
    K100_ENCODER_ROLLER,
};

bool encoder_update_kb(uint8_t index, bool clockwise) {
    if (!encoder_update_user(index, clockwise)) {
        return false;
    }

    switch (index) {
        case K100_ENCODER_WHEEL:
#ifdef RGB_MATRIX_ENABLE
            if (clockwise) {
                rgb_matrix_increase_val();
            } else {
                rgb_matrix_decrease_val();
            }
#endif
            return false;

        case K100_ENCODER_ROLLER:
            tap_code(clockwise ? KC_VOLU : KC_VOLD);
            return false;
    }

    return true;
}
