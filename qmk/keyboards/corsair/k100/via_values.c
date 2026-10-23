// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * What this keyboard answers on VIA's own channel.
 *
 * VIA leaves channel zero to the keyboard, and a definition can put any control
 * on it. Two things live here, and both exist because the application has no
 * way to ask for them otherwise.
 *
 * **The version.** VIA never shows a firmware version of its own accord: it
 * asks for one and uses it only to hide menu entries a firmware is too old for.
 * What it does display is a menu item of type `label` bound to a value,
 * rendered as the text the keyboard returns -- so a keyboard that wants to be
 * identified says so in a value of its own. The text is built from
 * `DEVICE_VER`, so the version is still declared exactly once; see
 * tools/generate-qmk-layout.py.
 *
 * **The LED probe**, which lights one LED by its index and nothing else. The
 * index an effect uses and the LED that lights are not known to agree on this
 * keyboard: the channel table comes from Corsair's own firmware and the
 * positions from ckb-next, and the two number the same hardware differently.
 * The right light bar is the proof -- eleven real LEDs that no effect reaches,
 * because they sit where this layout keeps its unplaced entries. Finding what
 * sits where needs a way to light one and look, which is this.
 */

#include "quantum.h"
#include "via.h"

#include "k100.h"

/** Set from VIA, kept nowhere: a diagnostic that outlived a power cycle would
    be a keyboard with one white LED and no explanation for it. */
static bool    probe_lit   = false;
static uint8_t probe_index = 0;

/**
 * @brief   Writes the version as NUL-terminated text, which is what VIA decodes.
 *
 * @details `DEVICE_VER` is binary-coded decimal -- two digits for the leading
 *          number and one each for the other two -- which is why the digits are
 *          picked out rather than printed: a formatter would pull in printf for
 *          six characters.
 */
static void describe_version(uint8_t *out) {
    const uint8_t major = ((DEVICE_VER >> 12) & 0xf) * 10 + ((DEVICE_VER >> 8) & 0xf);
    uint8_t       at    = 0;

    if (major >= 10) {
        out[at++] = '0' + major / 10;
    }
    out[at++] = '0' + major % 10;
    out[at++] = '.';
    out[at++] = '0' + ((DEVICE_VER >> 4) & 0xf);
    out[at++] = '.';
    out[at++] = '0' + (DEVICE_VER & 0xf);
    out[at]   = '\0';
}

void k100_led_probe_paint(void) {
#ifdef RGB_MATRIX_ENABLE
    if (probe_lit) {
        // Painted after the effect, like the status icons, so it shows whatever
        // the lighting is doing -- including nothing at all.
        rgb_matrix_set_color(probe_index, UINT8_MAX, UINT8_MAX, UINT8_MAX);
    }
#endif
}

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    (void)length;

    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id = &data[0];
    uint8_t *channel_id = &data[1];
    uint8_t *value_id   = &data[2];
    uint8_t *value_data = &data[3];

    if (*channel_id != id_custom_channel) {
        *command_id = id_unhandled;
        return;
    }

    switch (*command_id) {
        case id_custom_get_value:
            switch (*value_id) {
                case K100_VIA_VERSION:
                    describe_version(value_data);
                    return;
                case K100_VIA_LED_PROBE:
                    value_data[0] = probe_lit ? 1 : 0;
                    return;
                case K100_VIA_LED_INDEX:
                    value_data[0] = probe_index;
                    return;
            }
            break;

        case id_custom_set_value:
            switch (*value_id) {
                // The version is not ours to be told.
                case K100_VIA_LED_PROBE:
                    probe_lit = value_data[0] != 0;
                    return;
                case K100_VIA_LED_INDEX:
                    probe_index = value_data[0];
                    return;
            }
            break;

        case id_custom_save:
            // Answered rather than refused. Nothing on this channel is stored,
            // and the application sends a save after every change -- refusing
            // it would fill its error log with failures that are not failures.
            return;
    }

    *command_id = id_unhandled;
}
