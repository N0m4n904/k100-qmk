# List of all the LPCXpresso54628 board files.
BOARDSRC = $(CHIBIOS_CONTRIB)/os/hal/boards/LPCXPRESSO54628/board.c

# Required include directories
BOARDINC = $(CHIBIOS_CONTRIB)/os/hal/boards/LPCXPRESSO54628

# Shared variables
ALLCSRC += $(BOARDSRC)
ALLINC  += $(BOARDINC)
