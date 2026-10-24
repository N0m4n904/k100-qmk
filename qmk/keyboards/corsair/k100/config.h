// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

/*
 * 190 LEDs across three IS31FL3743B drivers, as the firmware's LedInfoMap
 * declares: 184 RGB keys and bars plus 6 single-colour indicators. Both tables
 * are generated and both are this long -- the lighting table by
 * tools/generate-qmk-layout.py and the channel table by
 * tools/generate-led-tables.py.
 *
 * It used to be 179, which was the length someone chose: far enough to reach
 * the status icons and no further. The firmware's own count is the one number
 * here that nobody picked, and QMK will not address an LED past the end of the
 * table, so a shorter one silently puts some of this keyboard out of reach.
 *
 * Sixteen of them have no position, so no effect reaches them and they stay
 * dark. What position each LED has is a separate and unsettled question; see
 * docs/pin-mapping.md on the LED counts.
 */
/*
 * With rgb_matrix driving them the count is RGB_MATRIX_LED_COUNT, which the
 * driver header takes care of; this is for the build with rgb_matrix off, where
 * the tables are still compiled.
 */
#ifndef RGB_MATRIX_IS31FL3743B
#    define IS31FL3743B_LED_COUNT 190
#endif

#define IS31FL3743B_DRIVER_COUNT 3

/*
 * Settings QMK has already stored outlive a reflash, which is the point of
 * storing them -- and means a changed default here reaches a keyboard that has
 * never had settings written, and no other. Leaving it at that would make
 * every default change a manual reset on every keyboard that has ever run.
 *
 * QMK's answer is this number: a keyboard whose stored copy does not carry it
 * initialises its settings once, from the defaults compiled in. So it moves
 * whenever a default below or in keyboard.json does, and it moves *downwards*,
 * which is the convention QMK's own definition asks for so that a later
 * upstream value cannot collide with one chosen here.
 *
 * **The keymap is one of those settings.** VIA is enabled, so the keymap the
 * firmware reads is the one in EEPROM and the compiled keymaps[] is only what
 * that copy was first seeded from -- keymap_key_to_keycode goes to
 * dynamic_keymap_get_keycode, and a changed keycode in the keymap reaches a
 * keyboard that already holds a stored copy exactly as little as a changed
 * colour does. eeconfig_init_quantum() reseeds it through eeconfig_init_via(),
 * so this number carries the keymap too, and forgetting to move it looks like
 * a key that does nothing at all while every other part of the path measures
 * correct.
 *
 * 0xFEE3 is upstream's. Moved down when the lighting region was given room to grow into, and
 * before that when the default lighting became white,
 * again when the Windows lock key was given a keycode, again when the wheel
 * button was given one, again when the macro keys stopped sending F13-F18 and
 * became VIA's macros, again when the Fn layer took on NKRO and the effect
 * speed, again when the per-key colours took a region of their own, and again
 * when each of those colours took a brightness, and again when the lock keys
 * were given a colour of their own. Every
 * entry here is a keymap change, or a change to the shape of what is stored,
 * that the stored copy would otherwise have outlived -- which is what this
 * number is for, and what forgetting it looks like: a key that still does the
 * old thing however correct the build is.
 */
#define EECONFIG_MAGIC_NUMBER (uint16_t)0xFEDA

