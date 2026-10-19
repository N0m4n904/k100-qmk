#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Stops the part where it is and says what went wrong.
#
#   sudo swd/fault-report.sh
#
# This is the instrument that was missing while this firmware was being
# debugged blind. A Cortex-M records why it faulted and, for most kinds, the
# address involved; reading those registers answers in one command what was
# previously inferred from how long a keyboard stayed off the USB bus.
#
# It halts without resetting, on purpose. A reset clears the fault status
# registers and the stack frame that say what happened, so resetting first
# would destroy the evidence and report a clean machine every time.
#
# Options: --swclk, --swdio, --srst, --speed, --halt-late, --no-halt.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=swd/swd-session.sh
source "$here/swd-session.sh"

HALT=$HALT_ONLY

while (( $# )); do
    swd_parse_option "$@"
    (( swd_shift < 0 )) && { echo "unknown option: $1" >&2; exit 1; }
    shift "$swd_shift"
done

# 0xE000ED24 is SHCSR, and the seven words from there are SHCSR, CFSR, HFSR,
# DFSR, MMFAR, BFAR and AFSR -- everything the architecture records about a
# fault, in one read.
log=$(swd_run -c "reg" -c "mdw 0xE000ED24 7" || true)
printf '%s\n' "$log"

swd_halted "$log" || {
    echo >&2
    echo "The part did not halt, so there is nothing to report." >&2
    exit 1
}

echo
echo "what those registers mean:"
echo
printf '%s\n' "$log" | python3 -I "$here/decode-fault.py"
