# List of all the board related files.
#
# Unlike the STM32 boards, which point at a board ChibiOS already ships, this
# one carries its own: ChibiOS has no LPC546xx board definition to borrow.
#
# For a board under platforms/chibios/boards, QMK sets BOARD_PATH to the board's
# own directory. It means something different for a board that lives inside a
# keyboard, where it is the keyboard directory -- so these paths are relative to
# this file's parent, not to a boards/ directory.
BOARDSRC = $(BOARD_PATH)/board/board.c

# Required include directories
BOARDINC = $(BOARD_PATH)/board

# Shared variables
ALLCSRC += $(BOARDSRC)
ALLINC  += $(BOARDINC)
