#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Runs the decision of the USB recovery on the host. Runs nothing on the keyboard.
#
#   qmk/test/test-usb-recovery.sh
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
keyboard="$(cd "$here/../keyboards/corsair/k100" && pwd)"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT

# A timeout of its own, so the test states the rule and not the value the firmware ships with.
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -DK100_USB_WATCHDOG_MS=3000U -I"$keyboard" \
    "$here/usb_recovery/usb_recovery_test.c" "$keyboard/usb_recovery.c" \
    -o "$out/usb_recovery_test"

"$out/usb_recovery_test"
