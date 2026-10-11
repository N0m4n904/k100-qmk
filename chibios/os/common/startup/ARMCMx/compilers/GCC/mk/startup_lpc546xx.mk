# List of the ChibiOS generic LPC546xx startup and CMSIS files.
#
# Differs from startup_lpc11uxx.mk in two ways. The core is a Cortex-M4F rather
# than an M0, so crt0_v7m.S replaces crt0_v6m.S. And the device headers are
# NXP's MCUXpresso SDK ones rather than a vendored CMSIS copy, so the SDK's
# devices/<part> directory has to be on the include path -- pass it in
# NXP_SDK_DEVICE_DIR. They are not redistributed with ChibiOS.
#
# The vector table is shared with LPC11Uxx: vectors_lpc.S carries the boot ROM
# checksum that every LPC part needs, and the table length comes from
# CORTEX_NUM_VECTORS in the device's cmparams.h.

ifeq ($(NXP_SDK_DEVICE_DIR),)
  $(error set NXP_SDK_DEVICE_DIR to the MCUXpresso SDK devices/<part> directory)
endif

STARTUPSRC = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/crt1.c

STARTUPASM = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/crt0_v7m.S \
             $(CHIBIOS_CONTRIB)/os/common/startup/ARMCMx/compilers/GCC/vectors_lpc.S

STARTUPINC = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/ld \
             $(CHIBIOS_CONTRIB)/os/common/startup/ARMCMx/devices/LPC546xx \
             $(CHIBIOS)/os/common/ext/ARM/CMSIS/Core/Include \
             $(NXP_SDK_DEVICE_DIR)

STARTUPLD  = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/ld
STARTUPLD_CONTRIB = $(CHIBIOS_CONTRIB)/os/common/startup/ARMCMx/compilers/GCC/ld

ALLXASMSRC += $(STARTUPASM)
ALLCSRC    += $(STARTUPSRC)
ALLINC     += $(STARTUPINC)
