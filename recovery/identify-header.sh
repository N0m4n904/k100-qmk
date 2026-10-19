#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Works out which pins of an unknown header are safe to drive, using nothing but
# a Raspberry Pi.
#
# The trick is that a GPIO configured as an input can have the Pi's own weak
# pull-up or pull-down applied to it. Read the pin under each in turn:
#
#   follows the pull       nothing else is driving it -- safe to drive
#   low under a pull-up    something is holding it low: ground, or an output
#   high under a pull-down something is holding it high: a supply, or an output
#
# That distinction is the whole point. Driving a clock into a pin the keyboard's
# own chip is driving puts two outputs in opposition, which is the one way this
# procedure could damage a board that is currently only soft-bricked. Pins in
# the first class cannot be in opposition with anything, so those are the only
# ones worth trying as SWDIO and SWCLK.
#
# Reading is passive: a Pi input presented to a 3.3 V pin neither sources nor
# sinks anything worth the name, and the two parts run from the same voltage.
#
#   sudo recovery/identify-header.sh 22
#   sudo recovery/identify-header.sh 22 23 24 25 26 27
#
# With one GPIO, move the probe wire to the next header pin and run it again.

set -euo pipefail

SETTLE=0.05

die() { printf '%s\n' "$*" >&2; exit 1; }

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

(( $# )) || die "usage: $0 <gpio>..."

printf '%-6s  %-8s  %-10s  %s\n' GPIO 'pull-up' 'pull-down' verdict
printf -- '------------------------------------------------------------\n'

for gpio in "$@"; do
    set_input "$gpio" pu; sleep "$SETTLE"; up=$(read_level "$gpio")
    set_input "$gpio" pd; sleep "$SETTLE"; down=$(read_level "$gpio")

    case "$up$down" in
        10) verdict="floating -- SAFE to try as SWDIO/SWCLK" ;;
        00) verdict="held low -- ground, or an output. DO NOT DRIVE" ;;
        11) verdict="held high -- a supply, or an output. DO NOT DRIVE" ;;
        01) verdict="inconsistent -- re-seat the wire and try again" ;;
    esac

    printf '%-6s  %-8s  %-10s  %s\n' "$gpio" "$up" "$down" "$verdict"
done

# Leave every pin an input with no pull, so nothing is left attached to the
# keyboard through a resistor of the Pi's choosing.
for gpio in "$@"; do set_input "$gpio" pn; done
