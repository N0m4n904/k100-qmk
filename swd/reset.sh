#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Resets the part and lets it run, then disconnects.
#
#   sudo swd/reset.sh
#
# Use it after writing an image, to start the thing that was written. It is a
# reset through the core's own SYSRESETREQ, so the peripherals come up in their
# reset state -- the watchdog among them, which means a firmware that hangs
# gets the same eleven-second loop as any other power-up rather than a quieter
# one that would behave differently from the real thing.
#
# Options: --swclk, --swdio, --srst, --speed.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=swd/swd-session.sh
source "$here/swd-session.sh"

# Nothing is being inspected, so there is nothing to halt for.
HALT="echo {resetting}"
HALT_REQUIRED=no

while (( $# )); do
    swd_parse_option "$@"
    (( swd_shift < 0 )) && { echo "unknown option: $1" >&2; exit 1; }
    shift "$swd_shift"
done

swd_run -c "reset run"

echo
echo "running. To see where it got to:  sudo $here/fault-report.sh"
