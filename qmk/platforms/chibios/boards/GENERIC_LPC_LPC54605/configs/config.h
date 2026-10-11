// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

/*
 * This part has no QMK bootloader driver, so QMK must not try to jump to one on
 * early init: there is nothing to jump to. Entering the stock Corsair
 * bootloader is a host-side operation over the Bragi protocol -- see
 * docs/bragi-firmware-update.md -- not something the firmware does to itself.
 */
#ifndef EARLY_INIT_PERFORM_BOOTLOADER_JUMP
#    define EARLY_INIT_PERFORM_BOOTLOADER_JUMP FALSE
#endif
