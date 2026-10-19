# SPDX-License-Identifier: GPL-3.0-or-later
#
# Shared by the scripts here that talk to the keyboard over SWD. Sourced, not
# run: it defines the connection and one function that opens it.
#
# The defaults are the pins as found on a K100's J1 header, so the scripts need
# no arguments on that board and still take them anywhere else.

# J1 pin 3 and pin 2 respectively, as identified by find-swd.sh.
SWCLK=${SWCLK:-23}
SWDIO=${SWDIO:-22}
SRST=${SRST:-}
SPEED=${SPEED:-100}

# Reading memory does not need the core stopped, but it does need the part to
# stop resetting itself, and on a keyboard whose firmware faults the watchdog
# restarts it every eleven seconds. Resetting through the core's own
# SYSRESETREQ and catching it before firmware runs leaves the watchdog in its
# own reset state, which is off. Wrapped in catch so a target that will not
# halt still gets as far as reporting that it did not.
HALT="catch {reset halt}"
HALT_REQUIRED=yes

# Halting out of reset has a cost: the part is caught before its boot ROM has
# finished, and on this family the ROM is still mapped over the bottom of the
# address space at that point. A read of 0x00000000 then returns the ROM's
# first 0x200 bytes instead of the flash's -- which is how the vector table
# came back with every handler pointing into 0x0300xxxx.
#
# --halt-late resets, lets the part run far enough for the ROM to hand over,
# and halts it after. The watchdog is running again by then, so the part is
# only still for as long as it takes to be reset once more; short reads are
# fine, long ones should use the default.
HALT_LATE="catch {reset run}; sleep 300; catch {halt}"

# Stopping the part where it already is, without resetting it first. This is
# the one to use when the question is what the firmware did, because a reset
# clears the fault status registers and the stack that say what it was. Nothing
# that wants a clean machine should use it.
HALT_ONLY="catch {halt}"

swd_session_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

# swd_run <openocd -c arguments...> -- opens the session, runs them, shuts down.
# Everything, including OpenOCD's own diagnostics, comes back on stdout.
swd_run() {
    local -a gpio=(
        -c "adapter gpio swclk -chip 0 $SWCLK"
        -c "adapter gpio swdio -chip 0 $SWDIO"
    )
    [[ -n $SRST ]] && gpio+=(
        -c "adapter gpio srst -chip 0 $SRST"
        -c "reset_config srst_only srst_open_drain srst_nogate"
    )

    openocd \
        -c "adapter driver linuxgpiod" \
        "${gpio[@]}" \
        -c "adapter speed $SPEED" \
        -f "$swd_session_dir/lpc54605.cfg" \
        -c "init" \
        -c "$HALT" \
        -c "targets" \
        "$@" \
        -c "shutdown" 2>&1
}

# Whether a session's output shows the core actually stopped. OpenOCD prints
# the state in its target table, and "running" there is the difference between
# a read of a part sitting still and a read of one being reset underneath it.
swd_halted() { [[ $1 =~ cortex_m[[:space:]].*halted ]]; }

# The options every script here understands. Call it with "$@"; it sets
# swd_shift to how many arguments it consumed, or -1 if it recognised none.
swd_parse_option() {
    swd_shift=2
    case ${1:-} in
        --swclk) SWCLK=$2 ;;
        --swdio) SWDIO=$2 ;;
        --srst)  SRST=$2 ;;
        --speed) SPEED=$2 ;;
        --no-halt)
            HALT="echo {leaving the core running}"
            HALT_REQUIRED=no
            swd_shift=1
            ;;
        --halt-late)
            HALT=$HALT_LATE
            swd_shift=1
            ;;
        --halt-only)
            HALT=$HALT_ONLY
            swd_shift=1
            ;;
        *) swd_shift=-1 ;;
    esac
}
