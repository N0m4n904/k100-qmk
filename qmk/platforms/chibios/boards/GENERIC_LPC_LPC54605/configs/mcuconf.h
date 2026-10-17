// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

/*
 * Clock tree. The FRO runs at 48 MHz and feeds both the core and USB1, so no
 * PLL is configured at all -- USB1CLKSEL takes fro_hf directly. The port's
 * hal_lld.h refuses to build a configuration whose USB clock is not 48 MHz.
 *
 * The stock Corsair firmware runs the core at 180 MHz and the port can do that
 * -- lpc546xx_pll.c implements the system PLL -- but this configuration stays on
 * the FRO on purpose. Clocking the core and USB from the same 48 MHz source is
 * the arrangement with the fewest ways to be wrong, and nothing here needs the
 * speed: a keyboard matrix and three SPI LED drivers are not 180 MHz problems.
 * A keyboard that wants the PLL overrides LPC_MAINCLKSELB here.
 */
#define LPC_FROHF_SEL     LPC_FROHF_48MHZ
#define LPC_MAINCLKSELA   LPC_MAINCLKSELA_FROHF
#define LPC_MAINCLKSELB   LPC_MAINCLKSELB_SELA
#define LPC_AHBCLKDIV     0U
#define LPC_USB1CLKSEL    LPC_USB1CLKSEL_FROHF
#define LPC_USB1CLKDIV    0U

/*
 * USB. The shared driver calls its single instance USB1, and here that happens
 * to be right: it drives USBHSD, this part's high-speed device controller, which
 * is the one the K100's connector is wired to. See chibios/.../lpc_registry.h.
 */
#define LPC_USB_USE_USB1          TRUE
#define LPC_USB_USB1_IRQ_PRIORITY 3
