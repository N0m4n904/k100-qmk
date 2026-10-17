// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * K100 key matrix.
 *
 * Eight strobes against eighteen senses, all on GPIO port 1, exactly as the
 * stock firmware drives them. Every pin number, pad configuration and index
 * below comes from disassembling the official image; docs/pin-mapping.md shows
 * the instructions each one was read from.
 *
 * Custom rather than QMK's generic scan for one reason worth stating: all
 * eighteen senses live in the low bits of a single port, so one 32-bit port
 * read samples the whole row. QMK's generic matrix would issue eighteen
 * per-pin reads instead. The hardware was laid out to be read this way, and
 * the stock firmware reads it this way too.
 */

#include "matrix.h"
#include "quantum.h"

#include "k100.h"

/*
 * Strobes occupy P1_18..P1_25 and senses P1_0..P1_17, which is what makes the
 * single-read trick possible: the senses are a contiguous block from bit 0.
 */
#define STROBE_FIRST_PAD 18U
#define SENSE_MASK       ((uint32_t)((1UL << MATRIX_COLS) - 1UL))

/*
 * Pad configuration, matched bit-for-bit to the stock firmware's IOCON words
 * rather than merely being equivalent -- it is the one configuration known to
 * work on this hardware.
 *
 *   strobes  0x300  digital, input filter off, no pull, push-pull output
 *   senses   0x120  digital, pull-up, input
 *
 * The firmware can also run the senses at 0x100, with no pull, selected by a
 * flag in its SRAM. Pull-ups are used here because an undriven sense line has
 * to read as released, and with no pull it would float.
 */
#define STROBE_MODE (PAL_MODE_OUTPUT_PUSHPULL | MODE_FILTER_OFF)
#define SENSE_MODE  PAL_MODE_INPUT_PULLUP

/*
 * Whether a pressed key reads low.
 *
 * The electrical arrangement says it should: a strobe is asserted by driving it
 * low, the senses are pulled up, so a conducting switch pulls its sense down.
 * That is how the pads are configured, which is established.
 *
 * What is *not* established is which way round an OPX optical switch conducts.
 * These are not metal contacts -- each key interrupts an infrared beam -- and
 * whether the beam is broken by pressing or released by it cannot be read out
 * of the firmware's pin setup. The stock scan loop stores its samples without
 * inverting them, which settles nothing either, since whatever consumes them
 * is free to invert instead.
 *
 * So this is a hypothesis, and it is one define wide on purpose: if every key
 * reports held and releases when pressed, invert it. Nothing else needs to
 * change.
 */
#ifndef MATRIX_SENSE_ACTIVE_LOW
#    define MATRIX_SENSE_ACTIVE_LOW TRUE
#endif

/*
 * Settle time between asserting a strobe and sampling the senses.
 *
 * The stock firmware does not assert and sample in one go: its scan is a
 * four-state machine, and the state that samples is not the state that
 * asserted, so at least one timer tick passes between the two. That is what
 * optical sensing needs -- an emitter has to be on, and its phototransistor
 * settled, before the reading means anything -- and it is the clearest hint the
 * disassembly gives about timing. The tick length was not recovered, so this
 * is a guess on the generous side; a matrix that misses keys or reports
 * neighbours wants it raised before anything else is suspected.
 */
#ifndef MATRIX_STROBE_SETTLE_US
#    define MATRIX_STROBE_SETTLE_US 20
#endif

static pin_t strobe_line(uint8_t row) {
    return LPC_LINE(1U, STROBE_FIRST_PAD + row);
}

void k100_matrix_configure_pins(void) {
    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        // Released before becoming an output, so that making it one cannot
        // assert a strobe for however long the two writes take.
        palSetLine(strobe_line(row));
        palSetLineMode(strobe_line(row), STROBE_MODE);
        palSetLine(strobe_line(row));
    }

    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        palSetLineMode(LPC_LINE(1U, col), SENSE_MODE);
    }
}

uint32_t k100_matrix_sample_row(uint8_t row) {
    palClearLine(strobe_line(row));
    wait_us(MATRIX_STROBE_SETTLE_US);

    // One read for all eighteen senses. See the note at the top.
    uint32_t sensed = palReadPort(IOPORT1) & SENSE_MASK;

    palSetLine(strobe_line(row));
    return sensed;
}

void matrix_init_custom(void) {
    k100_matrix_configure_pins();
}

bool matrix_scan_custom(matrix_row_t current_matrix[]) {
#ifdef K100_USB_DIAGNOSTIC
    k100_diagnostic_mark_first_scan();
#endif

    bool changed = false;

    for (uint8_t row = 0; row < MATRIX_ROWS; row++) {
        uint32_t sensed = k100_matrix_sample_row(row);

#if MATRIX_SENSE_ACTIVE_LOW == TRUE
        sensed = ~sensed & SENSE_MASK;
#endif

        matrix_row_t pressed = (matrix_row_t)sensed;
        if (current_matrix[row] != pressed) {
            current_matrix[row] = pressed;
            changed             = true;
        }
    }

    return changed;
}
