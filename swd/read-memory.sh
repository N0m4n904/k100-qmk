#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Reads any region of the part's address space to a file, twice, and refuses to
# produce one unless the two reads agree.
#
# dump-flash.sh is this with the flash's address and size already filled in.
# This one exists for the questions that come up around a dump and cannot be
# answered from it: what the boot ROM at 0x03000000 holds, whether an address
# reads differently with the core running than halted, what a peripheral
# register says.
#
#   sudo swd/read-memory.sh 0x03000000 0x10000 rom-64k.bin
#   sudo swd/read-memory.sh --halt-late 0x00000000 0x10000 boot0-late.bin
#
# Options: --swclk, --swdio, --srst, --speed, --no-halt, --halt-late.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=swd/swd-session.sh
source "$here/swd-session.sh"

ATTEMPTS=3

while (( $# )); do
    swd_parse_option "$@"
    (( swd_shift < 0 )) && break
    shift "$swd_shift"
done

(( $# == 3 )) || {
    echo "usage: $0 [options] <address> <length> <output>" >&2
    exit 1
}

ADDRESS=$1
LENGTH=$2
OUTPUT=$3

read_once() {
    local target=$1 attempt log got
    local want=$(( LENGTH ))

    for attempt in $(seq "$ATTEMPTS"); do
        log=$(swd_run -c "dump_image $target $ADDRESS $LENGTH" || true)
        printf '%s\n' "$log"

        if [[ $HALT_REQUIRED == yes ]] && ! swd_halted "$log"; then
            echo "attempt $attempt: the part did not halt, discarding" >&2
            continue
        fi

        # OpenOCD can exit successfully having written less than was asked for,
        # so its exit status is not the thing to believe. The length is.
        got=$(stat -c %s "$target" 2>/dev/null || echo 0)
        if (( got != want )); then
            echo "attempt $attempt: short read, got $got bytes of $want" >&2
            continue
        fi

        return 0
    done

    echo "gave up after $ATTEMPTS attempts" >&2
    exit 1
}

echo "pass 1 of 2"
read_once "$OUTPUT.pass1"
echo
echo "pass 2 of 2"
read_once "$OUTPUT.pass2"
echo

if ! cmp "$OUTPUT.pass1" "$OUTPUT.pass2"; then
    echo >&2
    echo "The two passes disagree, so neither can be trusted." >&2
    exit 1
fi

mv "$OUTPUT.pass1" "$OUTPUT"
rm -f "$OUTPUT.pass2"

distinct=$(python3 -c \
    'import sys; print(len(set(open(sys.argv[1], "rb").read())))' "$OUTPUT")

echo "Both passes matched."
echo "  $OUTPUT  $(stat -c %s "$OUTPUT") bytes from $ADDRESS," \
     "$distinct distinct byte values"
