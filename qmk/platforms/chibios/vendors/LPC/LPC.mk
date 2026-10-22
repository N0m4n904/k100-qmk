# Copyright 2026 k100-qmk contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Puts this directory on the include path, which is the whole job.
#
# QMK's platforms/chibios/_pin_defs.h reaches a family's pin names with
# `#include_next "_pin_defs.h"`, so the family's directory has to be searched
# after that file's own. Without this the include_next finds nothing, QMK falls
# through to the names it defines itself -- STM32's -- and a P1_28 in a
# keyboard.json becomes an undeclared identifier in generated code, which reads
# as the keyboard naming a pin that does not exist rather than as a missing
# search path.
#
# Only reached when this family's pin names are needed from a header, which is
# why it went missing for so long: the matrix driver writes LPC_LINE() itself
# and never asked for them.

EXTRAINCDIRS += $(PLATFORM_PATH)/$(PLATFORM_KEY)/vendors/$(MCU_FAMILY)
