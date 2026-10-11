# List of all the board related files.
#
# Unlike the STM32 boards, which point at a board ChibiOS already ships, this
# one carries its own: ChibiOS has no LPC546xx board definition to borrow.
BOARDSRC = $(BOARD_PATH)/boards/$(BOARD)/board/board.c

# Required include directories
BOARDINC = $(BOARD_PATH)/boards/$(BOARD)/board

# Shared variables
ALLCSRC += $(BOARDSRC)
ALLINC  += $(BOARDINC)
