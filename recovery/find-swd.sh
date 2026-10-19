#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Finds which two header pins are SWDIO and SWCLK by trying every ordered pair
# and seeing which one answers.
#
# A debug port that is listening responds to the line-reset sequence with its
# ID code. Nothing else on a board does that, so a single successful read
# identifies the pair with no ambiguity. A wrong pair reads back all-ones or
# all-zeroes and OpenOCD gives up in under a second.
#
#   sudo recovery/find-swd.sh 22 23 24 25
#
# Pass only GPIOs that identify-header.sh called SAFE. The arguments are the Pi's
# GPIO numbers, in whatever order; the pairing is what this searches.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

(( $# >= 2 )) || { echo "usage: $0 <gpio> <gpio> [gpio...]" >&2; exit 1; }

attempt() {
    local clk=$1 dio=$2
    # -expected-id 0 accepts whatever the port reports. At this stage any answer
    # at all is the finding; checking it against the LPC54605's published ID is
    # lpc54605.cfg's job, once the pins are known.
    openocd \
        -c "adapter driver linuxgpiod" \
        -c "adapter gpio swclk -chip 0 $clk" \
        -c "adapter gpio swdio -chip 0 $dio" \
        -c "adapter speed 100" \
        -c "transport select swd" \
        -c "swd newdap probe cpu -expected-id 0" \
        -c "dap create probe.dap -chain-position probe.cpu" \
        -c "target create probe.cpu cortex_m -dap probe.dap" \
        -c "init" -c "dap info" -c "shutdown" 2>&1
}

for clk in "$@"; do
    for dio in "$@"; do
        [[ $clk == "$dio" ]] && continue

        printf 'SWCLK=GPIO%-3s SWDIO=GPIO%-3s ... ' "$clk" "$dio"
        log=$(attempt "$clk" "$dio" || true)

        if grep -q "IDCODE" <<<"$log"; then
            echo "ANSWERED"
            printf '\n%s\n\n' "$log"
            echo "Those are the pins. Record them, then dump the flash:"
            echo "  sudo $here/dump-flash.sh --swclk $clk --swdio $dio"
            exit 0
        fi
        echo "nothing"
    done
done

cat >&2 <<'EOF'

No pair answered.

That means one of:
  - the pins are not on this header, or not among the ones passed
  - the keyboard is not powered (SWD needs the target running its own supply)
  - the Pi and the keyboard do not share a ground
  - debug access is disabled on the part

Check power and ground first: they account for most of these, and the last one
is the only one that cannot be fixed with a wire.
EOF
exit 1
