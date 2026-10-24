// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * How fast the lighting and the keyboard are actually running.
 *
 * They are the same loop. Once around it, QMK scans the matrix, hands what
 * changed to the host and works out a fifth of the next lighting frame -- so an
 * effect that takes longer to work out does not drop frames, it slows the loop,
 * and the keyboard types later. "It feels smooth" is not a measurement of that,
 * and this keyboard has less headroom than most: the matrix alone costs four
 * milliseconds a scan, because an optical switch needs half a millisecond per
 * strobe before the sense line means anything.
 *
 * So two counters and a clock, and both numbers are worth having side by side:
 *
 *   the scan rate   how often the main loop goes round, which is what typing
 *                   latency is made of
 *   the frame rate  how often a whole frame reaches the LED drivers, which is
 *                   what smooth means
 *
 * Read together they also say *which* limit is biting. If an effect gets dearer
 * and both numbers fall, the arithmetic is the cost. If the frame rate sits
 * still while the scan rate falls, frames are being paced by rgb_matrix's own
 * flush limit and the effect is free. Neither number alone can tell those apart.
 *
 * And a third number, which is what makes the first two mean anything for the
 * reactive effects: how many presses rgb_matrix is still remembering. Those
 * effects do their work per press, so on a keyboard nobody is touching they do
 * none at all and measuring one idle would flatter it. The ring holds eight and
 * keeps a press for a minute, so the worst case is eight keys tapped once --
 * which is only a worst case if the number says eight while it is being read.
 *
 * Reported as one line of text on this keyboard's own VIA channel, because VIA
 * renders a `label` as whatever the keyboard returns -- the same way the version
 * is shown; see via_values.c. Reading it costs nothing that is being measured:
 * the rates are divided out once a second by the loop itself, and a read copies
 * out six characters and some text.
 */

#include "quantum.h"

#include "k100.h"

/*
 * How long each rate is averaged over.
 *
 * Long enough that one slow trip round the loop does not show in the number, and
 * short enough that switching effects in the application and looking at it is
 * one motion rather than two.
 */
#define K100_RATE_WINDOW_MS 1000

static uint16_t scans;
static uint16_t frames;

static uint16_t scan_rate;
static uint16_t frame_rate;

static uint32_t window_start;
static bool     measuring;

/**
 * @brief   Turns a window's worth of counting into two rates, once it is full.
 *
 * @details Divided by the time that actually elapsed rather than by the window,
 *          because the window closes on the first scan after it expires and that
 *          scan is up to four milliseconds late. Rounding a measurement to the
 *          interval it was meant to be taken over is how an instrument comes to
 *          agree with its own expectations.
 */
static void close_window(void) {
    if (!measuring) {
        measuring    = true;
        window_start = timer_read32();
        return;
    }

    const uint32_t elapsed = timer_elapsed32(window_start);

    if (elapsed < K100_RATE_WINDOW_MS) {
        return;
    }

    scan_rate  = (uint16_t)((uint32_t)scans * 1000 / elapsed);
    frame_rate = (uint16_t)((uint32_t)frames * 1000 / elapsed);

    scans        = 0;
    frames       = 0;
    window_start = timer_read32();
}

/*
 * The main loop's own hook, called once per scan whether or not anything was
 * pressed -- which is what makes counting here the loop's rate and not the
 * typing's.
 */
void matrix_scan_kb(void) {
    scans++;
    close_window();

    matrix_scan_user();
}

void k100_frame_rendered(void) {
    frames++;
}

/** Decimal, by hand: a formatter for two numbers is not worth linking printf for. */
static uint8_t *put_number(uint8_t *at, uint16_t value) {
    uint8_t digits[5];
    uint8_t count = 0;

    do {
        digits[count++] = '0' + value % 10;
        value /= 10;
    } while (value > 0);

    while (count > 0) {
        *at++ = digits[--count];
    }

    return at;
}

static uint8_t *put_text(uint8_t *at, const char *text) {
    while (*text != '\0') {
        *at++ = (uint8_t)*text++;
    }

    return at;
}

void k100_lighting_rate_get(uint8_t *value_data) {
    uint8_t *at = value_data;

    at = put_number(at, frame_rate);
    at = put_text(at, " fps, ");
    at = put_number(at, scan_rate);
    at = put_text(at, " Hz, ");
    at = put_number(at, g_last_hit_tracker.count);
    at = put_text(at, " hits");

    *at = '\0';
}
