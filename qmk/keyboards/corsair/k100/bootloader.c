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
 *   QK_BOOT               needs USB and the matrix        an ordinary reflash
 *   A top-row key held    needs the matrix read only      USB broken
 *   USB watchdog          needs neither                   firmware unusable
 *
 * The last two matter most for a first flash, because they survive this project
 * being wrong about the things it is still guessing at.
 */

#include "quantum.h"
#include "usb_device_state.h"

#include "lpc546xx_eeprom.h"
#include "lpc546xx_rit.h"
#include "lpc546xx_wwdt.h"
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
 * The strobe the recovery key is looked for on: row 0, the top row.
 *
 * Any single key on it will do, and that is deliberate -- see
 * recovery_key_held() for why no particular column is named here.
 */
#define RECOVERY_ROW 0U

/*
 * Row 0 has no key on sense 14, from the generated layout. It is left out of the
 * unanimity test below, because a sense with nothing attached to it cannot be
 * expected to follow the ones that have.
 */
#define RECOVERY_ROW_UNUSED_COL 14U

/*
 * How many agreeing samples it takes. Each costs one strobe and the settling
 * wait, so a handful is microseconds and rules out a single bad read.
 */
#define RECOVERY_SAMPLES 4U

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

/*
 * Stage markers, for finding how far boot gets.
 *
 * Each stage holds the keyboard off the bus for a set number of seconds, feeding
 * the watchdog while it waits. The stages are nested -- reaching one means every
 * earlier one ran -- so the total absence is a prefix sum, and with the values
 * below every prefix is a distinct, widely separated number. One flash therefore
 * names the furthest stage reached, instead of answering one yes-or-no question:
 *
 *     ~2 s    nothing ran, or it died before matrix_setup
 *     ~10 s   matrix_setup           the kernel is scheduling
 *     ~30 s   keyboard_pre_init_kb   halInit() and USB init are behind it
 *     ~70 s   keyboard_post_init_kb  the Escape check and the rest of
 *                                    keyboard_init() completed
 *     ~130 s  housekeeping_task_kb   it is a running keyboard
 *
 * Wide apart on purpose. Values a few seconds apart were useless: the apply
 * before a reset and the bootloader's own start each vary by seconds, and a
 * marker that runs before halInit() is timed by whatever clock the bootloader
 * left behind. Against a working image, which is back on the bus in under a
 * second, anything above ten seconds is unambiguous.
 *
 * Two placements carry more weight than the rest. The pre-init marker sits
 * *before* the Escape check, which is the first thing in boot to call wait_us(),
 * so 30 s against 70 s is also the answer to "does the realtime counter tick" --
 * the one fact about the RIT taken from how the part is wired rather than read
 * back from a register. And the housekeeping marker separates the two ways a
 * keyboard that never enumerates can fail: if it runs, USB is the only thing
 * wrong and housekeeping_task_kb() will hand the keyboard back to the bootloader
 * by itself; if it does not, something before the main loop is hanging and that
 * recovery path never gets its chance.
 *
 * DIAGNOSTIC_STAGE_SCAN_S is the odd one out: it marks the first matrix scan of
 * the main loop, which sits between the last two stages above. It bisects the
 * gap they leave -- keyboard_post_init_kb() is reached, the first
 * housekeeping_task() is not, and between them lie only protocol_post_init(),
 * which sets a pointer, and one pass of the loop. Nothing but that loop calls
 * the scan; the Escape check reads the matrix through k100_matrix_sample_row()
 * directly.
 *
 * The wait feeds the watchdog itself. Without that the marker is cut short by
 * the very reset it is trying to locate -- which is how an earlier version of
 * this diagnostic reported that the firmware never ran at all.
 */
#    define DIAGNOSTIC_STAGE_LATE_S           0U
#    define DIAGNOSTIC_STAGE_BOARD_S          0U
#    define DIAGNOSTIC_STAGE_MATRIX_S         0U
#    define DIAGNOSTIC_STAGE_PREINIT_S       0U
#    define DIAGNOSTIC_STAGE_ESCAPE_DONE_S   0U
#    define DIAGNOSTIC_STAGE_SLEEP_OK_S      0U

