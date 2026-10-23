// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * What this firmware tells the application about itself.
 *
 * VIA never shows a firmware version of its own accord. It asks for one, keeps
 * it, and uses it only to decide which menu entries a given firmware is allowed
 * to show -- there is no field anywhere in the application that draws it. What
 * the application *does* draw is a menu item of type `label` bound to a value,
 * rendered as the text the keyboard returns for it. So a keyboard that wants to
 * be identified has to say so in a value of its own, on the channel VIA leaves
 * to keyboards.
 *
 * The text is built from `DEVICE_VER` rather than written out again, so this
 * firmware's version is still declared exactly once; see
 * tools/generate-qmk-layout.py, which puts the same number into the USB
 * descriptor, into VIA's firmware version and into the container header.
 */

#include "quantum.h"
#include "via.h"

#include "k100.h"

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

void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    (void)length;

    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id = &data[0];
    uint8_t *channel_id = &data[1];
    uint8_t *value_id   = &data[2];
    uint8_t *value_data = &data[3];

    if (*command_id == id_custom_get_value && *channel_id == id_custom_channel &&
        *value_id == K100_VIA_VERSION) {
        describe_version(value_data);
        return;
    }

    // Everything else, including an attempt to write this, is answered the way
    // QMK answers a command it does not know. The version is not ours to be
    // told.
    *command_id = id_unhandled;
}
