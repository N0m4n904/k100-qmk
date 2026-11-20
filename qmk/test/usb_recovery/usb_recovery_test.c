// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * When the USB recovery hands the keyboard to the bootloader, fed one trip round the main loop at
 * a time with the host the test sets up. The timeout is the one test-usb-recovery.sh builds with.
 */

#include <stdio.h>
#include <stdlib.h>

#include "usb_recovery.h"

#define REQUIRE(condition)                                                     \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

#define TIMEOUT_MS K100_USB_WATCHDOG_MS
#define TRIP_MS 5U

typedef struct {
    k100_usb_recovery_t recovery;
    uint32_t            now_ms;
} keyboard_t;

/** Runs trips for @p duration_ms. @return whether the recovery became due on any of them. */
static bool run(keyboard_t *keyboard, uint32_t duration_ms, enum k100_host_presence presence, bool configured) {
    bool due = false;
    for (const uint32_t end = keyboard->now_ms + duration_ms; keyboard->now_ms < end; keyboard->now_ms += TRIP_MS) {
        due = k100_usb_recovery_is_due(&keyboard->recovery, presence, configured, keyboard->now_ms) || due;
    }
    return due;
}

/*
 * A keyboard powered while no host uses it, such as behind a hub switched to a computer that is
 * off. This is also the known limitation: a firmware that never attaches to the bus looks the same
 * and is not handed over either.
 */
static void test_a_bus_without_frames_never_hands_over(void) {
    keyboard_t keyboard = {0};

    REQUIRE(!run(&keyboard, 5000U, K100_HOST_NOT_SEEN, false));
    REQUIRE(!run(&keyboard, 20U * TIMEOUT_MS, K100_HOST_GONE, false));
}

static void test_a_host_that_never_configures_hands_over_after_the_timeout(void) {
    keyboard_t keyboard = {0};

    REQUIRE(!run(&keyboard, 5000U, K100_HOST_NOT_SEEN, false));
    REQUIRE(!run(&keyboard, TIMEOUT_MS, K100_HOST_SENDING, false));
    REQUIRE(run(&keyboard, 2U * TRIP_MS, K100_HOST_SENDING, false));
}

/* A host that gives up disables the port, and the frames stop: the clock runs on. */
static void test_a_host_that_gives_up_still_hands_over(void) {
    keyboard_t keyboard = {0};

    REQUIRE(!run(&keyboard, 2000U, K100_HOST_SENDING, false));
    REQUIRE(run(&keyboard, TIMEOUT_MS, K100_HOST_GONE, false));
}

/* An idle bus reports a suspend 79 ms after power-up, before any configuration. */
static void test_a_suspend_before_any_configuration_does_not_disarm_it(void) {
    keyboard_t keyboard = {0};

    REQUIRE(!run(&keyboard, 100U, K100_HOST_GONE, false));
    REQUIRE(run(&keyboard, TIMEOUT_MS + 2U * TRIP_MS, K100_HOST_SENDING, false));
}

static void test_a_configured_keyboard_never_hands_over(void) {
    keyboard_t keyboard = {0};

    REQUIRE(!run(&keyboard, 1000U, K100_HOST_SENDING, false));
    REQUIRE(!run(&keyboard, TRIP_MS, K100_HOST_SENDING, true));
    REQUIRE(!run(&keyboard, 20U * TIMEOUT_MS, K100_HOST_GONE, false));
    REQUIRE(!run(&keyboard, 20U * TIMEOUT_MS, K100_HOST_SENDING, false));
}

/* A host that turns up late gets the whole timeout from its first frame. */
static void test_the_clock_starts_at_the_first_frame(void) {
    keyboard_t keyboard = {0};

    REQUIRE(!run(&keyboard, 4900U, K100_HOST_NOT_SEEN, false));
    REQUIRE(!run(&keyboard, TIMEOUT_MS, K100_HOST_SENDING, false));
}

int main(void) {
    test_a_bus_without_frames_never_hands_over();
    test_a_host_that_never_configures_hands_over_after_the_timeout();
    test_a_host_that_gives_up_still_hands_over();
    test_a_suspend_before_any_configuration_does_not_disarm_it();
    test_a_configured_keyboard_never_hands_over();
    test_the_clock_starts_at_the_first_frame();

    puts("ok: the USB recovery hands over only after a host sent frames and never configured the keyboard");
    return 0;
}
