// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Telling the application that the keyboard changed something itself.
 *
 * VIA reads the values its menus show once, when it connects, and after that it
 * knows only what it wrote itself. Everything this keyboard does on its own --
 * the wheel button stepping the effect, the wheel turning setting the
 * brightness -- therefore happens behind the application's back: its dropdown
 * goes on naming the effect that was running when the menu loaded, and the next
 * change made there is applied on top of a state it no longer knows.
 *
 * The application has a way out of this, and it is the keyboard's to use. VIA
 * passes every report that arrives on its own -- one that is not the answer to
 * a command it sent -- through a handler that looks for a **UI sync request**:
 * report 0x16, version 1, then which menu values to read again. Recognising one
 * consumes the report, so it never reaches the code that matches replies to
 * commands, and the application re-reads exactly the values named.
 *
 * Read out of the application rather than out of a specification, because there
 * is none: QMK's via.h stops at 0x15 and knows nothing of this, and
 * `parseUISyncRequest` in VIA's own bundle is what the layout below follows.
 *
 * What an application that does not know the request does with it is the one
 * risk here, and it cannot be avoided from this side -- a keyboard cannot ask
 * which version is listening. One that ignores unknown reports loses nothing.
 * One that is waiting for a reply at that moment would take this for the reply,
 * and be one behind for the rest of its session. Our own tools read a VIA
 * connection the same way, which is why k100_via.py skips these.
 */

#include "quantum.h"
#include "raw_hid.h"
#include "usb_descriptor.h"

#include "k100.h"

/** The report VIA listens for, and the layout it reads it with. */
enum via_ui_sync {
    VIA_UI_SYNC_REQUEST = 0x16,
    VIA_UI_SYNC_VERSION = 0x01,
    /** Read the values these channel and value pairs name, and nothing else. */
    VIA_UI_SYNC_MENU_VALUES = 0x01,
};

void k100_via_sync_value(uint8_t channel, uint8_t value) {
    uint8_t request[RAW_EPSIZE] = {
        VIA_UI_SYNC_REQUEST, VIA_UI_SYNC_VERSION, VIA_UI_SYNC_MENU_VALUES, 1, channel, value,
    };

    /*
     * Dropped rather than waited for when nothing is listening. QMK's endpoint
     * remembers a send that timed out and sends immediately from then on, until
     * one succeeds again -- so a keyboard with no application connected pays
     * for this once instead of on every press. See usb_endpoint_in_send().
     */
    raw_hid_send(request, sizeof(request));
}
