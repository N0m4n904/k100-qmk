/*
    ChibiOS - Copyright (C) 2026 k100-qmk contributors
    SPDX-License-Identifier: Apache-2.0

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#ifndef BOARD_H
#define BOARD_H

/*
 * Setup for the NXP LPCXpresso54628, board OM13098.
 *
 * Recommended as the first target for this port in preference to the keyboard,
 * for one reason: it carries an onboard Link2 debug probe speaking CMSIS-DAP
 * and J-Link, so the core can be halted and the part reflashed over SWD without
 * buying a probe. A first bring-up that does not boot is then a five-second
 * loop rather than a dead keyboard.
 *
 * Its MCU is the LPC54628 at up to 220 MHz against the keyboard's LPC54605 at
 * 180 MHz. Same family, same memory map, so the port and the linker script
 * apply unchanged; the clock configuration stays derived from LPC_FROHF_SEL and
 * the dividers rather than hardcoded, which is what makes that true.
 *
 * Pins below are from NXP's own SDK example for this board, not guessed.
 */
#define BOARD_LPCXPRESSO54628
#define BOARD_NAME                  "NXP LPCXpresso54628 (OM13098)"

/*
 * The part macros cannot be defined here. cmparams.h includes NXP's
 * fsl_device_registers.h and is itself reached from crt1.c and vectors_lpc.S,
 * neither of which includes a board header, so by the time this is read the
 * decision has been made. They belong on the command line.
 */
#if !defined(CPU_LPC54628J512ET180)
#error "define CPU_LPC54628J512ET180 on the command line, for NXP's headers"
#endif
#if !defined(LPC54628)
#error "define LPC54628 on the command line, for lpc_registry.h"
#endif

/**
 * @name    Board I/O
 * @details One of the board's three user LEDs and one of its user buttons.
 *          Both from NXP's gpio_led_output example, which drives port 2 pin 2
 *          and reads port 0 pin 6.
 * @{
 */
#define LINE_LED                    LPC_LINE(2U, 2U)
#define LINE_BUTTON                 LPC_LINE(0U, 6U)

/** @brief The LED is wired to sink through the pin, so low lights it. */
#define BOARD_LED_ACTIVE_LOW        TRUE

/** @brief The button pulls to ground, so it needs a pull-up and reads low. */
#define BOARD_BUTTON_ACTIVE_LOW     TRUE
/** @} */

/*
 * Worth knowing when reading the self-test's IOCON dump on this board: it has a
 * Winbond W25Q128 quad-SPI flash on the SPIFI pins, which are P0_23 to P0_28 at
 * FUNC6 -- the same six pins, at the same function, that the keyboard's firmware
 * configures. So this board can confirm that reading of them. See
 * docs/pin-mapping.md.
 *
 * Its LED pin also carries FC3_SSEL3 and its button pin carries FC3_SCK as
 * alternate functions, which is a coincidence worth not tripping over when
 * testing the LED driver's bus on this board.
 */

#ifdef __cplusplus
extern "C" {
#endif
  void boardInit(void);
#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
