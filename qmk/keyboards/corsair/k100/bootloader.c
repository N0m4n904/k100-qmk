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
 * Row 0 has no key on sense 14, from the generated layout. It is left out of the
 * unanimity test below, because a sense with nothing attached to it cannot be
 * expected to follow the ones that have.
 */
#define ESCAPE_ROW_UNUSED_COL 14U

/*
 * How many agreeing samples it takes. Each costs one strobe and the settling
 * wait, so a handful is microseconds and rules out a single bad read.
 */
#define ESCAPE_SAMPLES 4U

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

/*
 * Reporting a few bits of register state by how long the keyboard stays away.
 *
 * Bringing USB up on this board is being done without a debug probe, and a
 * controller that never connects produces no observable at all: the host logs
 * nothing, so every hypothesis costs a whole flash cycle to test and returns one
 * bit. This is the way out of that. The firmware already decides when to hand
 * itself back to the bootloader, so the *delay* before it does is a channel the
 * host can read by watching the bus, and a few bits at a time is enough to tell
 * which of several guesses is true.
 *
 * Enable it by defining K100_USB_DIAGNOSTIC. It is off by default because it
 * deliberately makes recovery slower, and because a firmware that works has
 * nothing to report.
 */
#ifdef K100_USB_DIAGNOSTIC
/* Reported on the watchdog path: 3 s plus 2 s per unit of the code. */
#    define DIAGNOSTIC_BASE_MS 3000
#    define DIAGNOSTIC_STEP_MS 2000

/* Reported unconditionally, before anything else runs, purely to answer "did
   this firmware execute at all". Long enough that it cannot be confused with
   the two or three seconds a bootloader reboot takes. With the Escape check
   skipped as well, a keyboard that disappears for at least this long has
   certainly run our reset handler, our clock setup and our kernel. */
#    define DIAGNOSTIC_MARKER_MS 20000

/* The earliest marker there is. QMK calls early_hardware_init_pre() from
   __early_init, which crt0 reaches before it fills the stacks, copies .data,
   clears .bss or runs a line of the HAL -- so a delay here distinguishes the two
   remaining explanations for a keyboard that never appears:
   the bootloader is refusing to branch to this image at all, or it branches and
   the image dies before QMK's first hook. A busy loop because there is no kernel
   and no initialised RAM; the count is approximate because the clock is still
   whatever the bootloader left running, which is the point -- it only has to be
   unmistakably long. */
#    define DIAGNOSTIC_EARLY_LOOPS 150000000UL
#endif

