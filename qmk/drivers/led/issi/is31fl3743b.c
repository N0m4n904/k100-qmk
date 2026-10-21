// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "is31fl3743b.h"
#include "is31fl3743b_transport.h"
#include "wait.h"

#include <string.h>

#ifndef IS31FL3743B_CONFIGURATION
#    define IS31FL3743B_CONFIGURATION IS31FL3743B_CONFIGURATION_NORMAL
#endif

/*
 * Each driver gets one buffer holding the two-byte frame header immediately
 * before its page, so a whole page is sent from where it was built with no copy
 * and no scatter-gather. The stock firmware allocates 204 bytes per driver for
 * exactly this reason, and the shape is worth keeping.
 */
#define HEADER_SIZE 2
#define FRAME_SIZE (HEADER_SIZE + IS31FL3743B_PWM_REGISTER_COUNT)

typedef struct {
    uint8_t frame[FRAME_SIZE];
    bool    dirty;
} driver_buffer_t;

static driver_buffer_t pwm_buffers[IS31FL3743B_DRIVER_COUNT];
static driver_buffer_t scaling_buffers[IS31FL3743B_DRIVER_COUNT];

/** @brief The page's values, past the header. */
static inline uint8_t *page_of(driver_buffer_t *buffer) {
    return &buffer->frame[HEADER_SIZE];
}

/**
 * @brief Sends a run of register values.
 *
 * @details The header occupies the two bytes before the values, so a caller
 *          that already has its values in a @p driver_buffer_t pays nothing to
 *          send them. Registers are numbered from one, which is why the channel
 *          index is offset: channel 0 is register 0x01.
 */
static void write_registers(uint8_t index, uint8_t page, uint8_t first_channel, uint8_t *frame, uint8_t count) {
    frame[0] = IS31FL3743B_COMMAND | page;
    frame[1] = first_channel + 1;

    is31fl3743b_transport_send(index, frame, HEADER_SIZE + count);
}

void is31fl3743b_write_register(uint8_t index, uint8_t page, uint8_t reg, uint8_t data) {
    uint8_t frame[HEADER_SIZE + 1];

    frame[0] = IS31FL3743B_COMMAND | page;
    frame[1] = reg;
    frame[2] = data;

    is31fl3743b_transport_send(index, frame, sizeof(frame));
}

void is31fl3743b_init_drivers(void) {
    is31fl3743b_transport_init();

    for (uint8_t i = 0; i < IS31FL3743B_DRIVER_COUNT; i++) {
        is31fl3743b_init(i);
    }

    /* Scaling is what bounds each channel's current, so it has to reach the
       drivers before anything is lit -- and it has to be given a value first.
       Resetting a driver leaves its buffer zeroed, and zero is not a dimmer
       setting, it is no current at all: the PWM values arrive, the transfers
       complete, and the keyboard stays dark. Full scale throughout is what the
       stock firmware writes. */
    for (uint8_t i = 0; i < IS31FL3743B_LED_COUNT; i++) {
        is31fl3743b_set_scaling_register(i, 0xFF, 0xFF, 0xFF);
    }

    for (uint8_t i = 0; i < IS31FL3743B_DRIVER_COUNT; i++) {
        is31fl3743b_update_scaling_registers(i);
    }
}

void is31fl3743b_init(uint8_t index) {
    /* Reset first: the driver may be mid-configuration from the stock firmware
       rather than fresh from power-on, since a warm restart does not
       necessarily power-cycle the LED drivers. */
    is31fl3743b_write_register(index, IS31FL3743B_PAGE_FUNCTION, IS31FL3743B_REG_RESET, IS31FL3743B_RESET_MAGIC);

    /* The datasheet's reset time is not known, so this follows the stock
       firmware's own margin rather than inventing one. */
    wait_ms(10);

    memset(&pwm_buffers[index], 0, sizeof(pwm_buffers[index]));
    memset(&scaling_buffers[index], 0, sizeof(scaling_buffers[index]));

    /* Immediately after the reset, and before anything else, which is the
       order the stock firmware uses. */
    is31fl3743b_write_register(index, IS31FL3743B_PAGE_FUNCTION, IS31FL3743B_REG_GLOBAL_CURRENT, IS31FL3743B_GLOBAL_CURRENT_VALUE);

    static const uint8_t undocumented[IS31FL3743B_DRIVER_COUNT] = IS31FL3743B_UNDOCUMENTED_VALUES;

    is31fl3743b_write_register(index, IS31FL3743B_PAGE_FUNCTION, IS31FL3743B_REG_UNDOCUMENTED, undocumented[index]);

    is31fl3743b_write_register(index, IS31FL3743B_PAGE_FUNCTION, IS31FL3743B_REG_CONFIGURATION, IS31FL3743B_CONFIGURATION);
}

void is31fl3743b_set_color(int index, uint8_t red, uint8_t green, uint8_t blue) {
    if (index < 0 || index >= IS31FL3743B_LED_COUNT) {
        return;
    }

    is31fl3743b_led_t led;
    memcpy_P(&led, (&g_is31fl3743b_leds[index]), sizeof(led));

    uint8_t *page = page_of(&pwm_buffers[led.driver]);

    if (page[led.r] == red && page[led.g] == green && page[led.b] == blue) {
        return;
    }

    page[led.r] = red;
    page[led.g] = green;
    page[led.b] = blue;

    pwm_buffers[led.driver].dirty = true;
}

void is31fl3743b_set_color_all(uint8_t red, uint8_t green, uint8_t blue) {
    for (int i = 0; i < IS31FL3743B_LED_COUNT; i++) {
        is31fl3743b_set_color(i, red, green, blue);
    }
}

void is31fl3743b_set_scaling_register(uint8_t index, uint8_t red, uint8_t green, uint8_t blue) {
    is31fl3743b_led_t led;
    memcpy_P(&led, (&g_is31fl3743b_leds[index]), sizeof(led));

    uint8_t *page = page_of(&scaling_buffers[led.driver]);

    page[led.r] = red;
    page[led.g] = green;
    page[led.b] = blue;

    scaling_buffers[led.driver].dirty = true;
}

void is31fl3743b_update_pwm_buffers(uint8_t index) {
    if (!pwm_buffers[index].dirty) {
        return;
    }

    write_registers(index, IS31FL3743B_PAGE_PWM, 0, pwm_buffers[index].frame, IS31FL3743B_PWM_REGISTER_COUNT);

    pwm_buffers[index].dirty = false;
}

void is31fl3743b_update_scaling_registers(uint8_t index) {
    if (!scaling_buffers[index].dirty) {
        return;
    }

    write_registers(index, IS31FL3743B_PAGE_SCALING, 0, scaling_buffers[index].frame, IS31FL3743B_SCALING_REGISTER_COUNT);

    scaling_buffers[index].dirty = false;
}

void is31fl3743b_flush(void) {
    for (uint8_t i = 0; i < IS31FL3743B_DRIVER_COUNT; i++) {
        is31fl3743b_update_pwm_buffers(i);
    }
}
