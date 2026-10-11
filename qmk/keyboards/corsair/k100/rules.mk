# matrix.c implements matrix_init_custom() and matrix_scan_custom(); QMK keeps
# debounce and the rest. keyboard.json declares matrix_pins.custom_lite to
# match, and QMK will not pick up matrix.c on its own.
SRC += matrix.c
