// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Matrix internals shared with the bootloader-entry check.
 *
 * The pin numbers, pad modes and timing all stay in matrix.c, which owns them.
 * What the bootloader check needs is the ability to read one row before QMK has
 * initialised the matrix, so these two are exposed rather than duplicated.
 */

/** Configures the strobes as outputs and the senses as pulled-up inputs. */
void k100_matrix_configure_pins(void);

/**
 * Asserts one strobe, waits out the settling time and returns the raw senses.
 *
 * Raw in both senses that matter: no debounce, and no polarity correction, so
 * bit N is the level on sense N rather than "key N is pressed". The bootloader
 * check wants exactly that, because it works out the pressed level for itself
 * instead of trusting MATRIX_SENSE_ACTIVE_LOW.
 */
uint32_t k100_matrix_sample_row(uint8_t row);

/**
 * Writes Corsair's operating mode and restarts into the bootloader.
 *
 * Does not return on success. Returns false if the EEPROM word could not be
 * programmed, in which case resetting would only loop back here -- see the
 * read-back in bootloader.c. Also reached through QMK's QK_BOOT keycode, which
 * calls bootloader_jump().
 */
bool k100_enter_bootloader(void);

#ifdef K100_USB_DIAGNOSTIC
/**
 * Marks the first matrix scan of the main loop; see bootloader.c. Diagnostic
 * only, and compiled out with the rest of the delay channel.
 */
void k100_diagnostic_mark_first_scan(void);
#endif
