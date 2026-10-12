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
 * @file    LPC546xx/hal_spi_lld.h
 * @brief   LPC546xx SPI subsystem low level driver header.
 *
 * @details Written for this family rather than adapted from the shared
 *          @p LLD/SPI driver, which targets the LPC11Uxx SSP peripheral. This
 *          part has no SSP: each of its ten Flexcomm blocks becomes a USART,
 *          SPI, I2C or I2S at runtime, and the SPI it becomes has a different
 *          register set, a FIFO, and chip selects carried in the data word.
 *
 *          That last point is the one that shapes this driver. A frame written
 *          to @p FIFOWR carries, beside the data, which of four chip selects to
 *          assert, the frame width, and whether this is the end of the
 *          transfer. Chip select is therefore not a separate pin this driver
 *          toggles -- it is a property of each word pushed, which is why
 *          @p spi_lld_select() here only records which line to use.
 *
 * @addtogroup SPI
 * @{
 */

#ifndef HAL_SPI_LLD_H
#define HAL_SPI_LLD_H

#if (HAL_USE_SPI == TRUE) || defined(__DOXYGEN__)

/*===========================================================================*/
/* Driver constants.                                                         */
/*===========================================================================*/

/**
 * @brief   Circular transfers are not implemented.
 */
#define SPI_SUPPORTS_CIRCULAR       FALSE

/**
 * @brief   Slave mode is not implemented.
 * @details The peripheral supports it; nothing this port exists for needs it.
 */
#define SPI_SUPPORTS_SLAVE_MODE     FALSE

/**
 * @brief   Value selecting the Flexcomm block's SPI function.
 * @details Written to @p PSELID. From NXP's @p FLEXCOMM_PERIPH_T, where the
 *          order is none, USART, SPI, I2C, I2S TX, I2S RX.
 */
#define LPC_FLEXCOMM_PERSEL_SPI     2U

/**
 * @name    Hardware chip selects
 * @details Four per block, asserted per frame rather than per transfer.
 * @{
 */
#define LPC_SPI_SSEL0               0U
#define LPC_SPI_SSEL1               1U
#define LPC_SPI_SSEL2               2U
#define LPC_SPI_SSEL3               3U

/**
 * @brief   Drive no hardware chip select.
 * @details For a board whose chip selects are ordinary GPIO, which is also
 *          what @p SPI_SELECT_MODE_PAD expects.
 */
#define LPC_SPI_SSEL_NONE           0xFFU
/** @} */

/*===========================================================================*/
/* Driver pre-compile time settings.                                         */
/*===========================================================================*/

/**
 * @name    Configuration options
 * @{
 */

/**
 * @brief   SPI3 driver enable switch.
 * @details SPI3 is Flexcomm 3, which is the block the stock keyboard firmware
 *          drives its LED drivers with; see @p docs/pin-mapping.md.
 */
#if !defined(LPC_SPI_USE_SPI3) || defined(__DOXYGEN__)
#define LPC_SPI_USE_SPI3            FALSE
#endif

/**
 * @brief   SPI3 interrupt priority.
 */
#if !defined(LPC_SPI_SPI3_IRQ_PRIORITY) || defined(__DOXYGEN__)
#define LPC_SPI_SPI3_IRQ_PRIORITY   5
#endif

/** @} */

/*===========================================================================*/
/* Derived constants and error checks.                                       */
/*===========================================================================*/

#if LPC_SPI_USE_SPI3 && !defined(LPC_HAS_FLEXCOMM3)
#error "SPI3 is not available on this part"
#endif

#if !LPC_SPI_USE_SPI3
#error "SPI is enabled but no SPI instance is"
#endif

/*===========================================================================*/
/* Driver data structures and types.                                         */
/*===========================================================================*/

/*===========================================================================*/
/* Driver macros.                                                            */
/*===========================================================================*/

/**
 * @brief   Low level fields of the SPI driver structure.
 */
#define spi_lld_driver_fields                                               \
  /* Pointer to the SPI registers of this instance.                      */ \
  SPI_Type                  *spi;                                           \
  /* Which Flexcomm block this is, for its clock gate and clock select.  */ \
  uint8_t                   flexcomm;                                       \
  /* Frame control bits ORed into every FIFOWR: chip select and width.   */ \
  uint32_t                  frame_ctrl;                                     \
  /* Frames still to push, and still to collect.                         */ \
  size_t                    tx_remaining;                                   \
  size_t                    rx_remaining;                                   \
  /* Transfer buffers. NULL means synthesise or discard frames.          */ \
  const uint8_t             *txptr;                                         \
  uint8_t                   *rxptr;                                         \
  /* TRUE when frames are wider than eight bits, so buffers step by two. */ \
  bool                      wide;

/**
 * @brief   Low level fields of the SPI configuration structure.
 */
#define spi_lld_config_fields                                               \
  /* Bit rate in Hz. The achievable rate is the Flexcomm function clock  */ \
  /* divided by an integer, so the result is the nearest rate at or      */ \
  /* below this one.                                                     */ \
  uint32_t                  speed;                                          \
  /* Clock polarity and phase, 0 to 3, numbered as everyone numbers      */ \
  /* SPI modes: mode 2 and 3 idle the clock high.                        */ \
  uint8_t                   mode;                                           \
  /* Frame width in bits, 4 to 16.                                       */ \
  uint8_t                   width;                                          \
  /* Hardware chip select to assert, or LPC_SPI_SSEL_NONE.               */ \
  uint8_t                   ssel;                                           \
  /* TRUE for a chip select that idles low and asserts high.             */ \
  bool                      ssel_active_high;

/*===========================================================================*/
/* External declarations.                                                    */
/*===========================================================================*/

#if (LPC_SPI_USE_SPI3 == TRUE) && !defined(__DOXYGEN__)
extern SPIDriver SPID3;
#endif

#ifdef __cplusplus
extern "C" {
#endif
  void spi_lld_init(void);
  void spi_lld_serve_interrupt(SPIDriver *spip);
  void spi_lld_start(SPIDriver *spip);
  void spi_lld_stop(SPIDriver *spip);
#if (SPI_SELECT_MODE == SPI_SELECT_MODE_LLD) || defined(__DOXYGEN__)
  void spi_lld_select(SPIDriver *spip);
  void spi_lld_unselect(SPIDriver *spip);
#endif
  void spi_lld_ignore(SPIDriver *spip, size_t n);
  void spi_lld_exchange(SPIDriver *spip, size_t n,
                        const void *txbuf, void *rxbuf);
  void spi_lld_send(SPIDriver *spip, size_t n, const void *txbuf);
  void spi_lld_receive(SPIDriver *spip, size_t n, void *rxbuf);
  uint16_t spi_lld_polled_exchange(SPIDriver *spip, uint16_t frame);
#ifdef __cplusplus
}
#endif

#endif /* HAL_USE_SPI == TRUE */

#endif /* HAL_SPI_LLD_H */

/** @} */