/*
 * Room for one colour per LED, which is what VIA's key painter writes.
 *
 * VIA keeps per-key colours on its own channel and expects the keyboard to
 * store them; this region is what QMK gives a keyboard for exactly that, and
 * declaring a size is what creates it. 190 LEDs at three bytes each -- a hue, a
 * saturation and a brightness -- and sixteen more.
 *
 * Nine of those forty-eight are the colours the lock keys show while their locks
 * are on, five are what the reactive effects do, and one is how long the
 * lighting waits before it rests. The remaining thirty-three are deliberate
 * slack, and the slack is the point of the number.
 *
 * Growing this region is the one thing left that clears what a user has stored,
 * which is why it was last grown *on purpose* rather than under pressure: there
 * were two bytes left, every small setting was costing one, and the next one
 * would have forced the reset anyway at whatever moment it happened to be
 * wanted. It was done once, deliberately, with tools/k100-backup.py proven
 * around it, and thirty-three bytes is a long time at the rate settings arrive.
 *
 * Spare bytes read as zero, and every default in lighting_store.c is zero for
 * that reason: a keyboard that has never been told any of this behaves exactly
 * as one that was told the defaults.
 *
 * The brightness is not in what VIA sends and is stored all the same, because it
 * is the only way to say that one key is off: zero saturation is white, not
 * dark. The application's palette carries a hue and a saturation, so the
 * brightness is set as a property of the brush instead; see
 * K100_VIA_PAINT_VALUE and lighting_store.c.
 *
 * It comes out of what the dynamic keymap leaves, so the macro buffer shrinks by
 * the same amount: 855 bytes before any of this was taken, and the keyboard
 * reports what is left, which is the thing to check before taking more. See the
 * EEPROM driver's own header for why the whole region is 2 KB rather than the
 * array's 16.
 */
#define VIA_EEPROM_CUSTOM_CONFIG_SIZE (190 * 3 + 48)

/*
 * What tells VIA's stored data apart, instead of the build date.
 *
 * VIA's own marker is the date the firmware was built, to the day, which throws
 * away the keymap and the macros of every keyboard updated on a day other than
 * the one its firmware was built on. Most updates change nothing about what is
 * stored, and a user's remapping is not ours to discard because the calendar
 * moved. It also fails the other way: two builds on one day that *do* change the
 * shape of what is stored keep it, which is the case the check exists for.
 *
 * So this keyboard says it itself, and says it with the number that already
 * means exactly this: the one above, which moves when the shape of what is
 * stored changes and is listed with every move that has happened. Keymap,
 * macros and painted colours then survive every update that does not move it,
 * and all three are reset together when one does -- deliberately, in one place,
 * for a reason written down.
 *
 * The patch that makes this possible is qmk/patches/0006; without it a keyboard
 * cannot have an opinion here, because via_eeprom_is_valid() is not weak.
 */
#define VIA_EEPROM_VERSION EECONFIG_MAGIC_NUMBER

/*
 * Remember which keys were pressed and how long ago, which is what makes an
 * effect able to react to one.
 *
 * rgb_matrix keeps a ring of the last eight hits -- the LED each landed on, its
 * position in the layout, and its age in milliseconds -- and the code that fills
 * it exists only when this is defined. QMK defines it for a keyboard that
 * compiles one of *its* reactive effects; this keyboard compiles its own, so it
 * says so here. See rgb_matrix_kb.inc.
 *
 * It costs 82 bytes of RAM -- the ring is double buffered, so that a frame being
 * rendered in slices does not see the ages move underneath it -- and a few
 * instructions per key event. What the effects themselves cost is the part worth
 * measuring rather than estimating, and is measured: the frame and scan rates in
 * VIA's Diagnostics menu, see lighting_rate.c.
 */
#define RGB_MATRIX_KEYPRESSES

/*
 * Let the lighting be turned off while nothing is using it.
 *
 * This is what makes `rgb_matrix_set_suspend_state()` do anything: without it
 * the call compiles to nothing and the flag it sets is never read, so a
 * keyboard cannot be told to rest. With it, the lighting task renders one black
 * frame, flushes it and then stops sending frames until it is told otherwise.
 *
 * **QMK's own route to it does not exist on this keyboard.** The only caller of
 * `suspend_power_down()` is the loop in protocol_pre_task() that waits for a
 * suspended host, and that loop is compiled out here by NO_USB_STARTUP_CHECK --
 * deliberately, because it never reaches housekeeping and the watchdog this
 * firmware inherits is fed from there. So nothing told the lighting that the
 * host had gone to sleep, and it stayed lit all night.
 *
 * lighting_sleep.c decides it instead, from the main loop, where the watchdog
 * keeps being fed.
 */
#define RGB_MATRIX_SLEEP

