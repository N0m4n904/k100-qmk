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

/*
 * The smallest program that exercises every layer of the LPC546xx port.
 *
 * Its purpose is to be *built*, not to do anything useful: it is what turns
 * "the register names are right" into "this links as a ChibiOS HAL". So it
 * deliberately touches each driver at least once -- the clock tree through
 * halInit(), the kernel and SysTick through chSysInit() and chThdSleep(), PAL
 * through a pad it drives and a pad it reads, and the USB device driver through
 * usbStart(), which is what pulls in the endpoint allocator, the interrupt
 * handler and the family's clocking and power hooks.
 *
 * Nothing here has been run on hardware.
 */

#include "ch.h"
#include "hal.h"

#include "selftest.h"

/*
 * A board that has an LED says so; otherwise an arbitrary pad, chosen only
 * because every LPC54605 package has it and there is no board for it to be
 * connected to. On a board with an LED this is the whole point of the first
 * power-up: something visible, without a debugger.
 */
#if defined(LINE_LED)
#define LINE_OUT        LINE_LED
#else
#define LINE_OUT        LPC_LINE(1U, 18U)
#endif

#if defined(LINE_BUTTON)
#define LINE_IN         LINE_BUTTON
#else
#define LINE_IN         LPC_LINE(1U, 9U)
#endif

/*
 * The minimum USB configuration the HAL will accept. The callbacks do nothing
 * and the descriptor hook returns nothing, so this device would never finish
 * enumerating -- descriptors are a keyboard's business and belong with the
 * keyboard. What matters here is that usbStart() reaches the driver.
 */
static const USBDescriptor *get_descriptor(USBDriver *usbp,
                                           uint8_t dtype,
                                           uint8_t dindex,
                                           uint16_t lang) {
  (void)usbp;
  (void)dtype;
  (void)dindex;
  (void)lang;

  return NULL;
}

/*
 * The LED drivers' bus, as the stock firmware uses it: 8-bit frames, mode 0,
 * and one of the four hardware chip selects. The pins this lands on are not
 * known -- see docs/pin-mapping.md -- so nothing here configures any, and on
 * real hardware the board would.
 */
static const SPIConfig spicfg = {
  .end_cb           = NULL,
  .speed            = 1000000U,
  .mode             = 0U,
  .width            = 8U,
  .ssel             = LPC_SPI_SSEL0,
  .ssel_active_high = false,
  .loopback         = false
};

static const USBConfig usbcfg = {
  .event_cb     = NULL,
  .get_descriptor_cb = get_descriptor,
  .requests_hook_cb = NULL,
  .sof_cb       = NULL
};

int main(void) {

  halInit();
  chSysInit();

  /* Before any pad is reconfigured: on the keyboard this is the only chance to
     see what the bootloader left, which is what identifies the LED driver's
     SPI pins. See selftest.h. */
  selftest_capture_pins();

  palSetLineMode(LINE_OUT, PAL_MODE_OUTPUT_PUSHPULL);
  palSetLineMode(LINE_IN, PAL_MODE_INPUT_PULLUP);

  /* A blink rate that changes when the button is pressed, so one LED reports
     both that the kernel is scheduling and that an input is being read. */

  usbStart(&USBD1, &usbcfg);

  /* Everything the firmware can establish about itself unaided, left in RAM
     for a debugger: the clock tree, a pad driven and read back, and a SPI frame
     through the peripheral's own loopback. */
  selftest_run();

  spiStart(&SPID3, &spicfg);

  while (true) {
    palToggleLine(LINE_OUT);
    chThdSleepMilliseconds(palReadLine(LINE_IN) == PAL_HIGH ? 500 : 100);
  }
}