bool k100_enter_bootloader(void) {
    if (!lpcEepromWrite(EEPROM_WORD_OPERATING_MODE, OPERATING_MODE_BOOTLOADER)) {
        return false;
    }

    /*
     * Read it back before resetting, because the alternative is a loop. The
     * EEPROM array is ordinary readable memory, so this costs one load -- and
     * all three routes into the bootloader funnel through this one function, so
     * if the write silently does not take they all fail together. Resetting
     * then would come straight back here: the mode word still says
     * self-operated, so the bootloader would start this firmware again, and the
     * watchdog would reset it again ten seconds later, forever.
     *
     * Reporting the failure instead leaves the keyboard in whatever working
     * state it had, which is strictly better than a reboot loop and leaves the
     * other routes something to act on.
     */
    if (lpcEepromRead(EEPROM_WORD_OPERATING_MODE) != OPERATING_MODE_BOOTLOADER) {
        return false;
    }

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
    (void)k100_enter_bootloader();
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
 * Row 0 carries a key on every sense but one, so with nothing held they all read
 * the same level and Escape agrees with them. Holding Escape alone makes it the
 * one that differs, under either polarity and either pull direction.
 *
 * **Every other populated sense has to agree.** An earlier version asked only
 * whether Escape differed from the *majority*, which is far too weak: a reading
 * that is simply wrong -- the wrong pins, a strobe that drives nothing, a bus
 * left floating -- scatters bits arbitrarily, and Escape lands on the minority
 * side of that about half the time. The check then fires on a keyboard nobody
 * touched, and since it fires before USB, the result is a keyboard that boots
 * into the bootloader and looks for all the world like firmware that will not
 * run. Requiring unanimity among the other sixteen costs nothing when a key
 * really is held and rejects a garbage read immediately.
 *
 * Column 14 is excluded because row 0 has no key on it. With pull-ups it reads
 * high regardless, so under the polarity where a released key reads *low* it
 * would disagree with every real sense and veto the check forever.
 *
 * Sampling several times and demanding the same answer each time guards against
 * a single bad read, which costs microseconds here.
 *
 * It fails safe in the direction that matters: anything other than exactly this
 * pattern means "not held", and the keyboard boots normally.
 */
__attribute__((unused)) static bool escape_held(void) {
    const uint32_t escape = 1UL << ESCAPE_COL;
    const uint32_t senses = ((1UL << MATRIX_COLS) - 1UL) & ~(1UL << ESCAPE_ROW_UNUSED_COL);
    const uint32_t others = senses & ~escape;

    k100_matrix_configure_pins();

    for (uint8_t sample = 0; sample < ESCAPE_SAMPLES; sample++) {
        const uint32_t reading = k100_matrix_sample_row(ESCAPE_ROW) & senses;

        /* Unanimous among the others, and Escape the opposite of them. The two
           acceptable readings are "others all high, Escape low" and its
           inverse; anything else is not a held key. */
        const bool all_high = (reading & others) == others;
        const bool all_low  = (reading & others) == 0UL;
        const bool escape_high = (reading & escape) != 0UL;

        if (!((all_high && !escape_high) || (all_low && escape_high))) {
            return false;
        }
    }

    return true;
}

#ifdef K100_USB_DIAGNOSTIC
void early_hardware_init_pre(void) {
    for (volatile uint32_t spin = 0; spin < DIAGNOSTIC_EARLY_LOOPS; spin++) {
    }
}

/**
 * The three bits of DEVCMDSTAT that say why a device is not on the bus.
 *
 * DEV_EN is the controller enabled at all, DCON is the D+ pull-up the host
 * actually sees, and VBUS_DEBOUNCED is whether the controller believes the
 * cable is powered -- a high-speed controller that does not see VBUS will not
 * connect however correctly everything else is set up, and this part offers a
 * FORCE_VBUS override for boards that do not wire the sense line.
 */
static uint8_t usb_diagnostic_code(void) {
    const uint32_t status = USBHSD->DEVCMDSTAT;

    return (uint8_t)(((status & USBHSD_DEVCMDSTAT_DEV_EN_MASK) ? 1U : 0U) |
                     ((status & USBHSD_DEVCMDSTAT_DCON_MASK) ? 2U : 0U) |
                     ((status & USBHSD_DEVCMDSTAT_VBUS_DEBOUNCED_MASK) ? 4U : 0U));
}
#endif

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
#ifdef K100_USB_DIAGNOSTIC
    /* The marker first, so nothing downstream can prevent it being observed,
       and no Escape check, so it cannot cut the measurement short. */
    wait_ms(DIAGNOSTIC_MARKER_MS);
#else
    if (escape_held()) {
        (void)k100_enter_bootloader();
    }
#endif

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
         *
         * If that fails the EEPROM write did not take, and trying again every
         * loop would be a busy wait on something that is not going to change.
         * Latch instead and let the keyboard run as it is.
         */
#ifdef K100_USB_DIAGNOSTIC
        /* Stay away for a length of time that encodes the register state, so
           whoever is watching the bus can read it off. */
        wait_ms(DIAGNOSTIC_BASE_MS + (DIAGNOSTIC_STEP_MS * usb_diagnostic_code()));
#endif
        if (!k100_enter_bootloader()) {
            usb_was_ready = true;
        }
    }

    housekeeping_task_user();
}
