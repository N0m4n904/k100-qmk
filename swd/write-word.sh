#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Writes one 32-bit word while the firmware runs, and reads it back.
#
#   sudo swd/write-word.sh 0x40094034 0x00000000
#
# For the questions that can only be answered by changing one thing and
# watching what the part does next: a peripheral register poked while the
# firmware is running, a state forced so that the path out of it can be
# measured. watch-memory.sh observes; this is the other half.
#
# The core is left running, because stopping it stops the thing being changed.
# The read-back is not a courtesy: a register with reserved bits, write-one-to-
# clear bits or hardware that owns the field will not hold what it was given,
# and a write whose effect was never there is worse than one that failed.
#
# This writes wherever it is pointed, including at a peripheral that is mid-
# transaction. It is a diagnostic instrument and the part is recoverable --
# swd/README.md says how -- but nothing here checks that an address is a
# sensible thing to write.
#
# Options: --swclk, --swdio, --srst, --speed.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=swd/swd-session.sh
source "$here/swd-session.sh"

# Changing a running system is the whole point, so the core stays running.
HALT="echo {leaving the core running}"
HALT_REQUIRED=no

while (( $# )); do
    swd_parse_option "$@"
    (( swd_shift < 0 )) && break
    shift "$swd_shift"
done

(( $# == 2 )) || { echo "usage: $0 [options] <address> <value>" >&2; exit 1; }

ADDRESS=$1
VALUE=$2

output=$(swd_run \
    -c "echo \"before [format %08x [lindex [read_memory $ADDRESS 32 1] 0]]\"" \
    -c "mww $ADDRESS $VALUE" \
    -c "echo \"after [format %08x [lindex [read_memory $ADDRESS 32 1] 0]]\"")

before=$(sed -n 's/^before //p' <<<"$output")
after=$(sed -n 's/^after //p' <<<"$output")

if [[ -z $before || -z $after ]]; then
    echo "$output" >&2
    echo "the write could not be confirmed; the session output is above" >&2
    exit 1
fi

printf '%s: %s -> %s\n' "$ADDRESS" "$before" "$after"
