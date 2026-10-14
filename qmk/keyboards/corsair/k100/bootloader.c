// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Getting back to Corsair's bootloader.
 *
 * This is not a convenience. QMK does not speak Bragi, so the moment this
 * firmware runs there is no vendor interface left for k100-bragi to talk to and
 * no stock application to do the flashing. Without a route back, the first
 * custom image written to this keyboard is permanent whether or not it works --
 * a perfectly good build that cannot be replaced is as much a dead end as one
 * that does not boot.
 *
 * The route is Corsair's own, recovered from the stock application image: its
 * SET OPERATING_MODE handler writes the mode to word 2 of the on-chip EEPROM
 * and then soft-resets, and the bootloader reads that word back on startup. So
 * any firmware that can program one EEPROM word can hand control back, with no
 * dependency on USB, on the matrix, or on anything else here being correct.
 * docs/recovery.md records how that was established, register by register.
 *
 * Three routes are provided, because each one depends on a different part of
 * this firmware being right:
 *
 *   QK_BOOT             needs USB and the matrix          an ordinary reflash
 *   Escape at power-on  needs the matrix read only        USB broken
 *   USB watchdog        needs neither                     firmware unusable
 *
 * The last two matter most for a first flash, because they survive this project
 * being wrong about the things it is still guessing at.
 */

#include "quantum.h"
#include "usb_device_state.h"

#include "lpc546xx_eeprom.h"
#include "k100.h"

/*
 * Corsair's operating mode, in EEPROM word 2.
 *
 * Both the index and the value are the stock firmware's: its setter writes the
 * mode to word 2 and its getter reads word 2, and the mode enumeration is the
 * one the updater bundle declares -- SELF_OPERATED 1, HOST_CONTROLLED 2,
 * BOOTLOADER 3.
 */
#define EEPROM_WORD_OPERATING_MODE 2U
#define OPERATING_MODE_BOOTLOADER  3U

/*
 * Where Escape sits in the matrix, from the generated layout: row 0, column 1.
 */
#define ESCAPE_ROW 0U
#define ESCAPE_COL 1U

/*
 * How long to wait for USB before concluding this firmware is unusable.
 *
 * Generous on purpose. A host that is slow to enumerate, or a hub that resets
 * the port, must not be mistaken for broken firmware -- a false trigger costs a
 * trip through the bootloader, which is recoverable but tedious. Ten seconds is
 * far longer than enumeration takes and far shorter than anyone's patience.
 */
#ifndef K100_USB_WATCHDOG_MS
#    define K100_USB_WATCHDOG_MS 10000
#endif

void k100_enter_bootloader(void) {
    lpcEepromWrite(EEPROM_WORD_OPERATING_MODE, OPERATING_MODE_BOOTLOADER);

    // The stock firmware defers this by 10 ms through a timer, which it needs
    // because it is answering a USB request and wants the reply out first.
    // Nothing here is waiting on a reply.
    NVIC_SystemReset();

    while (true) {
        // NVIC_SystemReset does not return; this only keeps the compiler from
        // deciding the function falls through.
    }
}

/*
 * QMK's hooks for `bootloader: custom`. Both are weak and empty by default.
 *
 * mcu_reset() is a plain restart, and on this part a plain restart lands back
 * in whatever the EEPROM already says -- normally this firmware again, which is
 * what a reset should do.
 */
void bootloader_jump(void) {
    k100_enter_bootloader();
}

void mcu_reset(void) {
    NVIC_SystemReset();
}

/**
 * Is Escape held, whichever way round the optical switches conduct?
 *
 * The polarity of an OPX switch is still a hypothesis in this project --
 * MATRIX_SENSE_ACTIVE_LOW is one define with a long comment above it -- and an
 * escape hatch that depends on an untested guess is not an escape hatch. So
 * this does not ask "does Escape read low". It asks whether Escape disagrees
 * with the rest of its row.
 *
 * Row 0 carries seventeen of the eighteen senses, so with nothing held the
 * populated keys all read the same level and Escape agrees with them. Holding
 * Escape alone makes it the one that differs, under either polarity and either
 * pull direction. The single unpopulated column is one vote among seventeen and
 * cannot swing the majority.
 *
 * It fails safe in the direction that matters: if the reading is uniform --
 * including the case where the matrix does not work at all and every sense
 * reads alike -- nothing is held and the keyboard boots normally.
 */
static bool escape_held(void) {
    k100_matrix_configure_pins();

    const uint32_t senses = k100_matrix_sample_row(ESCAPE_ROW);
    const uint32_t others = senses & ~(1UL << ESCAPE_COL) & ((1UL << MATRIX_COLS) - 1UL);

    const unsigned high  = (unsigned)__builtin_popcount(others);
    const unsigned total = MATRIX_COLS - 1U;

    const bool released_level = (high * 2U) > total;
    const bool escape_level   = (senses & (1UL << ESCAPE_COL)) != 0UL;

    return escape_level != released_level;
}

void keyboard_pre_init_kb(void) {
    /*
     * Deliberately here rather than in one of QMK's earlier hooks. Programming
     * the EEPROM needs its clock divider set from the real system clock, and
     * the system clock is only what it is supposed to be once halInit() has run
     * -- which is after __early_init and before this. This is the first hook
     * where the write would be correct.
     *
     * It is also still early enough to be useful: the matrix and the LED driver
     * are initialised after this, and those are the parts most likely to hang
     * on hardware this project has not fully mapped.
     */
    if (escape_held()) {
        k100_enter_bootloader();
    }

    keyboard_pre_init_user();
}

void housekeeping_task_kb(void) {
    static uint32_t first_task_at = 0;
    static bool     usb_was_ready = false;

    if (usb_was_ready) {
        housekeeping_task_user();
        return;
    }

    /*
     * QMK's own view of the USB state rather than the ChibiOS driver's, so this
     * says nothing about which USB stack is underneath. Suspend counts as
     * success: the state machine only reaches it by way of configured, so a
     * host that enumerated us and then slept has already proved the point.
     */
    const usb_configure_state_t usb = usb_device_state_get_configure_state();
    if (usb == USB_DEVICE_STATE_CONFIGURED || usb == USB_DEVICE_STATE_SUSPEND) {
        usb_was_ready = true;
        housekeeping_task_user();
        return;
    }

    if (first_task_at == 0) {
        // timer_read32() can legitimately return zero; one tick's delay in
        // starting the clock is irrelevant over ten seconds, and this keeps the
        // state to one variable rather than two.
        first_task_at = timer_read32() | 1U;
    } else if (timer_elapsed32(first_task_at) > K100_USB_WATCHDOG_MS) {
        /*
         * The firmware runs but never became usable. Hand the keyboard back to
         * the bootloader rather than leaving it enumerating as nothing, which
         * is the state no tool in this project can recover from.
         */
        k100_enter_bootloader();
    }

    housekeeping_task_user();
}
