/*
    ChibiOS - Copyright (C) 2026 k100-qmk contributors
    SPDX-License-Identifier: Apache-2.0

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
 * @file    LPC546xx/hal_pal_lld.h
 * @brief   LPC546xx PAL subsystem low level driver header.
 *
 * @details This is a family-local driver rather than a use of the shared
 *          @p LLD/GPIO one, because the two families agree on the GPIO
 *          registers and disagree on everything else that matters.
 *
 *          The GPIO block is near enough identical: @p PIN, @p SET, @p CLR,
 *          @p NOT and @p DIR all exist here under the same names. But an
 *          @p iomode_t is mostly an IOCON pad configuration word, and LPC546xx
 *          places **every IOCON field at a different bit** from LPC11Uxx —
 *          @p FUNC widens from three bits to four and pushes the rest up, so
 *          the pull-mode, invert, digital-mode and open-drain fields all move.
 *          That encoding is the substance of a PAL driver; sharing the dozen
 *          register accessors around it would buy nothing and would mean
 *          reworking the one family already using them, untested.
 *
 *          Two things are consequently better here than in the shared driver:
 *          IOCON is reached as @p IOCON->PIO[port][pin] rather than through a
 *          hardcoded base address, which also lifts the two-port limit to this
 *          part's six, and pad direction uses the atomic @p DIRSET and
 *          @p DIRCLR registers that LPC11Uxx does not have.
 *
 * @addtogroup PAL
 * @{
 */

#ifndef HAL_PAL_LLD_H
#define HAL_PAL_LLD_H

#if (HAL_USE_PAL == TRUE) || defined(__DOXYGEN__)

/*
 * PAL events would be built on the PINT peripheral, which can map eight pins
 * from any port to its own interrupts. That is a layer of its own and is not
 * written yet, so the options that would need it are rejected rather than
 * silently answered with a dummy event.
 */
#if (PAL_USE_WAIT == TRUE) || (PAL_USE_CALLBACKS == TRUE)
#error "PAL events are not implemented on LPC546xx; they need the PINT driver"
#endif

/* Specifies palInit() without parameter, required until all platforms will
   be updated to the new style.*/
#define PAL_NEW_INIT

/*===========================================================================*/
/* Unsupported modes and specific modes                                      */
/*===========================================================================*/

#undef PAL_MODE_RESET
#undef PAL_MODE_UNCONNECTED
#undef PAL_MODE_INPUT
#undef PAL_MODE_INPUT_PULLUP
#undef PAL_MODE_INPUT_PULLDOWN
#undef PAL_MODE_INPUT_ANALOG
#undef PAL_MODE_OUTPUT_PUSHPULL
#undef PAL_MODE_OUTPUT_OPENDRAIN

/*===========================================================================*/
/* I/O Ports Types and constants.                                            */
/*===========================================================================*/

/**
 * @name    Port related definitions
 * @{
 */
/**
 * @brief   Width, in bits, of an I/O port.
 */
#define PAL_IOPORTS_WIDTH           32U

/**
 * @brief   Whole port mask.
 * @details This macro specifies all the valid bits into a port.
 */
#define PAL_WHOLE_PORT              ((ioportmask_t)0xFFFFFFFFU)
/** @} */

/**
 * @brief   Digital I/O port sized unsigned type.
 */
typedef uint32_t ioportmask_t;

/**
 * @brief   Digital I/O modes.
 *
 * @details The low sixteen bits are written to the pin's @p IOCON PIO register
 *          verbatim, and bit 31 carries the GPIO direction, which lives in a
 *          different peripheral and so has nowhere to sit inside the IOCON
 *          word. Splitting it out this way is what lets one @p iomode_t
 *          describe a pad completely.
 */
typedef uint32_t iomode_t;

/**
 * @name    Pad configuration bits
 *
 * @details These are derived from NXP's own @p IOCON_PIO_* field macros rather
 *          than from literal bit numbers, so a field this part places
 *          differently from its siblings cannot be got wrong here, and a field
 *          NXP renames becomes a compile error instead of a silent
 *          misconfiguration.
 *
 *          Names match the LPC11Uxx driver's wherever the two parts mean the
 *          same thing, so a board file reads the same across the LPC port even
 *          though the bit patterns differ. Fields this part does not have, such
 *          as LPC11Uxx's separate hysteresis control, are deliberately absent:
 *          a board asking for one should fail to build.
 * @{
 */