/*
 * Proving k100_enter_bootloader() on hardware.
 *
 * It is the single point of failure in this firmware's recovery: both the
 * Escape check and the ten-second USB timeout go through it, and its EEPROM
 * write has never been observed to succeed -- every mode change this project
 * has relied on was performed by Corsair's bootloader instead. Nothing should
 * be flashed that depends on it until it has been watched working.
 *
 * The call cannot report success, because success resets the part. So the
 * first mark says the attempt was reached and the second runs only if the
 * function *returns*, which it does only on failure. Roughly 18 s of absence
 * means the write works; roughly 58 s means it does not.
 *
 * Placed before USB and before the main loop, so the test depends on neither.
 * It also makes this image self-rescuing: if the write works the keyboard
 * returns to its bootloader on every boot, which is the safest thing that can
 * be installed on it.
 */
#    define DIAGNOSTIC_STAGE_ENTRY_TRY_S    10U
#    define DIAGNOSTIC_STAGE_ENTRY_FAILED_S 40U
/*
 * Inside keyboard_init(), which is where boot actually fails.
 *
 * Two weak hooks fall in the right places: matrix_init_kb() at the end of
 * matrix_init(), and led_init_ports(), which runs after quantum_init() and
 * host_init(). Overriding the latter costs nothing here -- this keyboard has no
 * backlight pins and rgb_matrix is off, so QMK's version is empty.
 */
#    define DIAGNOSTIC_STAGE_MATRIX_INIT_S    0U
#    define DIAGNOSTIC_STAGE_LED_PORTS_S      0U

/*
 * Inside USB initialisation, which is where boot fails.
 *
 * QMK's ordering is what points here: keyboard_setup() -- holding the Escape
 * check, which completes -- runs before protocol_pre_init(), and keyboard_init()
 * runs after it. So a boot that clears the Escape check and never reaches
 * matrix_init_kb() is a boot that died starting USB.
 */
#    define DIAGNOSTIC_STAGE_USB_STOP_S       0U
#    define DIAGNOSTIC_STAGE_USB_POWER_S      0U

#    define DIAGNOSTIC_STAGE_POSTINIT_S      0U
#    define DIAGNOSTIC_STAGE_HOUSEKEEPING_S   0U
#    define DIAGNOSTIC_STAGE_SCAN_S           0U

/*
 * Reported from board_init() instead of a stage length, for the case where the
 * realtime counter is dead rather than merely slow. Left at zero while the
 * prefix sums above are being read, because anything non-zero here shifts all of
 * them.
 */
/* Marked by the second boot only, to prove the boot counter retains. */
#    define DIAGNOSTIC_BOOT2_PROBE_S          0U

/*
 * Marked by *every* boot, to count how many times a failed image is started.
 *
 * This is the number every other reading is divided by, and it had never been
 * measured. A total of absence only names a boot stage once it is known whether
 * the markers in it ran once or several times, and the bootloader restarting an
 * image it cannot run multiplies them. Unguarded on purpose: the whole quantity
 * being measured is how often this runs.
 *
 * It sits in early_hardware_init_pre(), so it is timed by whatever clock the
 * bootloader handed over -- which is not the clock the other markers assume.
 * Calibrated against the boot-counter probe, where 30 nominal seconds there
 * measured about 27 real ones, so 10 nominal is roughly 9 real per boot.
 */
#    define DIAGNOSTIC_BOOTCOUNT_PROBE_S      0U

