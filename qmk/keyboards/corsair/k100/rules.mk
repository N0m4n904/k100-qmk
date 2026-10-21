# SPDX-License-Identifier: GPL-2.0-or-later
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

# The six status icons. Keyboard level rather than keymap: which LED carries
# which printed icon is a property of the board, not a preference.
SRC += indicators.c

# The way back to Corsair's bootloader. Not optional: QMK does not speak Bragi,
# so without this the first custom image written to the keyboard is the last
# one. keyboard.json declares `bootloader: custom`, which gives QMK's weak
# bootloader_jump() and mcu_reset() for this file to override.
SRC += bootloader.c

# The chip driver's headers. QMK adds this itself when RGB_MATRIX_DRIVER selects
# is31fl3743b, but not when rgb_matrix is off -- and the two files above are
# compiled either way, so that they cannot rot while the feature is disabled.
VPATH += $(TOP_DIR)/drivers/led/issi

# The application lives behind Corsair's bootloader, which occupies the first
# 64 KB of flash, so it links at 0x00010000 rather than at zero. An image linked
# at zero would have a reset vector pointing into the bootloader and could not
# run; see the script's own comment and docs/pin-mapping.md.
MCU_LDSCRIPT = LPC546xx_app_at_64k

# Debounce per key rather than across the whole matrix.
#
# QMK's default, sym_defer_g, waits for *every* position to be quiet before it
# reports anything. On this board several positions that hold no key flicker
# continuously -- there is no switch in them to interrupt the beam -- so the
# matrix is never quiet and nothing is ever reported, however cleanly a real
# key is read. Per-key debouncing lets a key through on its own merits.
#
# Safe here: all twenty positions that read as permanently pressed are ones the
# layout leaves empty, so none of them can report a stuck key.
DEBOUNCE_TYPE = sym_eager_pk