#define MODE_IOCONF_MASK        0x0000FFFFU

/**
 * @brief   Pin function select.
 * @param[in] n     function number, 0 selects GPIO
 * @note    Four bits wide here, against LPC11Uxx's three.
 */
#define MODE_FUNC(n)            IOCON_PIO_FUNC(n)
#define MODE_FUNC_DEFAULT       MODE_FUNC(0U)
#define MODE_FUNC_ALT1          MODE_FUNC(1U)
#define MODE_FUNC_ALT2          MODE_FUNC(2U)
#define MODE_FUNC_ALT3          MODE_FUNC(3U)
#define MODE_FUNC_ALT4          MODE_FUNC(4U)
#define MODE_FUNC_ALT5          MODE_FUNC(5U)
#define MODE_FUNC_ALT6          MODE_FUNC(6U)
#define MODE_FUNC_ALT7          MODE_FUNC(7U)

/** @brief  On-chip pull resistors. */
#define MODE_MODE_INACTIVE      IOCON_PIO_MODE(0U)
#define MODE_MODE_PULL_DOWN     IOCON_PIO_MODE(1U)
#define MODE_MODE_PULL_UP       IOCON_PIO_MODE(2U)
#define MODE_MODE_REPEATER      IOCON_PIO_MODE(3U)

/** @brief  Input polarity inversion. */
#define MODE_INV_DISABLE        IOCON_PIO_INVERT(0U)
#define MODE_INV_ENABLE         IOCON_PIO_INVERT(1U)

/**
 * @brief   Digital or analogue pad.
 * @note    NXP calls this field @p DIGIMODE; the LPC11Uxx driver's @p AD naming
 *          is kept because board files use it. **A pin left analogue reads
 *          nothing at all**, which is the easiest way to get a dead input on
 *          this part, so every digital mode below sets it explicitly.
 */
#define MODE_AD_ANALOG          IOCON_PIO_DIGIMODE(0U)
#define MODE_AD_DIGITAL         IOCON_PIO_DIGIMODE(1U)

/** @brief  Open-drain output. */
#define MODE_OD_ENABLE          IOCON_PIO_OD(1U)

/** @brief  Output slew rate, which LPC11Uxx has no equivalent of. */
#define MODE_SLEW_STANDARD      IOCON_PIO_SLEW(0U)
#define MODE_SLEW_FAST          IOCON_PIO_SLEW(1U)

/** @brief  Input glitch filter, which LPC11Uxx has no equivalent of. */
#define MODE_FILTER_ON          IOCON_PIO_FILTEROFF(0U)
#define MODE_FILTER_OFF         IOCON_PIO_FILTEROFF(1U)

/**
 * @brief   GPIO direction.
 * @note    Not an IOCON field; see @p iomode_t.
 */
#define MODE_DIR_POS            31U
#define MODE_DIR_MASK           (0x1U << MODE_DIR_POS)
#define MODE_DIR_IN             (0U << MODE_DIR_POS)
#define MODE_DIR_OUT            (1U << MODE_DIR_POS)
/** @} */

/**
 * @name    Standard PAL modes
 * @{
 */
#define PAL_MODE_RESET              (MODE_DIR_IN | MODE_MODE_INACTIVE |      \
                                     MODE_AD_DIGITAL | MODE_FUNC_DEFAULT)
#define PAL_MODE_INPUT              PAL_MODE_RESET
#define PAL_MODE_INPUT_PULLUP       (MODE_DIR_IN | MODE_MODE_PULL_UP |       \
                                     MODE_AD_DIGITAL | MODE_FUNC_DEFAULT)
#define PAL_MODE_INPUT_PULLDOWN     (MODE_DIR_IN | MODE_MODE_PULL_DOWN |     \
                                     MODE_AD_DIGITAL | MODE_FUNC_DEFAULT)
#define PAL_MODE_INPUT_ANALOG       (MODE_DIR_IN | MODE_MODE_INACTIVE |      \
                                     MODE_AD_ANALOG | MODE_FUNC_DEFAULT)