/*
 * Inner loop iterations per tenth of a second, at the clock halInit() sets.
 *
 * Measured, because guessing it wrong silently rescales every reading. With a
 * million here, a marker asking for 30 s produced 68.5 s of absence, so one
 * iteration costs about ten core cycles at 48 MHz rather than the five the
 * round number assumed. That factor of 2.28 is not a tidiness problem: it made
 * markers of 10 + 20 + 40 look like a total of 70 when the third had not run at
 * all, which put the failure three stages later than it really is.
 *
 * Markers that run before halInit() are still not in seconds -- the bootloader
 * hands over at roughly 180 MHz, and the same loop there measured 3.7 times
 * faster, which is the independent confirmation of that figure.
 */
#    define DIAGNOSTIC_SPINS_PER_TENTH   440000U

/* Marked only if the part refuses to lengthen the inherited watchdog. */
#    define DIAGNOSTIC_WATCHDOG_REFUSED_S    12U

/*
 * Seconds per fault class, reported if the core takes a fault.
 *
 * Worth catching separately from everything else, because a fault does not look
 * like a fault from outside. All four fault vectors in this image point at
 * ChibiOS's unhandled-exception stub, which is an infinite loop, so the core
 * spins there until the watchdog resets it -- the same few seconds of absence
 * that a hang, a stall or a refused image produce. It also explains a reading
 * that otherwise has no explanation: that every instruction between the last
 * stage known to run and the first one known not to is bounded.
 *
 * The class is encoded in the length so one flash answers both questions:
 * roughly 15 s for a hard fault, 30 for a memory-management fault, 45 for a bus
 * fault and 60 for a usage fault, against about 8 s for no fault at all.
 */
#    define DIAGNOSTIC_FAULT_STEP_S          15U

#    define DIAGNOSTIC_STAGE_RIT_LIVE_S       0U
#    define DIAGNOSTIC_STAGE_RIT_DEAD_S       0U

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
#    define DIAGNOSTIC_EARLY_LOOPS 2000000UL
#    define DIAGNOSTIC_EARLY_OUTER 120UL
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
 * The one sense in a reading that disagrees with all the others, if there is
 * exactly one.
 *
 * Which level means "pressed" is not known: the polarity of an OPX switch is
 * still a hypothesis in this project, MATRIX_SENSE_ACTIVE_LOW is one define with
 * a long comment above it, and an escape hatch that depends on an untested guess
 * is not an escape hatch. So this does not ask which senses read low. It asks
 * which one is the odd one out, which is the same answer under either polarity
 * and either pull direction.
 *
 * Returns the single disagreeing bit, or zero if the reading is unanimous or
 * more than one sense differs.
 */
static uint32_t lone_disagreeing_sense(uint32_t reading, uint32_t senses) {
    const uint32_t low = senses & ~reading;

    if (__builtin_popcount(reading) == 1) {
        return reading;
    }
    if (__builtin_popcount(low) == 1) {
        return low;
    }
    return 0UL;
}

/**
 * Is a single key on the top row held?
 *
 * Any one of them, and not a named column. Naming one was the obvious design and
 * it is the wrong one, because it makes the only escape hatch in this firmware
 * depend on the one table that is hardest to check: the strobe and sense
 * assignment decoded out of Corsair's firmware image. Escape and the topmost
 * macro key G1 are physically adjacent and sit at columns 1 and 0 of the same
 * row, so an off-by-one anywhere in that decoding silently moves the escape
 * hatch to the key next door -- and the person holding Escape has no way to tell
 * that from firmware which simply does not run. Accepting whichever single key
 * is held removes the dependency altogether.
 *
 * **Every other populated sense has to agree.** An earlier version asked only
 * whether the chosen column differed from the *majority*, which is far too weak:
 * a reading that is simply wrong -- the wrong pins, a strobe that drives nothing,
 * a bus left floating -- scatters bits arbitrarily, and any given sense lands on
 * the minority side of that about half the time. The check then fires on a
 * keyboard nobody touched, and since it fires before USB, the result is a
 * keyboard that boots into the bootloader and looks for all the world like
 * firmware that will not run. Requiring that exactly one of the seventeen
 * differs costs nothing when a key really is held and rejects a garbage read
 * immediately -- more firmly than the named-column version did, since a garbage
 * read now has to produce a single odd bit rather than merely put one particular
 * bit in the minority.
 *
 * Column 14 is excluded because row 0 has no key on it. With pull-ups it reads
 * high regardless, so under the polarity where a released key reads *low* it
 * would disagree with every real sense and veto the check forever.
 *
 * Sampling several times and demanding the *same* key each time guards against a
 * single bad read, which costs microseconds here.
 *
 * It fails safe in the direction that matters: anything other than one steady
 * odd sense means "not held", and the keyboard boots normally.
 */
