# List of the ChibiOS generic LPC546xx startup and CMSIS files.
#
# Differs from startup_lpc11uxx.mk in one way that matters: the core is a
# Cortex-M4F rather than an M0, so crt0_v7m.S replaces crt0_v6m.S.
#
# The vector table is shared with LPC11Uxx: vectors_lpc.S carries the boot ROM
# checksum that every LPC part needs, and the table length comes from
# CORTEX_NUM_VECTORS in the device's cmparams.h.
#
# NXP's device headers are found in either of two places, in this order:
#
#   1. vendored beside LPC11Uxx.h in os/common/ext/CMSIS/LPC/<part>/, which is
#      where ChibiOS-Contrib already keeps an NXP device header and where these
#      belong for any build that cannot fetch things -- QMK's, for one, which
#      vendors everything it needs;
#   2. NXP_SDK_DEVICE_DIR, an MCUXpresso SDK devices/<part> directory.
#
# Either satisfies the port, which is the point: it has no opinion on where the
# headers come from. They are BSD-3-Clause by their own SPDX identifier, so
# vendoring them is permitted provided the notice stays on them -- the SDK's
# top-level licence is a proprietary click-through, but per-file terms govern.
NXP_DEVICE_VENDORED := \
  $(CHIBIOS_CONTRIB)/os/common/ext/CMSIS/LPC/$(LPC_PART)

ifneq ("$(wildcard $(NXP_DEVICE_VENDORED)/fsl_device_registers.h)","")
  NXP_DEVICE_DIR := $(NXP_DEVICE_VENDORED)
else
  NXP_DEVICE_DIR := $(NXP_SDK_DEVICE_DIR)
endif

ifeq ($(NXP_DEVICE_DIR),)
  $(error no NXP device headers: set LPC_PART for a vendored copy, or \
          NXP_SDK_DEVICE_DIR to an MCUXpresso SDK devices/<part> directory)
endif

STARTUPSRC = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/crt1.c

STARTUPASM = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/crt0_v7m.S \
             $(CHIBIOS_CONTRIB)/os/common/startup/ARMCMx/compilers/GCC/vectors_lpc.S

STARTUPINC = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/ld \
             $(CHIBIOS_CONTRIB)/os/common/startup/ARMCMx/devices/LPC546xx \
             $(CHIBIOS)/os/common/ext/ARM/CMSIS/Core/Include \
             $(NXP_DEVICE_DIR)

STARTUPLD  = $(CHIBIOS)/os/common/startup/ARMCMx/compilers/GCC/ld
STARTUPLD_CONTRIB = $(CHIBIOS_CONTRIB)/os/common/startup/ARMCMx/compilers/GCC/ld

ALLXASMSRC += $(STARTUPASM)
ALLCSRC    += $(STARTUPSRC)
ALLINC     += $(STARTUPINC)
