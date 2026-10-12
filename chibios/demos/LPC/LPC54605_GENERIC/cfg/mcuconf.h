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

#ifndef MCUCONF_H
#define MCUCONF_H

#define LPC54605_MCUCONF

/*
 * Clock tree. Every one of these is the port's own default and is spelled out
 * only to show what the configuration surface is; omitting the block entirely
 * would build the same image.
 *
 * The FRO runs at 48 MHz and feeds both the core and USB0, so no PLL is set up
 * at all -- USB0CLKSEL can select fro_hf directly. hal_lld.h refuses to build a
 * configuration whose USB clock does not come out at 48 MHz.
 */
#define LPC_FROHF_SEL                       LPC_FROHF_48MHZ
#define LPC_MAINCLKSELA                     LPC_MAINCLKSELA_FROHF
#define LPC_MAINCLKSELB                     LPC_MAINCLKSELB_SELA
#define LPC_AHBCLKDIV                       0U
#define LPC_USB0CLKSEL                      LPC_USB0CLKSEL_FROHF
#define LPC_USB0CLKDIV                      0U

/*
 * USB. The shared driver calls its single instance USB1, which on this part is
 * the USB0 full-speed device controller -- the name is the driver's, not the
 * peripheral's.
 */
#define LPC_USB_USE_USB1                    TRUE
#define LPC_USB_USB1_IRQ_PRIORITY           3

/*
 * SPI3, which is Flexcomm 3 -- the block the stock keyboard firmware drives its
 * LED drivers with.
 */
#define LPC_SPI_USE_SPI3                    TRUE
#define LPC_SPI_SPI3_IRQ_PRIORITY           5

#endif /* MCUCONF_H */
