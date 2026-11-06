#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Runs the fault log on the host against an EEPROM in an array, and decodes what it builds with
# the tool that reads it on the keyboard. Runs nothing on the keyboard.
#
#   nix develop .#qmk
#   qmk/test/test-fault-log.sh
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
keyboard="$repo/qmk/keyboards/corsair/k100"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$here/fault_log/stubs" -I"$keyboard" \
    -I"$repo/chibios/os/hal/ports/LPC/LPC546xx" \
    -I"$repo/qmk/platforms/chibios/drivers/eeprom" \
    "$here/fault_log/fault_log_test.c" \
    "$keyboard/fault_log_ring.c" "$keyboard/via_fault_log.c" "$keyboard/via_text.c" \
    -o "$work/fault_log_test"

"$work/fault_log_test"

"$work/fault_log_test" --replies > "$work/replies"
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$repo/tools" \
    python3 "$here/fault_log/check_replies.py" "$repo/tools/k100-fault-log.py" < "$work/replies"
