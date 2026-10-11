# List of all the LPC546xx platform files.
PLATFORMSRC := ${CHIBIOS}/os/hal/ports/common/ARMCMx/nvic.c \
               ${CHIBIOS_CONTRIB}/os/hal/ports/LPC/LPC546xx/hal_lld.c

# Required include directories
PLATFORMINC := ${CHIBIOS}/os/hal/ports/common/ARMCMx \
               ${CHIBIOS_CONTRIB}/os/hal/ports/LPC/LPC546xx

# Shared LPC low level drivers.
#
# Only the system timer is pulled in so far. It is plain Cortex-M SysTick and so
# is family independent, which is why it ports unchanged.
#
# The others in LLD/ are written against LPC11Uxx register names and are not
# included yet:
#
#   USB    the same NXP device IP as this part's USB0, with an endpoint
#          command/status list and five logical endpoints, so it should adapt
#          rather than be rewritten; it needs its register names mapped from
#          LPC11Uxx CMSIS to this part's.
#   GPIO   close, as both parts use IOCON plus byte and word pin registers.
#   SPI    needs rewriting: this part uses Flexcomm, unrelated to the LPC11Uxx
#          peripheral.
#   UART   likewise Flexcomm, and not needed by QMK; use SWO for debug output.
include $(CHIBIOS_CONTRIB)/os/hal/ports/LPC/LLD/STM/driver.mk

# Shared variables
ALLCSRC += $(PLATFORMSRC)
ALLINC  += $(PLATFORMINC)
