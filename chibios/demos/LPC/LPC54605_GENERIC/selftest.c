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

/**
 * @file    selftest.c
 * @brief   What the port reports about itself on first power-up.
 *
 * @addtogroup HAL
 * @{
 */

#include "ch.h"
#include "hal.h"

#include "selftest.h"

/*
 * See the declaration for why this is volatile and used: nothing in the program
 * reads it, so both attributes are what keep it in the image.
 */
volatile selftest_t selftest __attribute__((used));

/**
 * @brief   A pad used for the loopback and read-back checks.
 *
 * @details Chosen to be harmless rather than meaningful. On the keyboard every
 *          pad does something, so a check that drives one has to pick a pad
 *          whose worst case is nothing: this is a matrix sense line, an input
 *          that the scan will reconfigure immediately afterwards.
 */
#define TEST_LINE LPC_LINE(1U, 0U)

void selftest_capture_pins(void) {

  /* Before anything reconfigures a pad. IOCON needs its clock to read back
     meaningfully, and palInit() is what enables it, so this runs after the HAL
     is up but before the application touches a pin. */
  for (unsigned port = 0; port < SELFTEST_PORTS; port++) {
    for (unsigned pin = 0; pin < SELFTEST_PINS; pin++) {
      selftest.iocon[port][pin] = IOCON->PIO[port][pin];
    }
    selftest.gpio_dir[port] = GPIO->DIR[port];
    selftest.gpio_pin[port] = GPIO->PIN[port];
  }
}

/**
 * @brief   Checks that a pad reads back what it is driven to.
 *
 * @details An output on this part is readable through its own input buffer when
 *          the pad is digital, so this needs no second pin and no wiring.
 */
static selftest_result_t check_pal(void) {

  palSetLineMode(TEST_LINE, PAL_MODE_OUTPUT_PUSHPULL);

  palSetLine(TEST_LINE);
  if (palReadLine(TEST_LINE) != PAL_HIGH) {
    return SELFTEST_FAILED;
  }

  palClearLine(TEST_LINE);
  if (palReadLine(TEST_LINE) != PAL_LOW) {
    return SELFTEST_FAILED;
  }

  /* Left as it was found, an input with a pull-up, so the check does not change
     how the board behaves afterwards. */
  palSetLineMode(TEST_LINE, PAL_MODE_INPUT_PULLUP);
  return SELFTEST_PASSED;
}

#if HAL_USE_SPI == TRUE
/**
 * @brief   Checks that the SPI driver moves a frame, through internal loopback.
 *
 * @details The loop is inside the peripheral, so this proves the clock gate,
 *          the Flexcomm function selection, the divider, the FIFO and the
 *          transfer interrupt without a single pin being connected and without
 *          knowing which pins the signals are on. That is the whole reason it
 *          is worth having: on this part those pins are not known.
 *
 *          What it cannot prove is anything outside the peripheral -- pin
 *          configuration, wiring, or that a device answers.
 */
static const SPIConfig loopback_config = {
  .end_cb           = NULL,
  .speed            = 1000000U,
  .mode             = 0U,
  .width            = 8U,
  .ssel             = LPC_SPI_SSEL_NONE,
  .ssel_active_high = false,
  .loopback         = true
};

static selftest_result_t check_spi(void) {
  static const uint8_t sent[4] = {0x5A, 0xA5, 0x00, 0xFF};
  static uint8_t received[sizeof sent];

  spiStart(&SPID3, &loopback_config);
  spiSelect(&SPID3);
  spiExchange(&SPID3, sizeof sent, sent, received);
  spiUnselect(&SPID3);
  spiStop(&SPID3);

  /* Report the first frame either way: a wrong byte and no byte at all are
     different faults, and a result of zero would otherwise look like both. */
  selftest.spi_sent = sent[0];
  selftest.spi_received = received[0];

  for (size_t i = 0; i < sizeof sent; i++) {
    if (received[i] != sent[i]) {
      return SELFTEST_FAILED;
    }
  }
  return SELFTEST_PASSED;
}
#endif

void selftest_run(void) {

  selftest.sysclk = LPC_SYSCLK;
  selftest.usbclk = LPC_USB1CLK;
  selftest.flash_waitstates = LPC_FLASHCFG_WAITSTATES;

  /* Reaching here at all means the clock tree did not hang, the vector table
     is right, and the kernel is scheduling. */
  selftest.kernel = SELFTEST_PASSED;

  selftest.pal_readback = check_pal();

#if HAL_USE_SPI == TRUE
  selftest.spi_loopback = check_spi();
#else
  selftest.spi_loopback = SELFTEST_SKIPPED;
#endif

  /* Last, so that a debugger finding the magic knows every other field above
     it has been written. */
  selftest.magic = SELFTEST_MAGIC;
}

/** @} */
