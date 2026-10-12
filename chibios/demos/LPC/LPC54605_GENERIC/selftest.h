/*
    ChibiOS - Copyright (C) 2026 k100-qmk contributors

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
 * @file    selftest.h
 * @brief   What the port reports about itself on first power-up.
 *
 * @details The first time this port runs, nothing is known: whether the clock
 *          tree started, whether the SPI driver moves a byte, and -- on the
 *          keyboard rather than a devboard -- which pins the bootloader left
 *          configured. None of that can be read from the firmware images that
 *          exist, and none of it needs a UART, a USB host or a working LED to
 *          answer.
 *
 *          So the answers go into one structure in RAM, with a magic number in
 *          front of it, and a debugger reads them over SWD. That works on a
 *          board whose pins are unknown, which is the case this exists for.
 *
 *          @code
 *          (gdb) print/x selftest
 *          @endcode
 */

#ifndef SELFTEST_H
#define SELFTEST_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief   Marks the structure as filled in.
 * @details A debugger looking at uninitialised SRAM sees whatever was there
 *          before; this says the firmware got far enough to write it.
 */
#define SELFTEST_MAGIC 0x4B313030UL /* "K100" */

/** @brief IOCON ports and pins on this part. */
#define SELFTEST_PORTS 6
#define SELFTEST_PINS 32

/** @brief Outcome of one check. */
typedef enum {
    SELFTEST_SKIPPED = 0,
    SELFTEST_PASSED  = 1,
    SELFTEST_FAILED  = 2
} selftest_result_t;

/**
 * @brief   Everything the firmware can establish about itself without help.
 */
typedef struct {
    /** @brief @p SELFTEST_MAGIC once the structure is valid. */
    uint32_t magic;

    /** @brief Core clock the port believes it configured, in Hz. */
    uint32_t sysclk;
    /** @brief USB device clock, which has to be 48 MHz for USB to work. */
    uint32_t usbclk;
    /** @brief Flash wait states the clock tree selected. */
    uint8_t flash_waitstates;

    /** @brief Whether the kernel reached this code at all. */
    selftest_result_t kernel;

    /**
     * @brief   SPI moved a byte, checked through the peripheral's own loopback.
     * @details Needs no wiring, no second device and no knowledge of which pins
     *          the SPI signals are on -- the loop is inside the peripheral. So
     *          the SPI driver can be proven on a bare board.
     */
    selftest_result_t spi_loopback;
    /** @brief What came back, to tell a wrong byte from no byte. */
    uint16_t spi_sent;
    uint16_t spi_received;

    /** @brief A pad driven high then low and read back through its own input. */
    selftest_result_t pal_readback;

    /**
     * @brief   Every IOCON pad configuration, as found at startup.
     *
     * @details The point of the whole structure. On the keyboard the
     *          application firmware configures no Flexcomm pins, so the
     *          bootloader must, and the bootloader is not published -- this
     *          reports whatever it left, for all 192 pads, which is what
     *          identifies the LED driver's SPI pins. Captured before anything
     *          here reconfigures a pad.
     */
    uint32_t iocon[SELFTEST_PORTS][SELFTEST_PINS];

    /** @brief GPIO direction registers at startup, for the same reason. */
    uint32_t gpio_dir[SELFTEST_PORTS];
    /** @brief GPIO pin states at startup. */
    uint32_t gpio_pin[SELFTEST_PORTS];
} selftest_t;

/*
 * volatile deliberately: nothing in the program ever reads this structure -- a
 * debugger does -- so without it the stores are elided as writes nobody
 * observes. The definition also carries `used`, which is what stops link-time
 * optimisation removing the object itself; the attribute belongs on the
 * definition rather than here. With neither, no selftest symbol survived into
 * the image at all, which defeats the only purpose it has, so
 * chibios/test/build-demo.sh checks that it is still there.
 */
extern volatile selftest_t selftest;

/** @brief Captures the startup state. Call before configuring any pad. */
void selftest_capture_pins(void);

/** @brief Runs the checks that need drivers started. */
void selftest_run(void);

#endif /* SELFTEST_H */
