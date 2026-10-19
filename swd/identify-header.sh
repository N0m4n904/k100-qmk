#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Works out which pins of an unknown header are safe to drive, using nothing but
# a Raspberry Pi.
#
# A GPIO held as an input can have the Pi's own weak pull-up or pull-down
# applied to it. Read the pin under each in turn:
#
#   follows the pull        nothing else is attached to it
#   low under a pull-up     ground, or something holding it low
#   high under a pull-down  a supply, a pull-up resistor, or an output
#
# Driving a pin the keyboard's own chip is driving puts two outputs in
# opposition, and that is the one way this procedure could damage a board which
# is currently only soft-bricked. So the pins that get driven later have to be
# picked from what this measures.
#
# Reading is passive: a Pi input presented to a 3.3 V pin neither sources nor
# sinks anything worth the name, and the two parts run from the same voltage.
#
#   sudo swd/identify-header.sh 24
#   sudo swd/identify-header.sh 17 27 22 23 24
#
# With one GPIO, move the probe wire to the next header pin and run it again.
#
# ---------------------------------------------------------------------------
# What a single reading cannot tell you
#
# A pull-up resistor on the board and a chip output driving high read the same
# against the Pi's ~50 kohm pull-down. One is perfectly safe to drive and the
# other is the case worth avoiding, so "held high" on its own decides nothing.
#
# --watch separates them, using the fault the keyboard already has. Its
# watchdog restarts the part every few seconds, and a chip in reset has every
# pin high-impedance -- so an output cannot hold a level through a reset, while
# a resistor soldered to the board does not care that a reset happened.
#
#   never changes over several resets   static: a rail or a pull-up, SAFE
#   changes at any point                the chip is driving it, LEAVE ALONE
#
#   sudo swd/identify-header.sh --watch 45 24
#
# Pick a duration that covers several restarts; this board's loop is about
# eleven seconds, so 45 covers four.

set -euo pipefail

SETTLE=0.05
WATCH=0

die() { printf '%s\n' "$*" >&2; exit 1; }

while (( $# )); do
    case $1 in
        --watch) WATCH=${2:?--watch needs a duration in seconds}; shift 2 ;;
        -*) die "unknown option: $1" ;;
        *) break ;;
    esac
done

(( $# )) || die "usage: $0 [--watch <seconds>] <gpio>..."

if command -v pinctrl >/dev/null 2>&1; then
    set_input() { pinctrl set "$1" ip "$2"; }
    # "22: ip    pu | hi // GPIO22 = input"
    read_level() { [[ "$(pinctrl get "$1")" == *"| hi"* ]] && echo 1 || echo 0; }
elif command -v raspi-gpio >/dev/null 2>&1; then
    set_input() { raspi-gpio set "$1" ip "$2"; }
    # "GPIO 22: level=1 fsel=0 func=INPUT pull=UP"
    read_level() { [[ "$(raspi-gpio get "$1")" == *"level=1"* ]] && echo 1 || echo 0; }
else
    die "neither pinctrl nor raspi-gpio found; install raspi-gpio"
fi

# Leave every pin an input with no pull however this exits, so nothing stays
# attached to the keyboard through a resistor of the Pi's choosing.
cleanup() { for gpio in "$@"; do set_input "$gpio" pn 2>/dev/null || true; done; }
trap 'cleanup "$@"' EXIT

sample() {
    local gpio=$1 up down
    set_input "$gpio" pu; sleep "$SETTLE"; up=$(read_level "$gpio")
    set_input "$gpio" pd; sleep "$SETTLE"; down=$(read_level "$gpio")
    echo "$up$down"
}

describe() {
    case $1 in
        10) echo "floating" ;;
        00) echo "held low" ;;
        11) echo "held high" ;;
        01) echo "inconsistent" ;;
    esac
}

if (( WATCH == 0 )); then
    printf '%-6s  %-8s  %-10s  %s\n' GPIO 'pull-up' 'pull-down' verdict
    printf -- '------------------------------------------------------------\n'

    for gpio in "$@"; do
        reading=$(sample "$gpio")
        case $reading in
            10) verdict="floating -- SAFE to try as SWDIO/SWCLK" ;;
            00) verdict="held low -- ground, or an output" ;;
            11) verdict="held high -- a supply, a pull-up, or an output" ;;
            01) verdict="inconsistent -- re-seat the wire and try again" ;;
        esac
        printf '%-6s  %-8s  %-10s  %s\n' \
            "$gpio" "${reading:0:1}" "${reading:1:1}" "$verdict"
    done
    exit 0
fi

for gpio in "$@"; do
    echo "GPIO$gpio: watching for ${WATCH}s"

    declare -A seen=()
    deadline=$(( SECONDS + WATCH ))
    samples=0

    while (( SECONDS < deadline )); do
        reading=$(sample "$gpio")
        seen[$reading]=$(( ${seen[$reading]:-0} + 1 ))
        (( ++samples ))
    done

    for reading in "${!seen[@]}"; do
        printf '  %-14s %5d of %d samples\n' \
            "$(describe "$reading")" "${seen[$reading]}" "$samples"
    done

    if (( ${#seen[@]} > 1 )); then
        echo "  -> changed during the run: the chip is driving this pin. LEAVE ALONE."
    else
        # Static means only that the chip is not the thing holding the level.
        # What else could be holding it still matters, and differs per level.
        case "${!seen[*]}" in
            10) echo "  -> nothing attached. SAFE to drive." ;;
            00) echo "  -> ground, or tied to it. DO NOT DRIVE." ;;
            11) echo "  -> static: a pull-up resistor, or a supply rail." \
                     "SAFE to drive only once a supply rail is ruled out --" \
                     "see README.md." ;;
            01) echo "  -> inconsistent throughout: re-seat the wire." ;;
        esac
    fi

    unset seen
done
