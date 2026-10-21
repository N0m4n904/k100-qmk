#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Samples a region repeatedly while the firmware runs, and reports the distinct
# values it took rather than every sample.
#
#   sudo swd/watch-memory.sh 0x20001e84 17
#   sudo swd/watch-memory.sh --seconds 20 --interval 50 0x20001ea8 8
#
# read-memory.sh answers "what is there"; it reads twice and refuses to produce
# a file unless the two agree, which is what you want for a dump and exactly
# wrong for a variable. This answers "does it ever change, and to what" -- the
# question behind every "I pressed the key and nothing happened".
#
# The core is left running, because stopping it stops the thing being watched:
# a halted keyboard scans no matrix and processes no keycode. Memory reads work
# on a running core, so nothing is given up by leaving it alone.
#
# A value that never changes is an answer too, and the most common useful one:
# it separates "the firmware never saw it" from "the firmware saw it and did
# something else with it".
#
# Options: --swclk, --swdio, --srst, --speed, --seconds, --interval.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=swd/swd-session.sh
source "$here/swd-session.sh"

SECONDS_TO_WATCH=10
INTERVAL_MS=50

# Watching is the whole point, so the core stays running unless a caller is
# explicit about it; swd_parse_option still honours --halt-only after this.
HALT="echo {leaving the core running}"
HALT_REQUIRED=no

while (( $# )); do
    case ${1:-} in
        --seconds)  SECONDS_TO_WATCH=$2; shift 2; continue ;;
        --interval) INTERVAL_MS=$2; shift 2; continue ;;
    esac
    swd_parse_option "$@"
    (( swd_shift < 0 )) && break
    shift "$swd_shift"
done

(( $# >= 1 && $# <= 2 )) || {
    echo "usage: $0 [options] <address> [words]" >&2
    exit 1
}

ADDRESS=$1
WORDS=${2:-1}

samples=$(( SECONDS_TO_WATCH * 1000 / INTERVAL_MS ))
(( samples > 0 )) || { echo "nothing to sample: the window is shorter than one interval" >&2; exit 1; }

echo "watching $WORDS word(s) at $ADDRESS for ${SECONDS_TO_WATCH}s, $samples samples"

# read_memory returns the words as a Tcl list, so one echo per sample carries
# the whole region and the marker keeps OpenOCD's own output out of the way.
# They are formatted here rather than downstream: the list is decimal, and a
# word with the top bit set is past where awk's %x can be trusted.
output=$(swd_run -c "
    for {set i 0} {\$i < $samples} {incr i} {
        set words {}
        foreach word [read_memory $ADDRESS 32 $WORDS] {
            append words [format { %08x} \$word]
        }
        echo \"sample\$words\"
        sleep $INTERVAL_MS
    }
")

if ! grep -q '^sample ' <<<"$output"; then
    echo "$output" >&2
    echo "no samples were taken; the session output is above" >&2
    exit 1
fi

grep '^sample ' <<<"$output" | sed 's/^sample *//' | awk '
    { count[$0]++; if (!($0 in seen)) { seen[$0] = ++order; value[order] = $0 } }
    END {
        printf "%d distinct value(s) over %d samples\n", order, NR
        for (i = 1; i <= order; i++) printf "%6d x  %s\n", count[value[i]], value[i]
    }
'
