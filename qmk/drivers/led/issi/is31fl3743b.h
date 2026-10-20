// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "progmem.h"
#include "util.h"

/*
 * ISSI IS31FL3743B: 18 x 11 constant-current channels, driven over SPI.
 *
 * The sibling IS31FL3743A in QMK's tree is the same LED matrix behind an I2C
 * interface. The register model is shared with it -- three pages, PWM and
 * scaling values one byte per channel -- and the transport is not.
 *
 * Every constant below was read out of the K100's own firmware rather than
 * from a datasheet, which was not obtainable. docs/pin-mapping.md shows the
 * instructions each came from.
 */

/** @brief Channels per driver: 18 current sinks by 11 switch rows. */
#define IS31FL3743B_PWM_REGISTER_COUNT 198
#define IS31FL3743B_SCALING_REGISTER_COUNT 198

/**
 * @brief Command byte prefix.
 *
 * Every frame opens with this ORed with the page number, so a PWM write starts
 * 0x50 and a scaling write 0x51. The firmware builds it as `orr r1, r1, #0x50`.
 */
#define IS31FL3743B_COMMAND 0x50

/** @name Pages */
#define IS31FL3743B_PAGE_PWM 0
#define IS31FL3743B_PAGE_SCALING 1
#define IS31FL3743B_PAGE_FUNCTION 2

/** @name Function registers, on page 2 */
#define IS31FL3743B_REG_CONFIGURATION 0x00
#define IS31FL3743B_REG_RESET 0x2F

/**
 * @brief Configuration register values.
 *
 * Taken from the firmware, which writes 0x09 to run and 0x08 to shut down --
 * the two differing in the software-shutdown bit. The remaining bit is set in
 * both cases, so it is reproduced rather than interpreted.
 */
#define IS31FL3743B_CONFIGURATION_NORMAL 0x09
#define IS31FL3743B_CONFIGURATION_SHUTDOWN 0x08

/** @brief Written to the reset register to restore defaults. */
#define IS31FL3743B_RESET_MAGIC 0xAE

/**
 * @brief   A function register the stock firmware writes, and this one must.
 *
 * @details Its purpose is not identified. It is reproduced because leaving it
 *          out was one of the reasons this keyboard stayed dark with every
 *          other part of the path demonstrably working, and because the I2C
 *          sibling's global current control -- the register that decides
 *          whether any current flows at all -- sits at `0x01`, which the stock
 *          firmware never writes. A part whose current control had moved would
 *          explain both.
 *
 *          The stock firmware writes `0xC0` or `0x80` depending on a flag.
 *          The lower is reproduced: if this is a current control then that is
 *          the gentler of the two, and if it is not, it is still one of the
 *          two states their firmware puts the part in.
 */
#define IS31FL3743B_REG_UNDOCUMENTED 0x25
#define IS31FL3743B_UNDOCUMENTED_VALUE 0x80

#if defined(RGB_MATRIX_IS31FL3743B)
#    define IS31FL3743B_LED_COUNT RGB_MATRIX_LED_COUNT
#endif

#if !defined(IS31FL3743B_DRIVER_COUNT)
#    define IS31FL3743B_DRIVER_COUNT 3
#endif

/**
 * @brief Which driver and which three channels light one RGB LED.
 *
 * Generated from the firmware's LedInfoMap; see tools/generate-led-tables.py.
 */
typedef struct is31fl3743b_led_t {
    uint8_t driver : 2;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} PACKED is31fl3743b_led_t;

extern const is31fl3743b_led_t PROGMEM g_is31fl3743b_leds[IS31FL3743B_LED_COUNT];

void is31fl3743b_init_drivers(void);
void is31fl3743b_init(uint8_t index);

void is31fl3743b_write_register(uint8_t index, uint8_t page, uint8_t reg, uint8_t data);

void is31fl3743b_set_color(int index, uint8_t red, uint8_t green, uint8_t blue);
void is31fl3743b_set_color_all(uint8_t red, uint8_t green, uint8_t blue);

void is31fl3743b_set_scaling_register(uint8_t index, uint8_t red, uint8_t green, uint8_t blue);

void is31fl3743b_update_pwm_buffers(uint8_t index);
void is31fl3743b_update_scaling_registers(uint8_t index);

void is31fl3743b_flush(void);