static bool recovery_key_held(void) {
    const uint32_t senses = ((1UL << MATRIX_COLS) - 1UL) & ~(1UL << RECOVERY_ROW_UNUSED_COL);
    uint32_t       held   = 0UL;

    k100_matrix_configure_pins();

    for (uint8_t sample = 0; sample < RECOVERY_SAMPLES; sample++) {
        const uint32_t reading = k100_matrix_sample_row(RECOVERY_ROW) & senses;
        const uint32_t odd     = lone_disagreeing_sense(reading, senses);

        if (odd == 0UL || (sample > 0U && odd != held)) {
            return false;
        }
        held = odd;
    }

    return true;
}

/*
 * Feeding the watchdog the bootloader left running.
 *
 * Corsair's bootloader starts the windowed watchdog before it hands control to
 * an application, and on this part that cannot be undone: NXP's WDEN
 * description says the timer runs permanently once set and fed. So every stage
 * of this firmware's boot has to feed it, and so does the main loop.
 *
 * This was the bug that made the firmware look like it was being rejected. It
 * ran, got about a second and a half, and was reset -- which from the host is
 * indistinguishable from a bootloader that refuses to start the image, since the
 * keyboard reappears almost at once and still in bootloader mode.
 *
 * Deliberately *not* fed from the systick hook, though that would be less
 * code. Feeding only from the main loop leaves the watchdog as a free recovery
 * path: firmware that wedges somewhere with interrupts still running stops
 * feeding, the watchdog resets the chip, and the bootloader comes back with the
 * mode word untouched -- which is reachable and reflashable. Feeding it from an
 * interrupt would keep a hung keyboard alive and useless instead.
 */
#ifdef K100_USB_DIAGNOSTIC
/**
 * State that outlives a reset, so one boot can report on the previous one.
 *
 * Placed by hand at the base of SRAMX. The linker script keeps ram1 out of every
 * REGION_ALIAS, so nothing is linked there and crt0 -- which zeroes the whole of
 * ram0 -- never touches it. SRAMX is core-coupled and has no clock gate of its
 * own (NXP gates SRAM1, SRAM2 and SRAM3 only), so it is readable as soon as the
 * core is running.
 *
 * It is read from early_hardware_init_pre(), which crt0 reaches before it copies
 * .data or clears .bss, so the read happens before anything could overwrite it
 * even if that reasoning about ram1 were wrong. If the area turns out not to
 * retain at all, the magic never matches, every boot looks like the first, and
 * the markers behave exactly as they did before this existed -- the failure mode
 * is the old behaviour rather than a wrong answer.
 */
#    define DIAGNOSTIC_SURVIVOR_ADDRESS 0x04000000U
#    define DIAGNOSTIC_SURVIVOR_MAGIC   0x4B313030UL /* "K100" */

typedef struct {
    uint32_t magic;
    uint32_t boots;
} diagnostic_survivor_t;

#    define DIAGNOSTIC_SURVIVOR \
        (*(volatile diagnostic_survivor_t *)DIAGNOSTIC_SURVIVOR_ADDRESS)

