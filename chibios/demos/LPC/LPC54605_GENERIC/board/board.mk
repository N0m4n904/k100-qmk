# SPDX-License-Identifier: Apache-2.0
# List of all the board related files.
BOARDSRC = $(BOARDDIR)/board.c

# Required include directories
BOARDINC = $(BOARDDIR)

# Shared variables
ALLCSRC += $(BOARDSRC)
ALLINC  += $(BOARDINC)
