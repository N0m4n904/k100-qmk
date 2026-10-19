// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

/*
 * Clock tree. The core runs from the FRO at 48 MHz, and USB1 does not.
 *
 * The FRO was feeding USB too, on the reasoning that one source for both is
 * the arrangement with the fewest ways to be wrong. For a full-speed device
 * that holds. This part's connector goes to USBHSD, the *high-speed*
 * controller, which the keyboard enumerates at 480 Mbit/s -- and USB asks for
 * better than 500 ppm where the FRO is a per-cent part. The stock firmware
 * drives USB1 from the dedicated USB PLL off the 12 MHz crystal, and so does
 * this.
 *
 * Both come out at 48 MHz. The difference is the source, not the number.
 *
 * The core stays on the FRO for now. The stock firmware runs it at 180 MHz
 * through the system PLL, which this port can also do, but moving it changes
 * every timing constant in the system at once -- matrix settling, SPI
 * dividers -- and the core clock is not implicated in anything USB does.
 * A keyboard that wants it overrides LPC_MAINCLKSELB here.
 */
#define LPC_FROHF_SEL     LPC_FROHF_48MHZ
#define LPC_MAINCLKSELA   LPC_MAINCLKSELA_FROHF
#define LPC_MAINCLKSELB   LPC_MAINCLKSELB_SELA
#define LPC_AHBCLKDIV     0U
#define LPC_USB1CLKSEL    LPC_USB1CLKSEL_USBPLL
#define LPC_USB1CLKDIV    0U

/*
 * USB. The shared driver calls its single instance USB1, and here that happens
 * to be right: it drives USBHSD, this part's high-speed device controller, which
 * is the one the K100's connector is wired to. See chibios/.../lpc_registry.h.
 */
#define LPC_USB_USE_USB1          TRUE
#define LPC_USB_USB1_IRQ_PRIORITY 3
