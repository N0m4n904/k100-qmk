// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * The K100's LED driver bus.
 *
 * Every number here was read out of the stock firmware rather than chosen, so
 * this is the configuration the hardware is known to work with:
 *
 *   SPI3, which is Flexcomm 3 at 0x40089000   the only SPI master it uses
 *   12 MHz, 8-bit frames, mode 0, MSB first   from its fsl_spi master config
 *   hardware chip selects, active low         sselNum per driver, sselPol 0
 *   chip select asserted per transfer         kSPI_FrameAssert, which is EOT
 *
 * The chip select each driver sits on comes from a four-byte-per-driver table
 * at flash 0x30052, which reads 0, 1, 2 -- so driver n is on SSEL n. See
 * docs/pin-mapping.md.
 *
 * Which pins carry those signals is still not written down anywhere, and this
 * driver still configures none -- but it no longer has to hope. IOCON was read
 * back over SWD under Corsair's bootloader and under this firmware, and port 0
 * is identical in both: the bootloader mixes the bus before it hands over, and
 * nothing here disturbs it. The pins arrive muxed.
 *
 * The clock agrees from both ends as well. The bootloader leaves Flexcomm 3
 * dividing its source by four, and this port's SPI driver selects the main
 * clock and asks for the 12 MHz above -- which on a 48 MHz main clock is the
 * same divider, and so the same bus speed the stock firmware uses.
 */

#include "is31fl3743b_transport.h"
#include "hal.h"

/** @brief 12 MHz, as the stock firmware's baudRate_Bps. */
#define K100_LED_SPI_SPEED 12000000U

/**
 * @brief One configuration per driver, differing only in the chip select.
 *
 * Three configurations rather than one mutated in place: a config is what
 * ChibiOS hands to @p spiStart(), and the chip select has to be settled before
 * a frame is pushed, since this peripheral carries it in the data word.
 */
static const SPIConfig led_spi_config[] = {
    {.end_cb = NULL, .speed = K100_LED_SPI_SPEED, .mode = 0U, .width = 8U, .ssel = LPC_SPI_SSEL0, .ssel_active_high = false},
    {.end_cb = NULL, .speed = K100_LED_SPI_SPEED, .mode = 0U, .width = 8U, .ssel = LPC_SPI_SSEL1, .ssel_active_high = false},
    {.end_cb = NULL, .speed = K100_LED_SPI_SPEED, .mode = 0U, .width = 8U, .ssel = LPC_SPI_SSEL2, .ssel_active_high = false},
};

#define DRIVER_COUNT (sizeof(led_spi_config) / sizeof(led_spi_config[0]))

void is31fl3743b_transport_init(void) {
    /* Nothing to do: each send starts the bus with its own driver's chip
       select, because that is the only thing that differs between them. */
}

void is31fl3743b_transport_send(uint8_t index, const uint8_t *frame, size_t length) {
    if (index >= DRIVER_COUNT) {
        return;
    }

    spiStart(&SPID3, &led_spi_config[index]);
    spiSelect(&SPID3);
    spiSend(&SPID3, length, frame);
    spiUnselect(&SPID3);
}
