// Copyright 2026 k100-qmk contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

/*
 * Clock tree. The FRO runs at 48 MHz and feeds both the core and USB0, so no
 * PLL is configured at all -- USB0CLKSEL takes fro_hf directly. The port's
 * hal_lld.h refuses to build a configuration whose USB clock is not 48 MHz.
 *
 * The stock Corsair firmware runs the core at 180 MHz. Matching that needs the
 * PLL, which the port does not implement yet; a keyboard wanting it overrides
 * LPC_MAINCLKSELB here once it does.
 */
#define LPC_FROHF_SEL     LPC_FROHF_48MHZ
#define LPC_MAINCLKSELA   LPC_MAINCLKSELA_FROHF
#define LPC_MAINCLKSELB   LPC_MAINCLKSELB_SELA
#define LPC_AHBCLKDIV     0U
#define LPC_USB0CLKSEL    LPC_USB0CLKSEL_FROHF
#define LPC_USB0CLKDIV    0U

/*
 * USB. The shared driver calls its single instance USB1; on this part that is
 * the USB0 full-speed device controller. The name is the driver's, not the
 * peripheral's.
 */
#define LPC_USB_USE_USB1          TRUE
#define LPC_USB_USB1_IRQ_PRIORITY 3