/**
 * Holds the keyboard off the bus for roughly this many seconds, feeding the
 * watchdog throughout so the delay is not cut short by it.
 *
 * Every boot marks. That is safe because the bootloader starts a firmware it
 * cannot run exactly once -- measured, not assumed: a build with a single
 * unguarded 10 s mark in early_hardware_init_pre() was absent for 14.31 s
 * against a no-marker baseline of 8.15 s, and one mark there is worth about
 * 6.1 s, so it ran once.
 *
 * It is worth saying why there is no first-boot guard, because there was one and
 * it did active harm. The worry it answered was real -- if the bootloader
 * retried, every marker would run again on each attempt and a total of 76 s
 * would read equally well as 70 s once or 30 s two and a half times. But the
 * guard counted boots in memory that survives a *reflash* as well as a reset, so
 * the count carried over from the previous experiment, the second flash in a row
 * never saw a count of one, and every marker silently vanished. A measurement
 * that fails by reporting the baseline is worse than no measurement, and the
 * retry count it guarded against turned out to be one.
 *
 * A busy loop rather than wait_ms(), because the earliest callers run before the
 * kernel exists. The iteration count is approximate by design.
 */
static void diagnostic_mark_always(uint32_t seconds) {
    for (uint32_t tenth = 0U; tenth < seconds * 10U; tenth++) {
        lpcWwdtFeed();
        for (volatile uint32_t spin = 0U; spin < DIAGNOSTIC_SPINS_PER_TENTH; spin++) {
        }
    }
}

static void diagnostic_mark(uint32_t seconds) {
    diagnostic_mark_always(seconds);
}

/**
 * The same marker, but timed by wait_us() instead of by a loop count.
 *
 * This measures the realtime counter, which is the one thing in the clock tree
 * that cannot be checked from the build. wait_us() here is chSysPolledDelayX()
 * against the RIT, scaled by the rate the port claims the RIT ticks at -- and
 * that rate is the one figure in lpc546xx_rit.c taken from how the part is
 * wired rather than from a register. Asking for a known number of seconds and
 * timing the result on the host settles it: the right length means the rate is
 * right, half or double means it is out by that factor, and a couple of seconds
 * means wait_us() did not return at all and the watchdog cut in.
 *
 * Marked unused because which marker a stage uses is an experiment-time choice,
 * and a stage isolating a different variable has to use the loop-based one --
 * a marker timed by the thing under test cannot report on it. Kept compiled so
 * that it cannot rot between experiments.
 */
__attribute__((unused)) static void diagnostic_mark_timed(uint32_t seconds) {
    for (uint32_t ms = 0U; ms < seconds * 1000U; ms++) {
        lpcWwdtFeed();
        wait_us(1000U);
    }
}
#endif

#ifdef K100_USB_DIAGNOSTIC
void k100_diagnostic_mark_first_scan(void) {
    static bool marked = false;

    if (!marked) {
        marked = true;
        diagnostic_mark(DIAGNOSTIC_STAGE_SCAN_S);
    }
}
#endif

#ifdef K100_USB_DIAGNOSTIC
/**
 * Reports a fault by its length, then lets the watchdog end the boot.
 *
 * Only the first boot reports, for the same reason the stage markers do: the
 * bootloader retries, and a signal that repeats is a signal that cannot be read.
 */
static void diagnostic_fault(uint32_t class) {
    if (DIAGNOSTIC_SURVIVOR.boots == 1U) {
        diagnostic_mark_always(class * DIAGNOSTIC_FAULT_STEP_S);
    }

    /* Deliberately not recovering. The watchdog resets the part and the
       bootloader comes back reflashable, which is the state to be left in. */
    for (;;) {
    }
}

void HardFault_Handler(void) {
    diagnostic_fault(1U);
}

void MemManage_Handler(void) {
    diagnostic_fault(2U);
}

void BusFault_Handler(void) {
    diagnostic_fault(3U);
}

void UsageFault_Handler(void) {
    diagnostic_fault(4U);
}
#endif

