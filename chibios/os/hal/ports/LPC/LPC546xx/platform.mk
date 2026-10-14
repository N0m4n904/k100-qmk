# SPDX-License-Identifier: Apache-2.0
# List of all the LPC546xx platform files.
PLATFORMSRC := ${CHIBIOS}/os/hal/ports/common/ARMCMx/nvic.c \
               ${CHIBIOS_CONTRIB}/os/hal/ports/LPC/LPC546xx/hal_lld.c \
               ${CHIBIOS_CONTRIB}/os/hal/ports/LPC/LPC546xx/hal_pal_lld.c \
               ${CHIBIOS_CONTRIB}/os/hal/ports/LPC/LPC546xx/lpc546xx_usb.c \
               ${CHIBIOS_CONTRIB}/os/hal/ports/LPC/LPC546xx/hal_spi_lld.c \
               ${CHIBIOS_CONTRIB}/os/hal/ports/LPC/LPC546xx/lpc546xx_pll.c \
               ${CHIBIOS_CONTRIB}/os/hal/ports/LPC/LPC546xx/lpc546xx_eeprom.c

# Required include directories
PLATFORMINC := ${CHIBIOS}/os/hal/ports/common/ARMCMx \
               ${CHIBIOS_CONTRIB}/os/hal/ports/LPC/LPC546xx

# Shared LPC low level drivers.
#
#   STM    plain Cortex-M SysTick, so it ports unchanged.
#   USB    the same NXP device IP as this part's USB0. Register names are mapped
#          by lpc546xx_usb_compat.h and the clocking, transceiver power and
#          endpoint memory come from lpc546xx_usb.c. Requires the family seam
#          patch; see chibios/patches/README.md.
#
# The rest are not used:
#
#   GPIO   the GPIO registers match, but an iomode_t is mostly an IOCON word and
#          this part places every IOCON field at a different bit, so PAL is
#          family local here. See hal_pal_lld.h.
#   SPI    not used: the shared driver targets the LPC11Uxx SSP peripheral,
#          which this part does not have. Its SPI is a Flexcomm block with a
#          FIFO and chip selects carried in the data word, so hal_spi_lld.c
#          here is family local.
#   UART   likewise Flexcomm, and not needed by QMK; use SWO for debug output.
include $(CHIBIOS_CONTRIB)/os/hal/ports/LPC/LLD/STM/driver.mk
include $(CHIBIOS_CONTRIB)/os/hal/ports/LPC/LLD/USB/driver.mk

# Shared variables
ALLCSRC += $(PLATFORMSRC)
ALLINC  += $(PLATFORMINC)
