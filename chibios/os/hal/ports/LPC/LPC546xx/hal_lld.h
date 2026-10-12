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
 * @file    LPC546xx/hal_lld.h
 * @brief   LPC546xx HAL subsystem low level driver header.
 *
 * @addtogroup HAL
 * @{
 */

#ifndef HAL_LLD_H
#define HAL_LLD_H

/* NXP's device header, which selects the right per-part register definitions
   from the CPU macro. The MCUXpresso SDK's devices/<part> directory must be on
   the include path; see chibios/README.md. */
#include "fsl_device_registers.h"

#include "lpc_registry.h"

/*
 * The shared USB driver's header defines macros that expand inside ChibiOS's
 * own hal_usb.c -- usb_lld_wakeup_host() among them -- so the LPC11Uxx register
 * names they use have to be mapped wherever hal.h is included, not just in the
 * driver's own translation unit. This is the only place that is true of, since
 * hal.h includes this file before any driver header.
 *
 * Guarded, because the compat header has to #undef NXP's function-like field
 * setters to reuse their names as masks, and a build without USB has no reason
 * to lose them.
 */
#if defined(HAL_USE_USB) && (HAL_USE_USB == TRUE)
#include "lpc546xx_usb_compat.h"
#include "lpc546xx_usb.h"
#endif

/*===========================================================================*/
/* Driver constants.                                                         */
/*===========================================================================*/

/**
 * @brief   Platform name.
 */
#define PLATFORM_NAME           "LPC546xx"

/**
 * @name    Clock sources for MAINCLKSELA
 * @{
 */
#define LPC_MAINCLKSELA_FRO12M      0U  /**< @brief FRO at 12 MHz.          */
#define LPC_MAINCLKSELA_CLKIN       1U  /**< @brief External CLKIN.         */
#define LPC_MAINCLKSELA_WDTOSC      2U  /**< @brief Watchdog oscillator.    */
#define LPC_MAINCLKSELA_FROHF       3U  /**< @brief FRO at 48 or 96 MHz.    */
/** @} */

/**
 * @name    Clock sources for MAINCLKSELB
 * @{
 */
#define LPC_MAINCLKSELB_SELA        0U  /**< @brief Whatever SELA chose.    */
#define LPC_MAINCLKSELB_PLL         2U  /**< @brief System PLL output.      */
#define LPC_MAINCLKSELB_RTC32K      3U  /**< @brief RTC 32 kHz output.      */
/** @} */

/**
 * @name    Clock sources for USB0CLKSEL
 * @{
 */
#define LPC_USB0CLKSEL_FROHF        0U  /**< @brief FRO at 48 or 96 MHz.    */
#define LPC_USB0CLKSEL_PLL          1U  /**< @brief System PLL output.      */
#define LPC_USB0CLKSEL_USBPLL       2U  /**< @brief Dedicated USB PLL.      */
#define LPC_USB0CLKSEL_NONE         7U  /**< @brief Gated off.              */
/** @} */

/**
 * @name    Clock sources for SYSPLLCLKSEL
 * @{
 */
#define LPC_SYSPLLCLKSEL_FRO12M     0U  /**< @brief FRO at 12 MHz.          */
#define LPC_SYSPLLCLKSEL_CLKIN      1U  /**< @brief External CLKIN.         */
#define LPC_SYSPLLCLKSEL_WDTOSC     2U  /**< @brief Watchdog oscillator.    */
#define LPC_SYSPLLCLKSEL_RTC32K     3U  /**< @brief RTC 32 kHz.             */
#define LPC_SYSPLLCLKSEL_NONE       7U  /**< @brief Gated off.              */
/** @} */

/**
 * @name    FRO high-frequency output selection
 * @{
 */
#define LPC_FROHF_48MHZ             0U  /**< @brief 48 MHz.                 */
#define LPC_FROHF_96MHZ             1U  /**< @brief 96 MHz.                 */
/** @} */

/*===========================================================================*/
/* Driver pre-compile time settings.                                         */
/*===========================================================================*/

/**
 * @name    Configuration options
 * @{
 */

/**
 * @brief   FRO high-frequency output.
 * @details 48 MHz is the default because USB0 can take @p fro_hf directly, so
 *          a full-speed device needs no PLL at all.
 */
#if !defined(LPC_FROHF_SEL) || defined(__DOXYGEN__)
#define LPC_FROHF_SEL               LPC_FROHF_48MHZ
#endif

/**
 * @brief   Main clock source, first stage.
 */
#if !defined(LPC_MAINCLKSELA) || defined(__DOXYGEN__)
#define LPC_MAINCLKSELA             LPC_MAINCLKSELA_FROHF
#endif

/**
 * @brief   Main clock source, second stage.
 */
#if !defined(LPC_MAINCLKSELB) || defined(__DOXYGEN__)
#define LPC_MAINCLKSELB             LPC_MAINCLKSELB_SELA
#endif

/**
 * @brief   AHB clock divider, as the raw register value.
 * @details The divider applied is this value plus one, so 0 means divide by 1.
 */
#if !defined(LPC_AHBCLKDIV) || defined(__DOXYGEN__)
#define LPC_AHBCLKDIV               0U
#endif

/**
 * @brief   USB0 device clock source.
 */
#if !defined(LPC_USB0CLKSEL) || defined(__DOXYGEN__)
#define LPC_USB0CLKSEL              LPC_USB0CLKSEL_FROHF
#endif

/**
 * @brief   USB0 clock divider, as the raw register value.
 */
#if !defined(LPC_USB0CLKDIV) || defined(__DOXYGEN__)
#define LPC_USB0CLKDIV              0U
#endif

/**
 * @brief   System PLL input.
 * @details The 12 MHz FRO by default, which needs no crystal and is always
 *          running. A board with an external clock can select it instead.
 */
#if !defined(LPC_SYSPLLCLKSEL) || defined(__DOXYGEN__)
#define LPC_SYSPLLCLKSEL            LPC_SYSPLLCLKSEL_FRO12M
#endif

/**
 * @name    System PLL dividers
 * @details The output is @p input * M / (N * 2P). Defaults give 180 MHz from
 *          the 12 MHz FRO, which is what the stock keyboard firmware runs at:
 *          12 MHz * 30 / (1 * 2) = 180 MHz.
 *
 *          These are the real divider values, not the sequence values the
 *          registers hold; @p lpc546xx_pll.c encodes them.
 * @{
 */
#if !defined(LPC_SYSPLL_N) || defined(__DOXYGEN__)
#define LPC_SYSPLL_N                1U
#endif
#if !defined(LPC_SYSPLL_P) || defined(__DOXYGEN__)
#define LPC_SYSPLL_P                1U
#endif
#if !defined(LPC_SYSPLL_M) || defined(__DOXYGEN__)
#define LPC_SYSPLL_M                30U
#endif
/** @} */

/** @} */

/*===========================================================================*/
/* Derived constants and error checks.                                       */
/*===========================================================================*/

/**
 * @brief   FRO high-frequency output frequency.
 */
#if LPC_FROHF_SEL == LPC_FROHF_48MHZ
#define LPC_FROHFCLK                48000000U
#elif LPC_FROHF_SEL == LPC_FROHF_96MHZ
#define LPC_FROHFCLK                96000000U
#else
#error "invalid LPC_FROHF_SEL"
#endif

/**
 * @brief   System PLL input frequency.
 */
#if LPC_SYSPLLCLKSEL == LPC_SYSPLLCLKSEL_FRO12M
#define LPC_SYSPLLCLKIN             12000000U
#elif LPC_SYSPLLCLKSEL == LPC_SYSPLLCLKSEL_CLKIN
#if !defined(LPC_CLKIN_FREQUENCY)
#error "LPC_CLKIN_FREQUENCY must be defined to clock the PLL from CLKIN"
#endif
#define LPC_SYSPLLCLKIN             LPC_CLKIN_FREQUENCY
#else
#error "LPC_SYSPLLCLKSEL setting is not supported as a PLL input"
#endif

/**
 * @brief   System PLL output frequency.
 * @details @p input * M / (N * 2P), the post-divider always doubling.
 */
#define LPC_PLLCLK                                                          \
  ((LPC_SYSPLLCLKIN / (LPC_SYSPLL_N * 2U * LPC_SYSPLL_P)) * LPC_SYSPLL_M)

/**
 * @brief   Selected main clock source frequency, before the AHB divider.
 */
#if LPC_MAINCLKSELB == LPC_MAINCLKSELB_SELA
#if LPC_MAINCLKSELA == LPC_MAINCLKSELA_FROHF
#define LPC_MAINCLK                 LPC_FROHFCLK
#elif LPC_MAINCLKSELA == LPC_MAINCLKSELA_FRO12M
#define LPC_MAINCLK                 12000000U
#else
#error "LPC_MAINCLKSELA setting is not supported yet"
#endif
#elif LPC_MAINCLKSELB == LPC_MAINCLKSELB_PLL
#define LPC_MAINCLK                 LPC_PLLCLK
#else
#error "LPC_MAINCLKSELB setting is not supported yet"
#endif

/**
 * @brief   Core and AHB clock frequency.
 */
#define LPC_SYSCLK                  (LPC_MAINCLK / (LPC_AHBCLKDIV + 1U))

/**
 * @brief   Core clock, under the name the shared LPC drivers expect.
 * @details @p LLD/STM reloads SysTick from @p LPC_SYS_FREQUENCY and fails to
 *          build without it. That name is the shared pool's contract, so it is
 *          satisfied here rather than by patching the driver; @p LPC_SYSCLK is
 *          kept as the primary spelling because it is what ChibiOS ports call
 *          this clock elsewhere.
 */
#define LPC_SYS_FREQUENCY           LPC_SYSCLK

/**
 * @brief   USB0 device clock frequency.
 */
#if LPC_USB0CLKSEL == LPC_USB0CLKSEL_FROHF
#define LPC_USB0CLK                 (LPC_FROHFCLK / (LPC_USB0CLKDIV + 1U))
#else
#error "LPC_USB0CLKSEL setting is not supported yet"
#endif

#if LPC_HAS_USB && (LPC_USB0CLK != 48000000U)
#error "USB0 requires a 48 MHz clock; check LPC_FROHF_SEL and LPC_USB0CLKDIV"
#endif

/*
 * Flash access time. The part needs wait states as the core clock rises, and
 * the number is set in SYSCON->FLASHCFG. These thresholds follow NXP's own
 * clock driver; see hal_lld.c.
 */
#if LPC_SYSCLK <= 12000000U
#define LPC_FLASHCFG_WAITSTATES     0U
#elif LPC_SYSCLK <= 24000000U
#define LPC_FLASHCFG_WAITSTATES     1U
#elif LPC_SYSCLK <= 36000000U
#define LPC_FLASHCFG_WAITSTATES     2U
#elif LPC_SYSCLK <= 60000000U
#define LPC_FLASHCFG_WAITSTATES     3U
#elif LPC_SYSCLK <= 96000000U
#define LPC_FLASHCFG_WAITSTATES     4U
#elif LPC_SYSCLK <= 120000000U
#define LPC_FLASHCFG_WAITSTATES     5U
#elif LPC_SYSCLK <= 144000000U
#define LPC_FLASHCFG_WAITSTATES     6U
#elif LPC_SYSCLK <= 168000000U
#define LPC_FLASHCFG_WAITSTATES     7U
#elif LPC_SYSCLK <= 180000000U
#define LPC_FLASHCFG_WAITSTATES     8U
#else
#error "LPC_SYSCLK is above the part's maximum"
#endif

/*===========================================================================*/
/* Driver data structures and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Driver macros.                                                            */
/*===========================================================================*/

/**
 * @name    Clock points
 * @details @p halclkpt_t is a plain unsigned whose meanings each port chooses.
 *          The sibling family, and most ChibiOS ports without dynamic clock
 *          management, answer every query with zero; these frequencies are all
 *          known at compile time, so there is no reason to.
 * @{
 */
#define LPC_CLKPT_MAINCLK           0U  /**< @brief Main clock, pre-divider. */
#define LPC_CLKPT_SYSCLK            1U  /**< @brief Core and AHB clock.      */
#define LPC_CLKPT_FROHF             2U  /**< @brief FRO high-frequency out.  */
#define LPC_CLKPT_USB0              3U  /**< @brief USB0 device clock.       */
/** @} */

/**
 * @brief   Returns the frequency of a clock point in Hz.
 * @note    Static implementation: the clock tree is fixed at compile time, so
 *          this folds to a constant.
 *
 * @param[in] clkpt     clock point to be returned
 * @return              The clock point frequency in Hz, or zero if unknown.
 *
 * @notapi
 */
#define hal_lld_get_clock_point(clkpt)                                      \
  (((clkpt) == LPC_CLKPT_MAINCLK) ? (halfreq_t)LPC_MAINCLK :                \
   ((clkpt) == LPC_CLKPT_SYSCLK)  ? (halfreq_t)LPC_SYSCLK  :                \
   ((clkpt) == LPC_CLKPT_FROHF)   ? (halfreq_t)LPC_FROHFCLK :               \
   ((clkpt) == LPC_CLKPT_USB0)    ? (halfreq_t)LPC_USB0CLK : (halfreq_t)0)

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#include "nvic.h"

#ifdef __cplusplus
extern "C" {
#endif
  void hal_lld_init(void);
  void lpc_clock_init(void);
#if LPC_MAINCLKSELB == LPC_MAINCLKSELB_PLL
  void lpc_syspll_init(void);
#endif
#ifdef __cplusplus
}
#endif

#endif /* HAL_LLD_H */

/** @} */
