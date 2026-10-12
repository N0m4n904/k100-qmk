#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Checks every interrupt vector name in lpc_registry.h against its IRQ number.
#
# ChibiOS names a vector VectorXX, where XX is the hexadecimal byte offset into
# the vector table, so IRQn n is Vector(0x40 + 4n). Get it wrong and the handler
# is installed in a neighbouring slot: the interrupt it was written for is never
# serviced, and some unrelated one calls it instead. Nothing in a build notices,
# because the name is a valid symbol either way.
#
# Three of these were wrong when this check was written, so it exists.
#
#   chibios/test/check-vectors.sh
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
registry="$here/../os/hal/ports/LPC/LPC546xx/lpc_registry.h"

python3 - "$registry" <<'PY'
import re, sys

path = sys.argv[1]
# Each line pairs the vector name with the IRQ number it claims to serve:
#   #define LPC_USB_IRQ_VECTOR  VectorB0  /* USB0_IRQn = 28 */
pattern = re.compile(
    r'#define\s+(LPC_\w*_IRQ_VECTOR)\s+Vector([0-9A-Fa-f]+)\s*/\*\s*(\w+)\s*=\s*(\d+)')

checked = failed = 0
for line in open(path):
    m = pattern.search(line)
    if not m:
        continue
    macro, vector, irq_name, irqn = m.group(1), m.group(2), m.group(3), int(m.group(4))
    expected = 0x40 + 4 * irqn
    checked += 1
    if int(vector, 16) != expected:
        failed += 1
        print(f"FAIL: {macro} is Vector{vector.upper()} but {irq_name} = {irqn} "
              f"needs Vector{expected:02X}")

if checked == 0:
    print("FAIL: no vectors matched, so this check proves nothing -- has the "
          "format of lpc_registry.h changed?")
    sys.exit(1)

if failed:
    sys.exit(1)
print(f"ok: all {checked} interrupt vectors match their IRQ numbers")
PY