#define PAL_MODE_OUTPUT_PUSHPULL    (MODE_DIR_OUT | MODE_MODE_INACTIVE |     \
                                     MODE_AD_DIGITAL | MODE_FUNC_DEFAULT)
#define PAL_MODE_OUTPUT_OPENDRAIN   (MODE_DIR_OUT | MODE_MODE_INACTIVE |     \
                                     MODE_AD_DIGITAL | MODE_OD_ENABLE |      \
                                     MODE_FUNC_DEFAULT)

/**
 * @brief   Mode for a pad that is left unused.
 * @details Pulled down rather than left floating, so an unconnected input does
 *          not oscillate and burn current.
 */
#define PAL_MODE_UNCONNECTED        PAL_MODE_INPUT_PULLDOWN
/** @} */

/**
 * @brief   Generic I/O ports static initializer.
 * @details Empty: this part needs no port-wide state, every pad being
 *          configured individually through IOCON.
 */
typedef struct {

} PALConfig;

/**
 * @brief   Type of an I/O line.
 */
typedef uint32_t ioline_t;

/**
 * @brief   Port Identifier.
 */
typedef uint32_t ioportid_t;

/**
 * @brief   Type of an pad identifier.
 */
typedef uint32_t iopadid_t;

/*===========================================================================*/
/* I/O Ports Identifiers.                                                    */
/*===========================================================================*/

/**
 * @name    Port identifiers
 * @details A port identifier is its number shifted into the second byte, which
 *          leaves the low byte free for a pad number so that the two pack into
 *          one @p ioline_t.
 * @{
 */
#define LPC_IOPORT_ID(x)        ((ioportid_t)((x) << 8U))
#define LPC_IOPORT_NUM(x)       ((unsigned)((x) >> 8U))

#if LPC_HAS_GPIO0 || defined(__DOXYGEN__)
#define IOPORT0                 LPC_IOPORT_ID(0U)
#endif
#if LPC_HAS_GPIO1 || defined(__DOXYGEN__)
#define IOPORT1                 LPC_IOPORT_ID(1U)
#endif
#if LPC_HAS_GPIO2 || defined(__DOXYGEN__)
#define IOPORT2                 LPC_IOPORT_ID(2U)
#endif
#if LPC_HAS_GPIO3 || defined(__DOXYGEN__)
#define IOPORT3                 LPC_IOPORT_ID(3U)
#endif
#if LPC_HAS_GPIO4 || defined(__DOXYGEN__)
#define IOPORT4                 LPC_IOPORT_ID(4U)
#endif
#if LPC_HAS_GPIO5 || defined(__DOXYGEN__)
#define IOPORT5                 LPC_IOPORT_ID(5U)
#endif
/** @} */

/**
 * @name    Line handling macros
 * @{
 */
/**
 * @brief   Forms a line identifier.
 */
#define PAL_LINE(port, pad)                                                 \
  ((ioline_t)((uint32_t)(port) | (uint32_t)(pad)))

/**
 * @brief   Decodes a port identifier from a line identifier.
 */
#define PAL_PORT(line)                                                      \
  ((ioportid_t)((uint32_t)(line) & 0xFFFFFF00U))

/**
 * @brief   Decodes a pad identifier from a line identifier.
 */
#define PAL_PAD(line)                                                       \
  ((iopadid_t)((uint32_t)(line) & 0x000000FFU))

/**
 * @brief   Forms a line identifier from a port number and a pad.
 * @details Spelled the way the part's documentation and this project's pin
 *          mapping spell a pin, so @p LPC_LINE(1, 18) is P1_18.
 */
#define LPC_LINE(port, pad)     PAL_LINE(LPC_IOPORT_ID(port), (pad))

/**
 * @brief   Value identifying an invalid line.
 * @note    Not zero, unlike most ChibiOS ports: port identifiers here are small
 *          integers rather than peripheral addresses, so zero is P0_0.
 */
#define PAL_NOLINE              ((ioline_t)0xFFFFFFFFU)
/** @} */

/*===========================================================================*/
/* Implementation.                                                           */
/*===========================================================================*/

/**
 * @brief   Low level PAL subsystem initialization.
 *
 * @notapi
 */
#define pal_lld_init() _pal_lld_init()

/**
 * @brief   Reads the physical I/O port states.
 *
 * @notapi
 */
