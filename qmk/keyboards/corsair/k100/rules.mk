# matrix.c implements matrix_init_custom() and matrix_scan_custom(); QMK keeps
# debounce and the rest. keyboard.json declares matrix_pins.custom_lite to
# match, and QMK will not pick up matrix.c on its own.
SRC += matrix.c

# The keyboard's half of the LED driver: how its three IS31FL3743B chips are
# reached, and which channels light which LED. The chip driver itself comes from
# QMK, which adds drivers/led/issi/is31fl3743b.c when RGB_MATRIX_DRIVER selects
# it -- adding it here as well is a duplicate definition at link time.
SRC += is31fl3743b_transport.c
SRC += is31fl3743b_leds.c

# The chip driver's headers. QMK adds this itself when RGB_MATRIX_DRIVER selects
# is31fl3743b, but not when rgb_matrix is off -- and the two files above are
# compiled either way, so that they cannot rot while the feature is disabled.
VPATH += $(TOP_DIR)/drivers/led/issi
