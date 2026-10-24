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

# What the wheel button and the profile-switch key do to the lighting. Keyboard
# level because the keys are: neither has a HID meaning to fall back on, so
# without this they do nothing of their own.
SRC += lighting_keys.c

# What the wheel and the roller do when they turn. Keyboard level for the same
# reason: which rotary is which is a property of the board.
SRC += encoders.c

# The ring around the wheel showing what the wheel is set to. Keyboard level
# because the ring is: eight lights in a circle around that one knob, and the
# file says why the volume roller gets no such thing.
SRC += wheel_ring.c

# What the keyboard answers on VIA's own channel: the version, because the
# application displays none it is not handed as text, and the LED probe, because
# which index lights which LED is not known on this board.
SRC += via_values.c

# What the lighting and the main loop are costing, as two numbers rather than an
# impression. Keyboard level because the loop is: the matrix scan this measures
# alongside is this board's own, and it is slow enough that an effect has to be
# asked about rather than assumed.
SRC += lighting_rate.c

# How the keyboard tells the application that it changed something itself. The
# keys and the wheel above do, and VIA reads its menu values only when it
# connects, so without this its idea of the lighting stops being true.
SRC += via_ui_sync.c

# Everything the keyboard remembers about its lighting, and the one thing that
# knows where in its EEPROM region each of it sits. Keyboard level because the
# storage is: the region is declared in this keyboard's config.h.
SRC += lighting_store.c

# When the lighting goes dark by itself: the host has gone to sleep, or nobody
# has touched the keyboard for a while. Keyboard level because the first one is
# unreachable here by QMK's own route, and the file says why.
SRC += lighting_sleep.c

# Our own RGB effect, in rgb_matrix_kb.inc, which QMK includes into
# rgb_matrix.c rather than compiling on its own -- so no SRC entry, just this.
# The file says why an effect that paints black has to exist at all.
RGB_MATRIX_CUSTOM_KB = yes

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
