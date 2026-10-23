#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fills a run of bytes with one value while the firmware runs, and reads it back.
#
#   sudo swd/write-bytes.sh 0x20001838 0xff 198
#
# write-word.sh pokes a register; this one paints a buffer. The question behind
# it is usually "which part of the hardware is this byte attached to": the LED
# page is 198 channels that nothing in the source names individually, and the
# only way to find out what a channel lights is to light it and look.
#
# The core is left running, because a buffer the firmware owns is only sent to
# the hardware by the firmware. Which also means a write here lasts exactly as
# long as nothing overwrites it: bytes an effect paints come back on the next
# frame, and bytes nothing claims stay.
#
# Options: --swclk, --swdio, --srst, --speed.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=swd/swd-session.sh
source "$here/swd-session.sh"

HALT="echo {leaving the core running}"
HALT_REQUIRED=no

while (( $# )); do
    swd_parse_option "$@"
    (( swd_shift < 0 )) && break
    shift "$swd_shift"
done

(( $# == 3 )) || { echo "usage: $0 [options] <address> <byte> <count>" >&2; exit 1; }

ADDRESS=$1
VALUE=$2
COUNT=$3

(( COUNT > 0 )) || { echo "nothing to write: count is $COUNT" >&2; exit 1; }

# Read back the first and last byte rather than the whole run: the run is the
# point, and a region that took the first and the last byte took the ones
# between them. A region that took neither says the address is wrong.
last=$(( ADDRESS + COUNT - 1 ))

output=$(swd_run \
    -c "mwb $ADDRESS $VALUE $COUNT" \
    -c "echo \"first [format %02x [lindex [read_memory $ADDRESS 8 1] 0]]\"" \
    -c "echo \"last [format %02x [lindex [read_memory $last 8 1] 0]]\"")

first=$(sed -n 's/^first //p' <<<"$output")
final=$(sed -n 's/^last //p' <<<"$output")

if [[ -z $first || -z $final ]]; then
    printf '%s\n' "$output" >&2
    echo "the write could not be confirmed; the session output is above" >&2
    exit 1
fi

printf '%s: %s bytes of %s, reads back %s ... %s\n' \
    "$ADDRESS" "$COUNT" "$VALUE" "$first" "$final"