#ifdef K100_USB_DIAGNOSTIC
/*
 * The stretch that no keyboard-side hook can see into.
 *
 * keyboard_post_init_kb() is reached and the first matrix_scan_custom() is not,
 * and matrix_scan_custom() is the first statement of matrix_scan(), which
 * keyboard_task() reaches in three instructions because matrix_can_read()
 * compiles to `movs r0, #1`. So the stop is in one of four places, and all four
 * are calls in QMK's main() with nothing overridable between them. Requires
 * qmk/patches/0002-main-loop-checkpoints.patch.
 *
 * Lengths are chosen so the total names the furthest point reached: 5, then 15,
 * then 35, then 75 seconds.
 */
void main_checkpoint(uint8_t stage) {
    static const uint8_t seconds[] = {0U, 80U, 0U, 0U, 0U};

    if (stage < (sizeof seconds / sizeof seconds[0])) {
        diagnostic_mark(seconds[stage]);
    }
}
#endif

#ifdef K100_USB_DIAGNOSTIC
void matrix_init_kb(void) {
    diagnostic_mark(DIAGNOSTIC_STAGE_MATRIX_INIT_S);
    matrix_init_user();
}

void led_init_ports(void) {
    diagnostic_mark(DIAGNOSTIC_STAGE_LED_PORTS_S);
}
#endif

void early_hardware_init_pre(void) {
    lpcWwdtFeed();

#ifdef K100_USB_DIAGNOSTIC
    /* Before crt0 has copied .data or cleared .bss, which is the whole point:
       this is the last moment the previous boot's RAM is still readable. */
    if (DIAGNOSTIC_SURVIVOR.magic != DIAGNOSTIC_SURVIVOR_MAGIC) {
        DIAGNOSTIC_SURVIVOR.magic = DIAGNOSTIC_SURVIVOR_MAGIC;
        DIAGNOSTIC_SURVIVOR.boots = 0U;
    }
    DIAGNOSTIC_SURVIVOR.boots++;

    /* Checks the counter above rather than the firmware: a mark that only the
       second boot makes is one the keyboard cannot produce unless this area
       really did survive the reset. Without it the stage markers are not
       trustworthy, because a total of 76 s reads the same whether the markers
       ran once as intended or several times across retries -- which is the
       confound the counter exists to remove, and so is exactly the thing that
       cannot be assumed to work. */
    if (DIAGNOSTIC_SURVIVOR.boots == 2U) {
        diagnostic_mark_always(DIAGNOSTIC_BOOT2_PROBE_S);
    }

    diagnostic_mark_always(DIAGNOSTIC_BOOTCOUNT_PROBE_S);

#    ifdef K100_DIAGNOSTIC_STRETCH_WATCHDOG
    /* Separates "hung" from "slower than the inherited watchdog allows". A
       distinctive mark if the part refuses, so a refusal is not read as a
       result. */
    if (!lpcWwdtStretch()) {
        diagnostic_mark_always(DIAGNOSTIC_WATCHDOG_REFUSED_S);
    }
#    endif
#endif
}

#ifdef K100_USB_DIAGNOSTIC
/*
 * ChibiOS's own weak hook, called by crt0 after the stacks are filled, .data is
 * copied, .bss is cleared and the whole of ram0 is zeroed -- and before main().
 * That makes it the one place a marker can separate "RAM initialisation" from
 * "halInit()", which is otherwise a single opaque stretch between the hook that
 * demonstrably runs and the one that does not.
 */
void __late_init(void) {
    diagnostic_mark(DIAGNOSTIC_STAGE_LATE_S);
}
#endif

void board_init(void) {
    lpcWwdtFeed();
#ifdef K100_USB_DIAGNOSTIC
    diagnostic_mark(DIAGNOSTIC_STAGE_BOARD_S);
    diagnostic_mark(lpcRitIsRunning() ? DIAGNOSTIC_STAGE_RIT_LIVE_S
                                      : DIAGNOSTIC_STAGE_RIT_DEAD_S);
#endif
}


#ifdef K100_USB_DIAGNOSTIC
/*
 * The port's USB bring-up trace, wired to the delay channel. Stage 1 is its
 * clock and power setup, reached from halInit(); stage 2 is the transceiver,
 * reached from usbStart(). Only stage 2 is marked, because stage 1 is already
 * known to run -- halInit() completes.
 */
