// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The two things on this keyboard that turn.
 *
 * The iCUE wheel sets the lighting's brightness, which is why the button in the
 * middle of it steps the effect and leaves the brightness alone -- a press that
 * set one would undo this. The volume roller does what it is printed to do.
 *
 * The pins are P1_28/P1_29 and P1_30/P1_31, read out of the stock firmware's
 * INPUTMUX configuration, where they are attached to four pin interrupts and
 * sit outside the matrix.
 *
 * **Which pair is which was measured by turning them**, and the pairing below
 * is the one the hardware has: the first pair is the wheel and the second the
 * roller. The wheel's directions came out right the first time; the roller's
 * did not, which is why the two cases below are not written the same way.
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
            /* Rolled away from the user, which is the direction that has to
               raise the volume, this one reports anticlockwise. Measured; it
               was the other way round here first and came out inverted on the
               keyboard. QMK's ENCODER_DIRECTION_FLIP would turn both round,
               and the wheel above is already right. */
            tap_code(clockwise ? KC_VOLD : KC_VOLU);
            return false;
    }

    return true;
}
