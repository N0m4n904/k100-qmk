#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Asks the part's own flash controller to identify itself. Read-only: it erases
# nothing and writes nothing.
#
#   sudo recovery/flash-info.sh
#
# Worth running before any erase, because it exercises the whole path an erase
# would use -- the IAP entry point, the working area in SRAM, the sector map --
# without changing anything. If the part ID comes back, OpenOCD is talking to
# the flash controller and the lpc54100 variant fits this part. If it does not,
# that is worth finding out now rather than halfway through an erase.
#
# Options: --swclk, --swdio, --srst, --speed.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=recovery/swd-session.sh
source "$here/swd-session.sh"

while (( $# )); do
    swd_parse_option "$@"
    (( swd_shift < 0 )) && { echo "unknown option: $1" >&2; exit 1; }
    shift "$swd_shift"
done

log=$(swd_run -c "flash probe 0" -c "flash info 0" -c "lpc2000 part_id 0" || true)
printf '%s\n' "$log"

swd_halted "$log" || {
    echo >&2
    echo "The part did not halt, so none of the above can be relied on." >&2
    exit 1
}

if [[ $log =~ lpc2000\ part\ id:\ (0x[0-9a-fA-F]+) ]]; then
    echo
    echo "The flash controller answered: part id ${BASH_REMATCH[1]}."
    echo "IAP works, so erasing and writing are available."
else
    echo >&2
    echo "The flash controller did not report a part id, so erasing and" >&2
    echo "writing are not available through this path." >&2
    exit 1
fi
