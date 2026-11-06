#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds every configuration of the K100 firmware this change added to or edited, with the image
# checks of build-k100.sh on each, and keeps each image, since every build starts a clean tree:
#
#   <work-dir>/images/normal       what goes on the keyboard
#   <work-dir>/images/hang         K100_DIAGNOSTIC_HANG=yes, see docs/qmk-layout.md
#   <work-dir>/images/diagnostic   K100_USB_DIAGNOSTIC, see bootloader.c
#
#   nix develop .#qmk
#   qmk/test/build-k100-variants.sh <work-dir>
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
work="$1"

build() {
    local name="$1"
    shift

    "$here/build-k100.sh" "$work" "$@"

    mkdir -p "$work/images/$name"
    cp "$work/qmk_firmware/.build/corsair_k100_default.bin" \
       "$work/qmk_firmware/.build/corsair_k100_default.elf" \
       "$work/images/$name/"
}

build normal
build hang K100_DIAGNOSTIC_HANG=yes
build diagnostic EXTRAFLAGS=-DK100_USB_DIAGNOSTIC