/* Temporary: USB bring-up diagnostic, see bootloader.c. */
/*
 * Never block the main loop waiting for the host.
 *
 * QMK's ChibiOS protocol_pre_task() otherwise spins in
 *
 *     while (USB_DRIVER.state == USB_SUSPENDED) { suspend_power_down(); ... }
 *
 * which is the one unbounded loop between keyboard_post_init_kb() and the first
 * housekeeping_task(). On a keyboard that can afford to wait for the host that
 * is reasonable. This one cannot, for two reasons that are both fatal rather
 * than untidy:
 *
 * The watchdog this firmware inherits from Corsair's bootloader cannot be turned
 * off, and it is fed from housekeeping_task_kb() -- deliberately, so that a
 * wedged keyboard resets itself back into a reflashable bootloader instead of
 * staying alive and useless. A loop that never reaches housekeeping therefore
 * stops the feed and the part resets about a second and a half later.
 *
 * And the same housekeeping task carries the recovery path: if USB has not come
 * up within ten seconds it hands the keyboard back to the bootloader. Starving
 * that is what turns "USB does not enumerate" from a reflashable state into a
 * keyboard that only cycles.
 *
 * This is a hazard removed, not a bug fixed. It was tried as a candidate for the
 * hang between keyboard_post_init_kb() and the first housekeeping_task(), and
 * measured: the length of absence from the bus was 8.15 s before the change and
 * 8.16 s after it, so the driver is evidently never in USB_SUSPENDED here and
 * this loop was not what boot was dying in. It stays because the reasoning above
 * holds regardless -- an unbounded wait on the host, in a firmware whose
 * watchdog is fed from the main loop, is a reset waiting for a host that
 * suspends.
 */
#define NO_USB_STARTUP_CHECK

/*
 * The vendor interface has no OUT endpoint, so VIA's requests arrive as
 * SET_REPORT on endpoint zero instead.
 *
 * Not a preference. An interrupt OUT endpoint on this controller takes the
 * packet, hands it to the firmware and is answered -- and the host is never
 * given an acknowledgement it accepts, so its data toggle never advances and
 * the two ends disagree from the second packet onwards. Every write is reported
 * as failed, and the VIA application sees a keyboard that never answers.
 * docs/usb-bringup.md has the measurements, and what has been ruled out: the
 * host's HID path, the arming size, double buffering, a shared buffer, and the
 * endpoint type.
 *
 * A control transfer carrying data is the one direction on this keyboard that
 * is known to work -- it is how the host drives the lock lights, and it was
 * measured answering. A host sends output reports that way when an interface
 * offers nowhere else, which a HID interface is entitled to do and many are.
 *
 * So this is a different transfer type, not a trick played on a broken one. It
 * stops being necessary the day the endpoint is understood, and nothing else
 * depends on it.
 */
#define RAW_HID_NO_OUT_ENDPOINT

/*
 * The version VIA reports is the version the device reports.
 *
 * QMK leaves this at zero unless a keyboard says otherwise, which would have
 * the application show nothing while the USB descriptor carries a real number.
 * `DEVICE_VER` comes from keyboard.json's `device_version`, which is generated
 * from one declaration in tools/generate-qmk-layout.py -- so the two cannot
 * drift, rather than being checked for drift.
 */
#define VIA_FIRMWARE_VERSION DEVICE_VER

/*
 * Settle time between asserting a strobe and sampling the senses.
 *
 * The driver's own default is 20 us, which was a guess on what an optical
 * switch needs. Measured on hardware it is not enough: with a switch installed
 * and held down, its bit in raw_matrix never changes, while every position
 * that has no key at all reads as pressed. The scan runs, the map is right --
 * all twenty of those stuck bits land on positions the layout leaves empty,
 * which noise would not do -- and the one thing left between asserting a
 * strobe and reading a meaningful answer is time.
 *
 * These are not contacts. Each key interrupts an infrared beam, so a strobe
 * has to turn an emitter on and let a phototransistor settle before the sense
 * line means anything, and that is tens to hundreds of microseconds rather
 * than tens. At eight rows this costs four milliseconds a scan, which is a
 * 250 Hz matrix -- far more than a keyboard needs.
 */
#define MATRIX_STROBE_SETTLE_US 500
