# matrix.c implements matrix_init_custom() and matrix_scan_custom(); QMK keeps
# debounce and the rest. keyboard.json declares matrix_pins.custom_lite to
# match, and QMK will not pick up matrix.c on its own.
SRC += matrix.c

# The LED drivers. Built, but not yet wired into rgb_matrix: that needs
# is31fl3743b registering as an rgb_matrix driver in QMK, and the LED count
# reconciled with the generated layout. Compiling them now is what keeps the
# next step from being a pile of unbuilt code.
SRC += is31fl3743b_transport.c
SRC += is31fl3743b_leds.c
SRC += $(TOP_DIR)/drivers/led/issi/is31fl3743b.c
VPATH += $(TOP_DIR)/drivers/led/issi