#define pal_lld_readport(port)                                              \
  ((ioportmask_t)(GPIO->PIN[LPC_IOPORT_NUM(port)]))

/**
 * @brief   Reads the output latch.
 *
 * @note    @p SET is read-write on this part while @p CLR and @p NOT are
 *          write-only, which is NXP's way of saying that reading @p SET returns
 *          the output register rather than the pin. That is what an output latch
 *          read should give, so it is used here in preference to @p PIN, which
 *          would report what the outside world is doing to the pin instead.
 *
 * @notapi
 */
#define pal_lld_readlatch(port)                                             \
  ((ioportmask_t)(GPIO->SET[LPC_IOPORT_NUM(port)]))

/**
 * @brief   Writes a bits mask on a I/O port.
 *
 * @notapi
 */
#define pal_lld_writeport(port, bits)                                       \
  do {                                                                      \
    GPIO->PIN[LPC_IOPORT_NUM(port)] = (uint32_t)(bits);                     \
  } while (false)

/**
 * @brief   Sets a bits mask on a I/O port.
 *
 * @notapi
 */
#define pal_lld_setport(port, bits)                                         \
  do {                                                                      \
    GPIO->SET[LPC_IOPORT_NUM(port)] = (uint32_t)(bits);                     \
  } while (false)

/**
 * @brief   Clears a bits mask on a I/O port.
 *
 * @notapi
 */
#define pal_lld_clearport(port, bits)                                       \
  do {                                                                      \
    GPIO->CLR[LPC_IOPORT_NUM(port)] = (uint32_t)(bits);                     \
  } while (false)

/**
 * @brief   Toggles a bits mask on a I/O port.
 *
 * @notapi
 */
#define pal_lld_toggleport(port, bits)                                      \
  do {                                                                      \
    GPIO->NOT[LPC_IOPORT_NUM(port)] = (uint32_t)(bits);                     \
  } while (false)

/**
 * @brief   Reads a logical state from an I/O pad.
 *
 * @details Through the byte pin registers, where one aligned byte access reads
 *          one pin as 0 or 1 with no masking or shifting.
 *
 * @notapi
 */
#define pal_lld_readpad(port, pad)                                          \
  ((GPIO->B[LPC_IOPORT_NUM(port)][pad] != 0U) ? PAL_HIGH : PAL_LOW)

/**
 * @brief   Writes a logical state on an output pad.
 *
 * @details Through the byte pin registers, so one pin is driven without a
 *          read-modify-write of the whole port and without disturbing the
 *          other thirty-one.
 *
 * @notapi
 */
#define pal_lld_writepad(port, pad, bit)                                    \
  do {                                                                      \
    GPIO->B[LPC_IOPORT_NUM(port)][pad] = (uint8_t)(bit);                    \
  } while (false)

/**
 * @brief   Sets a pad logical state to @p PAL_HIGH.
 *
 * @notapi
 */
#define pal_lld_setpad(port, pad)                                           \
  do {                                                                      \
    GPIO->B[LPC_IOPORT_NUM(port)][pad] = 1U;                                \
  } while (false)

/**
 * @brief   Clears a pad logical state to @p PAL_LOW.
 *
 * @notapi
 */
#define pal_lld_clearpad(port, pad)                                         \
  do {                                                                      \
    GPIO->B[LPC_IOPORT_NUM(port)][pad] = 0U;                                \
  } while (false)

/**
 * @brief   Pads group mode setup.
 *
 * @notapi
 */
#define pal_lld_setgroupmode(port, mask, offset, mode)                      \
  _pal_lld_setgroupmode(port, (ioportmask_t)(mask) << (offset), mode)

/**
 * @brief   Pad mode setup.
 *
 * @notapi
 */
#define pal_lld_setpadmode(port, pad, mode)                                 \
  _pal_lld_setpadmode(port, pad, mode)

#ifdef __cplusplus
extern "C" {
#endif
  void _pal_lld_init(void);
  void _pal_lld_setgroupmode(ioportid_t port,
                             ioportmask_t mask,
                             iomode_t mode);
  void _pal_lld_setpadmode(ioportid_t port,
                           iopadid_t pad,
                           iomode_t mode);
#ifdef __cplusplus
}
#endif

#endif /* HAL_USE_PAL == TRUE */

#endif /* HAL_PAL_LLD_H */

/** @} */