void lpcUsbTrace(unsigned stage) {
    /* Stage 1 is the clock and power setup, reached from halInit() and so
       already known to run. Stage 2 is the transceiver, reached from usbStart()
       inside protocol_pre_init() -- which is the call boot now fails in. */
    if (stage == 2U) {
        diagnostic_mark(DIAGNOSTIC_STAGE_USB_POWER_S);
    }

    /* Stage 3 is lpc_usb_power_off(), which usbStop() reaches. init_usb_driver()
       calls usbStop() before usbStart(), so this splits the window that is left:
       the endpoint setup and usbDisconnectBus() come before it, the 50 ms wait
       and usbStart() after. */
    if (stage == 3U) {
        diagnostic_mark(DIAGNOSTIC_STAGE_USB_STOP_S);
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
    lpcWwdtFeed();

    /*
     * The recovery-key check comes first -- before every marker, every
     * diagnostic and anything else that could stall -- and that ordering is not
     * a stylistic preference, it is the lesson from losing a keyboard to this
     * file.
     *
     * This is the only lever a person has on firmware that is already running.
     * Everything else this project can do needs the keyboard to be answering on
     * USB, so once it stops doing that, a held key is the entire recovery story.
     * A build that put two diagnostic delays and a failing EEPROM write ahead of
     * this check moved it roughly fifty seconds into boot and then never reached
     * it at all, and the keyboard could not be recovered by any software means.
     *
     * So: nothing goes above this line. Anything added to this function belongs
     * below it, however briefly it claims to run.
     */
    if (recovery_key_held()) {
        (void)k100_enter_bootloader();
    }

#ifdef K100_USB_DIAGNOSTIC
    diagnostic_mark(DIAGNOSTIC_STAGE_PREINIT_S);
#endif

#ifdef K100_DIAGNOSTIC_PROVE_BOOTLOADER_ENTRY
    diagnostic_mark(DIAGNOSTIC_STAGE_ENTRY_TRY_S);

    /* Returns only on failure; success resets the part from inside the call. */
    if (!k100_enter_bootloader()) {
        diagnostic_mark(DIAGNOSTIC_STAGE_ENTRY_FAILED_S);
    }
#endif

#ifdef K100_USB_DIAGNOSTIC
    /* The Escape check above is the first thing in boot to read the matrix, and
       the first to call wait_us(). Marking below it separates it from the rest
       of keyboard_init(). */
    diagnostic_mark(DIAGNOSTIC_STAGE_ESCAPE_DONE_S);

    /*
     * Does a kernel sleep ever return?
     *
     * Nothing in boot sleeps until USB initialisation does, and that is exactly
     * where boot stops. Everything working up to here -- including wait_us() in
     * the Escape check -- runs off the RIT and never involves the scheduler, so
     * the system timer has not been exercised once.
     *
     * It is the right shape for the fault that is left. ChibiOS runs tickless
     * here (CH_CFG_ST_TIMEDELTA is 2), so a sleep programmes the timer's
     * comparator and blocks until it fires; if the timer never fires the thread
     * is simply never woken. That is a hang with no fault, no unbounded loop in
     * any source file, and no dependence on USB at all -- which is every
     * symptom observed.
     *
     * One millisecond, because the question is whether it returns, not when.
     */
    chThdSleepMilliseconds(1);

    diagnostic_mark(DIAGNOSTIC_STAGE_SLEEP_OK_S);
#endif

    keyboard_pre_init_user();
}

#ifdef K100_USB_DIAGNOSTIC
/*
 * QMK's earliest overridable hook after chSysInit(). keyboard_setup() calls it
 * before keyboard_pre_init_quantum(), which makes it the one place a marker can
 * separate "the kernel started" from "the kernel did not".
 */
void matrix_setup(void) {
    diagnostic_mark(DIAGNOSTIC_STAGE_MATRIX_S);
}

void keyboard_post_init_kb(void) {
#ifdef K100_DIAGNOSTIC_MASK_USB_IRQ
    /*
     * Masks the USB interrupt, as a test of one specific explanation for the
     * stop between here and the first matrix scan.
     *
     * Everything in that stretch is bounded when read on its own, and the core
     * is not faulting -- a build that caught all four fault classes reported
     * none. What is left is usb_event_queue_task(), whose drain loop exits only
     * when the queue is momentarily empty. A controller that never enumerates
     * and keeps signalling can refill it from the interrupt as fast as the loop
     * empties it, and then the loop does not end. That also explains why the
     * marker in this very function completes: nothing drains the queue while a
     * marker is running, so the spin only begins once the main loop does.
     *
     * With the interrupt masked the queue cannot be refilled. If the main loop
     * then runs, that is the mechanism.
     */
    nvicDisableVector(LPC_USB_IRQ_NUMBER);
#endif

    diagnostic_mark(DIAGNOSTIC_STAGE_POSTINIT_S);

#ifdef K100_DIAGNOSTIC_SILENCE_IRQ_AT_POSTINIT
    /*
     * Tests whether an interrupt is what loses the return out of
     * keyboard_init().
     *
     * The marker above completes, and the next instrumented point -- the one
     * immediately after keyboard_init() returns -- does not. Between them are
     * keyboard_post_init_user(), which is `bx lr`, and two pops. Straight-line
     * code like that cannot hang, so something must be corrupting the return
     * path, and the only other thing executing is interrupt handlers: this
     * firmware spends the marker's whole duration spinning with them enabled,
     * driving a USB controller that never enumerates.
     *
     * With them masked from here on, the return path runs alone. If the next
     * point is then reached, an interrupt handler is responsible.
     */
    __disable_irq();
#endif

    keyboard_post_init_user();
}
#endif

void housekeeping_task_kb(void) {
    static uint32_t first_task_at = 0;
    static bool     usb_was_ready = false;
    static bool     starve_watchdog = false;

    /*
     * Every iteration, and before anything that might return early -- unless
     * the firmware has given up on becoming usable, in which case the surest
     * thing it can do is stop feeding and let the watchdog have it.
     */
    if (!starve_watchdog) {
        lpcWwdtFeed();
    }

#ifdef K100_USB_DIAGNOSTIC
    /* Once, not every iteration: this one reports that the loop was reached, and
       a marker that ran on every pass would stop it being a loop. */
    static bool marked = false;
    if (!marked) {
        marked = true;
        diagnostic_mark(DIAGNOSTIC_STAGE_HOUSEKEEPING_S);
    }
#endif

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
         * If that fails, stop feeding the watchdog.
         *
         * An earlier version latched here and let the keyboard keep running,
         * on the reasoning that this beat a reboot loop. It does not, and the
         * difference is the whole safety margin of this project. A firmware
         * that runs, never enumerates and keeps its watchdog fed is invisible
         * and immortal: no tool here can reach it, and the one thing that had
         * been rescuing every failed experiment -- the watchdog reset -- is
         * exactly what the feeding prevents. That state was reached once, and
         * the keyboard only came back because a power cycle happened to land
         * in the bootloader.
         *
         * Starving it instead reaches the bootloader through the mechanism this
         * project has actually observed working: an application that dies soon
         * after handover makes the bootloader stay in bootloader mode. The
         * EEPROM write above has never once been seen to succeed on hardware,
         * so it must not be the only way back -- and because both the Escape
         * check and this timeout call it, a single failure took both routes
         * out together.
         */
#ifdef K100_USB_DIAGNOSTIC
        /* Stay away for a length of time that encodes the register state, so
           whoever is watching the bus can read it off. */
        wait_ms(DIAGNOSTIC_BASE_MS + (DIAGNOSTIC_STEP_MS * usb_diagnostic_code()));
#endif
        if (!k100_enter_bootloader()) {
            starve_watchdog = true;
        }
    }

    housekeeping_task_user();
}
