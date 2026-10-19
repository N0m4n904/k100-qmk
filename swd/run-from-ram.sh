#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Loads an image into SRAM and starts it, without touching flash at all.
#
#   sudo swd/run-from-ram.sh corsair_k100_default.bin
#
# Build one with:
#
#   LDSCRIPT=LPC546xx_sram qmk/test/build-k100.sh <work-dir>
#
# The part is left exactly as it was: nothing is erased, nothing is written to
# flash, and the next reset runs whatever is in flash again as though this had
# not happened. That makes it the right way to chase a firmware that does not
# boot -- each attempt costs a few seconds and leaves no trace, instead of a
# flash sector and something permanent.
#
# What it does, in order:
#
#   reset halt          a clean machine, running on the 12 MHz internal
#                       oscillator with every peripheral in its reset state
#   AHBCLKCTRLSET0      clocks SRAM1, SRAM2 and SRAM3, which are off out of
#                       reset; without this everything above 0x20008000 is
#                       written into nothing
#   load_image          the image itself
#   verify_image        read back, so a load that did not land is caught here
#   VTOR                pointed at the loaded vector table before anything can
#                       interrupt through the one still in flash
#   MSP, PC             taken from the image's own first two words
#   vector_catch        so that a fault stops the core before its own
#                       handler runs, rather than after the watchdog has
#                       reset the part and wiped the evidence
#   resume              unless --no-run, which leaves it halted to step from
#   wait, then report   what faulted and where, or that it is still running
#
# Options: --no-run, --watch <seconds>, --swclk, --swdio, --srst, --speed.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=swd/swd-session.sh
source "$here/swd-session.sh"

SRAM_BASE=$(( 0x20000000 ))
SRAM_END=$(( 0x20028000 ))

# SYSCON at 0x40000000, AHBCLKCTRLSET array at offset 0x220. Bits 3, 4 and 5
# are SRAM1, SRAM2 and SRAM3; the register is write-one-to-set, so this
# disturbs nothing else. Both addresses and the value are from NXP's own
# LPC54605.h, and __cpu_init makes the identical write for the same reason.
AHBCLKCTRLSET0=0x40000220
SRAM_BANKS=0x38

VTOR=0xE000ED08
start=yes
WATCH=10

while (( $# )); do
    case ${1:-} in
        --no-run) start=no; shift; continue ;;
        --watch)  WATCH=$2; shift 2; continue ;;
    esac
    swd_parse_option "$@"
    (( swd_shift < 0 )) && break
    shift "$swd_shift"
done

(( $# == 1 )) || { echo "usage: $0 [options] <image.bin>" >&2; exit 1; }
IMAGE=$1
[[ -s $IMAGE ]] || { echo "$IMAGE is missing or empty" >&2; exit 1; }

# The image's first two words are its initial stack pointer and its reset
# vector. Reading them is also the check that this is the right kind of image:
# a flash-linked build has a reset vector in 0x0001xxxx, and loading that into
# SRAM and jumping to it would run whatever is at that address in flash
# instead -- which is Corsair's firmware, from the middle.
#
# Assigned to a variable first, deliberately. Inside `read <<<"$(...)"` the
# substitution's exit status is discarded, so a refusal would print and the
# script would carry on with nothing in sp and pc; a plain assignment is what
# set -e actually acts on.
vectors=$(python3 -I - "$IMAGE" "$SRAM_BASE" "$SRAM_END" <<'PY'
import struct
import sys

image, base, end = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
with open(image, "rb") as f:
    sp, pc = struct.unpack("<II", f.read(8))

for name, value in (("stack pointer", sp), ("reset vector", pc & ~1)):
    if not base <= value <= end:
        sys.exit(f"{image} is not linked for SRAM: its {name} is 0x{value:08x}, "
                 f"outside 0x{base:08x}-0x{end:08x}. Build it with "
                 f"LDSCRIPT=LPC546xx_sram.")

print(f"0x{sp:08x} 0x{pc:08x}")
PY
)
read -r sp pc <<<"$vectors"

printf 'loading %s (%s bytes) at 0x%08x\n' "$IMAGE" "$(stat -c %s "$IMAGE")" "$SRAM_BASE"
printf '  stack pointer %s, entry %s\n' "$sp" "$pc"

# Running and reporting have to happen in one session, and the core has to stop
# itself at the fault rather than be asked about it afterwards.
#
# This firmware's fault handler spins without feeding the watchdog, so a few
# seconds later the part resets -- which clears every fault status register and
# starts whatever is in flash. A separate fault-report.sh run would arrive to
# find a clean machine running Corsair's firmware and report that nothing had
# gone wrong.
#
# vector_catch halts the core the instant it takes a fault, before its own
# handler runs at all, so the registers and the stack frame are intact and the
# watchdog never gets the chance.
run=(
    -c "cortex_m vector_catch hard_err bus_err mm_err state_err chk_err nocp_err"
    -c "resume"
    -c "catch {wait_halt $(( WATCH * 1000 ))}"
    -c "targets"
    -c "reg"
    -c "mdw 0xE000ED24 7"
)
[[ $start == no ]] && run=(-c "echo {left halted at the reset vector}")

log=$(swd_run \
    -c "mww $AHBCLKCTRLSET0 $SRAM_BANKS" \
    -c "load_image $IMAGE $SRAM_BASE bin" \
    -c "verify_image $IMAGE $SRAM_BASE bin" \
    -c "mww $VTOR $SRAM_BASE" \
    -c "reg msp $sp" \
    -c "reg pc $pc" \
    "${run[@]}" || true)
printf '%s\n' "$log"

swd_halted "$log" || {
    echo "the part did not halt, so nothing was loaded" >&2
    exit 1
}

if [[ ! $log =~ verified\ [0-9]+\ bytes ]]; then
    echo >&2
    echo "SRAM does not read back as the file, so the load did not take." >&2
    echo "If the image is larger than 32 KB, suspect the bank clocks." >&2
    exit 1
fi

echo
if [[ $start == no ]]; then
    echo "Loaded and halted at the reset vector. Nothing has executed yet."
elif [[ $log =~ timed\ out\ while\ waiting\ for\ target\ halted ]]; then
    echo "Still running after ${WATCH}s without taking a fault."
    echo "Whatever stops this firmware, it is not a fault -- look for a wait"
    echo "that never finishes. It is still running now, so the host's USB tree"
    echo "is worth a look."
else
    echo "It faulted. The core was caught before its own handler ran:"
    echo
    printf '%s\n' "$log" | python3 -I "$here/decode-fault.py"
fi
echo
echo "Flash was not touched. A power cycle brings back whatever is in it."
