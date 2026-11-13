#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs the interrupt handler of the shared LPC USB driver on the host, against the driver as
# patched by a build. Runs nothing on the keyboard.
#
#   nix develop .#qmk --command qmk/test/build-k100.sh <work-dir>
#   nix develop .#usb-test --command chibios/test/test-usb-interrupt.sh <work-dir>
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
lib="$(cd "${1:?the work directory of qmk/test/build-k100.sh}" && pwd)/qmk_firmware/lib"
ports="$lib/chibios-contrib/os/hal/ports/LPC"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -DCPU_LPC54605J512ET100 -DLPC54605 \
    -I"$here/usb_interrupt/stubs" \
    -I"$ports/LLD/USB" -I"$ports/LPC546xx" \
    -I"$lib/chibios/os/hal/include" \
    -I"$lib/chibios-contrib/os/common/ext/CMSIS/LPC/LPC54605" \
    -I"$lib/chibios/os/common/ext/ARM/CMSIS/Core/Include" \
    "$here/usb_interrupt/usb_interrupt_test.c" "$lib/chibios/os/hal/src/hal_usb.c" \
    -o "$out/usb_interrupt_test"

"$out/usb_interrupt_test"
