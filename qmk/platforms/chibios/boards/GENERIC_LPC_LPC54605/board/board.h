// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

/*
 * Setup for a bare LPC54605J512, knowing nothing about any particular board.
 *
 * A keyboard supplies its own pin configuration; this exists so that QMK has a
 * BOARD to name for every LPC54605 target.
 */
#define BOARD_GENERIC_LPC_LPC54605
#define BOARD_NAME "Generic LPC54605J512"

/*
 * The part macros cannot be defined here. cmparams.h includes NXP's
 * fsl_device_registers.h and is itself reached from crt1.c and vectors_lpc.S,
 * neither of which includes a board header, so by the time this file is read
 * the decision has been made. They come from mcu_selection.mk instead.
 */
#if !defined(CPU_LPC54605J512ET100)
#    error "CPU_LPC54605J512ET100 must be defined on the command line"
#endif
#if !defined(LPC54605)
#    error "LPC54605 must be defined on the command line"
#endif

#ifdef __cplusplus
extern "C" {
#endif
void boardInit(void);
#ifdef __cplusplus
}
#endif
