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
 * @file    LPC546xx/lpc_registry.h
 * @brief   LPC546xx capabilities registry.
 *
 * @addtogroup HAL
 * @{
 */

#ifndef LPC_REGISTRY_H_
#define LPC_REGISTRY_H_

/**
 * @brief   Sub-family identifier.
 */
#if defined(LPC54605) || defined(LPC54606) || defined(LPC54607) || \
    defined(LPC54608) || defined(LPC54616) || defined(LPC54618) || \
    defined(LPC54628) || defined(__DOXYGEN__)
#define LPC546xx
#else
#error unknown/unsupported LPC546xx microcontroller
#endif

#if defined(LPC546xx) || defined(__DOXYGEN__)

/*
 * Interrupt vector names. ChibiOS names an IRQ vector VectorXX where XX is the
 * hexadecimal byte offset into the vector table, so IRQn n is Vector(0x40 + 4n).
 * The IRQn values are NXP's, from the device header's IRQn_Type enumeration.
 */

/* USB attributes. USB0 is the full-speed device controller, which shares the
   NXP USB device IP that the shared LLD/USB driver already targets: an endpoint
   command/status list in SRAM, DEVCMDSTAT, and five logical endpoints. */
#define LPC_HAS_USB                 TRUE
#define LPC_USB_IRQ_VECTOR          VectorB0    /* USB0_IRQn = 28            */
#define LPC_USB_NEEDCLK_IRQ_VECTOR  VectorAC    /* USB0_NEEDCLK_IRQn = 27    */

/* Flexcomm. Each instance is one of USART, SPI, I2C or I2S, selected at
   runtime, so there is one vector per instance rather than per function.
   Flexcomm 3 is the one the stock keyboard firmware drives its LED drivers
   with; see docs/pin-mapping.md. */
#define LPC_HAS_FLEXCOMM            TRUE
#define LPC_FLEXCOMM0_IRQ_VECTOR    Vector74    /* FLEXCOMM0_IRQn = 14       */
#define LPC_FLEXCOMM1_IRQ_VECTOR    Vector78    /* FLEXCOMM1_IRQn = 15       */
#define LPC_FLEXCOMM2_IRQ_VECTOR    Vector7C    /* FLEXCOMM2_IRQn = 16       */
#define LPC_FLEXCOMM3_IRQ_VECTOR    Vector84    /* FLEXCOMM3_IRQn = 17       */

/* Standard timers. */
#define LPC_HAS_CTIMER              TRUE
#define LPC_CTIMER4_IRQ_VECTOR      VectorD4    /* CTIMER4_IRQn = 37         */

/* Pin interrupts. */
#define LPC_HAS_PINT                TRUE
#define LPC_PINT0_IRQ_VECTOR        Vector50    /* PIN_INT0_IRQn = 4         */

/* GPIO. Six ports of 32 pins, addressed as IOCON->PIO[port][pin]. */
#define LPC_HAS_GPIO0               TRUE
#define LPC_HAS_GPIO1               TRUE
#define LPC_HAS_GPIO2               TRUE
#define LPC_HAS_GPIO3               TRUE
#define LPC_HAS_GPIO4               TRUE
#define LPC_HAS_GPIO5               TRUE

/* Summed rather than stated, so the flags above stay the only place a port is
   declared present. Valid because the ports are numbered contiguously from
   zero on every part in the family. */
#define LPC_GPIO_PORTS              (LPC_HAS_GPIO0 + LPC_HAS_GPIO1 +        \
                                     LPC_HAS_GPIO2 + LPC_HAS_GPIO3 +        \
                                     LPC_HAS_GPIO4 + LPC_HAS_GPIO5)

#endif /* defined(LPC546xx) */

/** @} */

#endif /* LPC_REGISTRY_H_ */

/** @} */
