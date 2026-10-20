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
 * @file    LPC546xx/hal_spi_lld.c
 * @brief   LPC546xx SPI subsystem low level driver source.
 *
 * @addtogroup SPI
 * @{
 */

#include "hal.h"

#if (HAL_USE_SPI == TRUE) || defined(__DOXYGEN__)

/*===========================================================================*/
/* Driver local definitions.                                                 */
/*===========================================================================*/

/**
 * @brief   Frames discarded rather than delivered carry this.
 * @details A receive the caller does not want still has to be popped: the
 *          peripheral shifts in a frame for every frame shifted out, and an
 *          unread one eventually sets @p RXERR. @p RXIGNORE makes the hardware
 *          drop them instead of queueing them, which is both cheaper and the
 *          only way a send-only transfer can run longer than the FIFO.
 */
#define FRAME_RXIGNORE              SPI_FIFOWR_RXIGNORE_MASK

/**
 * @brief   Dummy data for frames sent only to clock a reply in.
 */
#define FRAME_DUMMY                 0xFFFFU

/*===========================================================================*/
/* Driver exported variables.                                                */
/*===========================================================================*/

#if LPC_SPI_USE_SPI3 == TRUE
/** @brief SPI3 driver identifier, which is Flexcomm 3. */
SPIDriver SPID3;
#endif

/*===========================================================================*/
/* Driver local functions.                                                   */
/*===========================================================================*/

/**
 * @brief   Enables a Flexcomm block's bus clock and gives it a function clock.
 *
 * @details Two separate things, and a block with only the first is a block
 *          whose registers read back sensibly while the peripheral does
 *          nothing. The function clock is what the SPI divider divides.
 *
 * @param[in] flexcomm  the block number
 * @return              the function clock frequency in Hz
 */
static uint32_t flexcomm_clock_enable(uint8_t flexcomm) {

  /* Blocks 0 to 7 are in AHBCLKCTRL1 from bit 11; 8 and 9 are in AHBCLKCTRL2
     from bit 14. Following NXP's own clock driver, whose kCLOCK_FlexComm3 is
     defined against AHB_CLK_CTRL1 bit 14. */
  if (flexcomm < 8U) {
    SYSCON->AHBCLKCTRLSET[1] = 1UL << (11U + flexcomm);
  }
  else {
    SYSCON->AHBCLKCTRLSET[2] = 1UL << (14U + (flexcomm - 8U));
  }

  /* Take the function clock from the main clock, which needs no PLL and is
     whatever this port was configured to run on.

     Selection zero is the main clock, and the figure returned has to be the
     main clock too. It was a literal 12 MHz, written alongside a comment
     meaning to select the 12 MHz FRO -- so the register said one thing and the
     arithmetic another. Every divider came out four times too small on a
     48 MHz main clock, and a bus asked for 12 MHz ran at 48. Nothing says so
     at the time: the peripheral is happy, the transfers complete, and only the
     device at the other end notices that it cannot follow. */
  SYSCON->FCLKSEL[flexcomm] = SYSCON_FCLKSEL_SEL(0U);

  return LPC_MAINCLK;
}

/**
 * @brief   Selects the SPI function of a Flexcomm block.
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 */
static void flexcomm_select_spi(SPIDriver *spip) {
  FLEXCOMM_Type *fc = (FLEXCOMM_Type *)(void *)spip->spi;

  osalDbgAssert((fc->PSELID & FLEXCOMM_PSELID_SPIPRESENT_MASK) != 0U,
                "this Flexcomm block has no SPI function");
  osalDbgAssert((fc->PSELID & FLEXCOMM_PSELID_LOCK_MASK) == 0U,
                "this Flexcomm block's function is locked to another one");

  fc->PSELID = FLEXCOMM_PSELID_PERSEL(LPC_FLEXCOMM_PERSEL_SPI);
}

/**
 * @brief   Pushes one frame, or as many as the transmit FIFO will take.
 *
 * @details The last frame of a transfer carries @p EOT, which is what makes the
 *          hardware release the chip select. Chip select is therefore asserted
 *          for exactly the duration of one ChibiOS transfer, which is the right
 *          granularity for a register write to an LED driver and the reason
 *          @p spi_lld_select() needs to do no work of its own.
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 */
static void spi_fill_tx(SPIDriver *spip) {
  SPI_Type *spi = spip->spi;

  while ((spip->tx_remaining > 0U) &&
         ((spi->FIFOSTAT & SPI_FIFOSTAT_TXNOTFULL_MASK) != 0U)) {
    uint32_t frame;

    if (spip->txptr == NULL) {
      frame = FRAME_DUMMY;
    }
    else if (spip->wide) {
      frame = *(const uint16_t *)(const void *)spip->txptr;
      spip->txptr += 2U;
    }
    else {
      frame = *spip->txptr;
      spip->txptr += 1U;
    }

    spip->tx_remaining--;

    /* EOT on the final frame only. */
    if (spip->tx_remaining == 0U) {
      frame |= SPI_FIFOWR_EOT_MASK;
    }
    if (spip->rxptr == NULL) {
      frame |= FRAME_RXIGNORE;
    }

    spi->FIFOWR = frame | spip->frame_ctrl;
  }
}

/**
 * @brief   Collects whatever the receive FIFO holds.
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 */
static void spi_drain_rx(SPIDriver *spip) {
  SPI_Type *spi = spip->spi;

  while ((spip->rx_remaining > 0U) &&
         ((spi->FIFOSTAT & SPI_FIFOSTAT_RXNOTEMPTY_MASK) != 0U)) {
    uint32_t frame = spi->FIFORD & SPI_FIFORD_RXDATA_MASK;

    if (spip->wide) {
      *(uint16_t *)(void *)spip->rxptr = (uint16_t)frame;
      spip->rxptr += 2U;
    }
    else {
      *spip->rxptr = (uint8_t)frame;
      spip->rxptr += 1U;
    }
    spip->rx_remaining--;
  }
}

/**
 * @brief   Starts a transfer and lets the interrupt handler run it out.
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 * @param[in] n         number of frames
 * @param[in] txbuf     data to send, or @p NULL to send dummy frames
 * @param[in] rxbuf     where to put the reply, or @p NULL to discard it
 */
static void spi_start_transfer(SPIDriver *spip, size_t n,
                               const void *txbuf, void *rxbuf) {
  SPI_Type *spi = spip->spi;

  spip->txptr = txbuf;
  spip->rxptr = rxbuf;
  spip->tx_remaining = n;
  spip->rx_remaining = (rxbuf == NULL) ? 0U : n;

  /* Clear any error left by a previous transfer; both bits are write-1. */
  spi->FIFOSTAT = SPI_FIFOSTAT_TXERR_MASK | SPI_FIFOSTAT_RXERR_MASK;

  /* Prime the FIFO before enabling interrupts, so a transfer shorter than the
     FIFO can complete without one. */
  spi_fill_tx(spip);

  spi->FIFOINTENSET = SPI_FIFOINTENSET_TXLVL_MASK |
                      SPI_FIFOINTENSET_RXLVL_MASK;
}

/*===========================================================================*/
/* Driver interrupt handlers.                                                */
/*===========================================================================*/

#if LPC_SPI_USE_SPI3 == TRUE
#if !defined(LPC_FLEXCOMM3_IRQ_VECTOR)
#error "LPC_FLEXCOMM3_IRQ_VECTOR is not defined"
#endif
/**
 * @brief   Flexcomm 3 interrupt handler.
 *
 * @isr
 */
OSAL_IRQ_HANDLER(LPC_FLEXCOMM3_IRQ_VECTOR) {

  OSAL_IRQ_PROLOGUE();
  spi_lld_serve_interrupt(&SPID3);
  OSAL_IRQ_EPILOGUE();
}
#endif

/*===========================================================================*/
/* Driver exported functions.                                                */
/*===========================================================================*/

/**
 * @brief   Shared interrupt service routine.
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 *
 * @notapi
 */
void spi_lld_serve_interrupt(SPIDriver *spip) {
  SPI_Type *spi = spip->spi;
  uint32_t status = spi->FIFOSTAT;

  /* A FIFO error means a frame was lost: the transmit FIFO was written while
     full, or the receive FIFO overflowed. Neither can be recovered from, so the
     transfer is ended.

     This version of the HAL's SPI API has no way to say so. Its completion
     callback carries no status -- the error callback is a v2 addition -- so the
     caller sees a transfer that finished, with a buffer that is short. Ending
     it is still better than the alternative, which is a caller waiting forever
     on frames that will never arrive. The error bits are left set for a reader
     with a debugger. */
  if ((status & (SPI_FIFOSTAT_TXERR_MASK | SPI_FIFOSTAT_RXERR_MASK)) != 0U) {
    spi->FIFOINTENCLR = SPI_FIFOINTENCLR_TXLVL_MASK |
                        SPI_FIFOINTENCLR_RXLVL_MASK;
    spip->tx_remaining = 0U;
    spip->rx_remaining = 0U;
    _spi_isr_code(spip);
    return;
  }

  spi_drain_rx(spip);
  spi_fill_tx(spip);

  /* Done when nothing is left to push and nothing is left to collect. The
     transmit FIFO still has to drain, which TXEMPTY reports. */
  if ((spip->tx_remaining == 0U) && (spip->rx_remaining == 0U) &&
      ((spi->FIFOSTAT & SPI_FIFOSTAT_TXEMPTY_MASK) != 0U)) {
    spi->FIFOINTENCLR = SPI_FIFOINTENCLR_TXLVL_MASK |
                        SPI_FIFOINTENCLR_RXLVL_MASK;
    _spi_isr_code(spip);
  }
}

/**
 * @brief   Low level SPI driver initialization.
 *
 * @notapi
 */
void spi_lld_init(void) {

#if LPC_SPI_USE_SPI3 == TRUE
  spiObjectInit(&SPID3);
  SPID3.spi = (SPI_Type *)SPI3_BASE;
  SPID3.flexcomm = 3U;
#endif
}

/**
 * @brief   Configures and activates the SPI peripheral.
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 *
 * @notapi
 */
void spi_lld_start(SPIDriver *spip) {
  const SPIConfig *config = spip->config;
  SPI_Type *spi = spip->spi;

  osalDbgAssert((config->width >= 4U) && (config->width <= 16U),
                "frame width out of range");
  osalDbgAssert(config->mode <= 3U, "SPI mode out of range");

  if (spip->state == SPI_STOP) {
    uint32_t fclk = flexcomm_clock_enable(spip->flexcomm);

    flexcomm_select_spi(spip);

#if LPC_SPI_USE_SPI3 == TRUE
    if (&SPID3 == spip) {
      nvicEnableVector(FLEXCOMM3_IRQn, LPC_SPI_SPI3_IRQ_PRIORITY);
    }
#endif

    /* The divider register holds one less than the ratio, so the slowest the
       bus can go is the function clock over 65536. Rounding up is what keeps
       the result at or below the requested rate. */
    uint32_t ratio = (fclk + config->speed - 1U) / config->speed;
    osalDbgAssert(ratio >= 1U, "requested bit rate is not achievable");
    if (ratio > 0x10000U) {
      ratio = 0x10000U;
    }
    spi->DIV = SPI_DIV_DIVVAL(ratio - 1U);
  }

  /* Frame width and chip select travel with each data word rather than living
     in a configuration register, so they are prepared here and ORed in on
     every push. LEN holds one less than the width. */
  spip->wide = config->width > 8U;
  spip->frame_ctrl = SPI_FIFOWR_LEN(config->width - 1U);

  /* The SSEL bits are "not asserted" flags: a one leaves that chip select
     idle. So all four are set, and the selected one cleared. */
  spip->frame_ctrl |= SPI_FIFOWR_TXSSEL0_N_MASK | SPI_FIFOWR_TXSSEL1_N_MASK |
                      SPI_FIFOWR_TXSSEL2_N_MASK | SPI_FIFOWR_TXSSEL3_N_MASK;
  if (config->ssel != LPC_SPI_SSEL_NONE) {
    osalDbgAssert(config->ssel <= LPC_SPI_SSEL3, "no such chip select");
    spip->frame_ctrl &= ~(SPI_FIFOWR_TXSSEL0_N_MASK << config->ssel);
  }

  uint32_t cfg = SPI_CFG_ENABLE_MASK | SPI_CFG_MASTER_MASK;
  if ((config->mode & 1U) != 0U) {
    cfg |= SPI_CFG_CPHA_MASK;
  }
  if ((config->mode & 2U) != 0U) {
    cfg |= SPI_CFG_CPOL_MASK;
  }
  /* SPOL asserts the chip select high, which is the unusual case. */
  if (config->ssel_active_high && (config->ssel != LPC_SPI_SSEL_NONE)) {
    cfg |= SPI_CFG_SPOL0_MASK << config->ssel;
  }
  if (config->loopback) {
    cfg |= SPI_CFG_LOOP_MASK;
  }

  /* Empty both FIFOs before enabling them: a stale frame would be shifted out
     as the first word of the next transfer. The empty bits are write-only
     commands rather than state. */
  spi->FIFOCFG = SPI_FIFOCFG_EMPTYTX_MASK | SPI_FIFOCFG_EMPTYRX_MASK;
  spi->FIFOCFG = SPI_FIFOCFG_ENABLETX_MASK | SPI_FIFOCFG_ENABLERX_MASK;
  spi->FIFOTRIG = 0U;

  spi->CFG = cfg;
}

/**
 * @brief   Deactivates the SPI peripheral.
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 *
 * @notapi
 */
void spi_lld_stop(SPIDriver *spip) {
  SPI_Type *spi = spip->spi;

  if (spip->state == SPI_READY) {
    spi->FIFOINTENCLR = SPI_FIFOINTENCLR_TXLVL_MASK |
                        SPI_FIFOINTENCLR_RXLVL_MASK;
    spi->FIFOCFG = 0U;
    spi->CFG = 0U;

#if LPC_SPI_USE_SPI3 == TRUE
    if (&SPID3 == spip) {
      nvicDisableVector(FLEXCOMM3_IRQn);
    }
#endif

    /* The function clock is left selected: switching it off here would make a
       later restart depend on the order drivers happen to be started in. */
  }
}

#if (SPI_SELECT_MODE == SPI_SELECT_MODE_LLD) || defined(__DOXYGEN__)
/**
 * @brief   Asserts the slave select signal and prepares for transfers.
 *
 * @details Nothing to do. Chip select is asserted by each frame pushed to
 *          @p FIFOWR and released by the frame carrying @p EOT, so it follows
 *          the transfer rather than bracketing it. Which line to use was
 *          settled in @p spi_lld_start().
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 *
 * @notapi
 */
void spi_lld_select(SPIDriver *spip) {

  (void)spip;
}

/**
 * @brief   Deasserts the slave select signal.
 *
 * @details Also nothing to do; see @p spi_lld_select().
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 *
 * @notapi
 */
void spi_lld_unselect(SPIDriver *spip) {

  (void)spip;
}
#endif

/**
 * @brief   Ignores data on the SPI bus.
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 * @param[in] n         number of frames to be ignored
 *
 * @notapi
 */
void spi_lld_ignore(SPIDriver *spip, size_t n) {

  spi_start_transfer(spip, n, NULL, NULL);
}

/**
 * @brief   Exchanges data on the SPI bus.
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 * @param[in] n         number of frames to be exchanged
 * @param[in] txbuf     the pointer to the transmit buffer
 * @param[out] rxbuf    the pointer to the receive buffer
 *
 * @notapi
 */
void spi_lld_exchange(SPIDriver *spip, size_t n,
                      const void *txbuf, void *rxbuf) {

  spi_start_transfer(spip, n, txbuf, rxbuf);
}

/**
 * @brief   Sends data over the SPI bus.
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 * @param[in] n         number of frames to send
 * @param[in] txbuf     the pointer to the transmit buffer
 *
 * @notapi
 */
void spi_lld_send(SPIDriver *spip, size_t n, const void *txbuf) {

  spi_start_transfer(spip, n, txbuf, NULL);
}

/**
 * @brief   Receives data from the SPI bus.
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 * @param[in] n         number of frames to receive
 * @param[out] rxbuf    the pointer to the receive buffer
 *
 * @notapi
 */
void spi_lld_receive(SPIDriver *spip, size_t n, void *rxbuf) {

  spi_start_transfer(spip, n, NULL, rxbuf);
}

/**
 * @brief   Exchanges one frame, synchronously.
 *
 * @param[in] spip      pointer to the @p SPIDriver object
 * @param[in] frame     the frame to send
 * @return              The received frame.
 *
 * @notapi
 */
uint16_t spi_lld_polled_exchange(SPIDriver *spip, uint16_t frame) {
  SPI_Type *spi = spip->spi;

  while ((spi->FIFOSTAT & SPI_FIFOSTAT_TXNOTFULL_MASK) == 0U) {
  }
  spi->FIFOWR = (uint32_t)frame | spip->frame_ctrl | SPI_FIFOWR_EOT_MASK;

  while ((spi->FIFOSTAT & SPI_FIFOSTAT_RXNOTEMPTY_MASK) == 0U) {
  }
  return (uint16_t)(spi->FIFORD & SPI_FIFORD_RXDATA_MASK);
}

#endif /* HAL_USE_SPI == TRUE */

/** @} */
