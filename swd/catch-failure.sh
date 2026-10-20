#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Watches firmware that is already running, and stops it in the act of failing.
#
#   sudo swd/catch-failure.sh --seconds 120
#
# fault-report.sh answers "what went wrong" for a part that has already stopped.
# This is for the other case, and the harder one: firmware that starts, works,
# and then leaves the bus at no fixed time. By the time anyone can attach, the
# watchdog has reset the part, and a reset clears both the fault status
# registers and the stack frame that say what happened -- so every reading taken
# afterwards describes a healthy machine that has just been restarted.
#
# Three things have to be arranged before the failure rather than after it:
#
#   the watchdog     Corsair's bootloader hands over a running one whose
#                    timeout is about 1.6 s, and that is how long any evidence
#                    lasts. Raising its timeout constant to the maximum buys
#                    over a minute instead; the firmware's own feed reloads
#                    from the new value, so nothing in the firmware has to know
#                    and nothing else about the watchdog is changed. The next
#                    reset puts the constant back.
#   vector catch     the core halts on a fault instead of entering the
#                    handler, so the registers are read where the fault
#                    happened rather than wherever the handler ended up.
#   the watchdog,    the same counter read the other way round. Firmware that
#   as a stall       is running normally feeds it every pass of its main loop,
#   detector         so the counter sits just below the constant it reloads
#                    from. A main loop that stops reaching the feed shows up as
#                    that counter falling, long before it would have reset the
#                    part -- and with the timeout stretched, it never does.
#                    Polling it from here therefore catches a stalled loop
#                    while it is still stalled, which is the one moment worth
#                    reading registers at.
#
# Either way the part ends up halted and the registers are read and decoded. A
# window that ends with the counter still full is an answer too: the firmware
# neither faulted nor stopped feeding while it was watched.
#
# Options: --seconds <n>, --no-stretch, --stall-ms <n>, --peek <addr>:<count>,
# --leave-halted, --swclk, --swdio, --srst, --speed.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=swd/swd-session.sh
source "$here/swd-session.sh"

# The part is never stopped to set this up. Reads and writes go through the
# debug port without the core's help, so the watchdog can be stretched and the
# vector catch armed while the firmware runs. Halting first would have been the
# obvious way to do it and it is the wrong one: stopping a firmware in order to
# watch it changes the thing being watched, and a stall that only ever appears
# after the debugger interrupted it is the debugger's stall, not the
# firmware's.
HALT="echo {leaving the core running}"
HALT_REQUIRED=no

# The watchdog block, from the part's memory map. TC is the constant the counter
# reloads from, TV the counter, and the two FEED writes are what reloads it.
WWDT_TC=0x4000C004
WWDT_FEED=0x4000C008
WWDT_TV=0x4000C00C
WWDT_TC_MAX=0x00FFFFFF

# The counter runs at a quarter of the watchdog oscillator, which is nominally
# 1 MHz on this part, so this many counts is a millisecond of main loop.
COUNTS_PER_MS=250

SECONDS_TO_WATCH=120
STALL_MS=500
stretch=yes
leave_halted=no
peeks=()

while (( $# )); do
    case ${1:-} in
        --seconds)      SECONDS_TO_WATCH=$2; shift 2; continue ;;
        --stall-ms)     STALL_MS=$2; shift 2; continue ;;
        --no-stretch)   stretch=no; shift; continue ;;
        --leave-halted) leave_halted=yes; shift; continue ;;
        --peek)         peeks+=("$2"); shift 2; continue ;;
    esac
    swd_parse_option "$@"
    (( swd_shift < 0 )) && { echo "unknown option: $1" >&2; exit 1; }
    shift "$swd_shift"
done

prepare=()
if [[ $stretch == yes ]]; then
    # The counter reloads from TC on a feed, not on the write, so the feed is
    # what puts the new timeout into effect. TC is read back afterwards: the
    # part ignores the write when WDPROTECT is set, and a report that says the
    # watchdog was stretched when it was not is worse than no report.
    prepare+=(
        -c "mww $WWDT_TC $WWDT_TC_MAX"
        -c "mww $WWDT_FEED 0xAA"
        -c "mww $WWDT_FEED 0x55"
    )
fi

# Read where they are worth reading: with the part stopped in the act, not
# before it or after the watchdog has had it.
reads_tcl=""
for spec in ${peeks[@]+"${peeks[@]}"}; do
    reads_tcl+="mdw ${spec%%:*} ${spec##*:}"$'\n'
done

resume=(-c "catch {resume}")
[[ $leave_halted == yes ]] && resume=()

# The session halts the part once at the start, so the state in OpenOCD's own
# target table says nothing about how the window ended. These markers do.
CAUGHT="the firmware stopped feeding its watchdog"
RAN="still feeding when the window ended"

# Reading memory does not need the core stopped, so the counter can be sampled
# while the firmware runs. The threshold is how far it is allowed to fall from
# whatever it reloads from, which keeps it right whether or not TC was raised.
watch_tcl="
  set tc [lindex [read_memory $WWDT_TC 32 1] 0]
  set floor [expr {\$tc - ($STALL_MS * $COUNTS_PER_MS)}]
  set started [clock seconds]
  set deadline [expr {\$started + $SECONDS_TO_WATCH}]
  set reported \$started
  set caught 0
  echo \"watchdog reloads from \$tc; halting if the counter falls below \$floor\"
  while {[clock seconds] < \$deadline} {
    set tv [lindex [read_memory $WWDT_TV 32 1] 0]
    set now [clock seconds]
    if {\$tv < \$floor} {
      halt
      echo \"$CAUGHT after [expr {\$now - \$started}] s (counter at \$tv)\"
      set caught 1
      break
    }
    # A line every few seconds, so a counter that never reloads at all is
    # distinguishable from one that reloads until it suddenly stops.
    if {\$now - \$reported >= 5} {
      echo \"  [expr {\$now - \$started}] s, counter \$tv\"
      set reported \$now
    }
  }
  if {\$caught == 0} {
    echo \"$RAN\"
  } else {
    foreach r {pc lr sp xpsr basepri primask control} { echo [reg \$r] }
    echo [mdw 0xE000ED24 7]
    echo {VTOR, then the SVCall, DebugMon, reserved and PendSV vectors:}
    echo [mdw 0xE000ED08 1]
    echo [mdw [expr {[lindex [read_memory 0xE000ED08 32 1] 0] + 0x2C}] 4]
    $reads_tcl
  }
"

echo "watching for $SECONDS_TO_WATCH s; use the keyboard, give it something to"
echo "fail on"
echo

# Everything but reset: a reset is the thing being escaped from here, and
# catching it would stop the part on the very event whose evidence has already
# been lost.
log=$(swd_run \
        ${prepare[@]+"${prepare[@]}"} \
        -c "cortex_m vector_catch hard_err int_err bus_err state_err chk_err nocp_err mm_err" \
        -c "catch {resume}" \
        -c "$watch_tcl" \
        -c "targets" \
        ${resume[@]+"${resume[@]}"} || true)
printf '%s\n' "$log"

[[ $log == *"$CAUGHT"* ]] || {
    echo
    echo "It ran for the whole window without faulting and without missing a"
    echo "feed. Whatever ends this firmware did not happen while it was watched."
    exit 0
}

echo
echo "it stopped. what those registers mean:"
echo
printf '%s\n' "$log" | python3 -I "$here/decode-fault.py"
