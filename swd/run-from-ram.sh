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
# Options: --no-run, --watch <seconds>, --break <address>,
# --peek <address>:<count>, --poke <address>:<value>,
# --then-watch <seconds>, --swclk, --swdio, --srst, --speed.

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

# OpenOCD's work area sits at 0x20000000 by default, which is exactly where
# this image goes. It is not reserved memory -- OpenOCD executes helper code
# there -- so verify_image's checksum routine lands on top of the first fifty
# or so bytes of the thing it was asked to check, and the image is destroyed by
# its own verification. Switching the work area off makes verify_image read the
# region back over SWD and compare on the host, which is slower by a fraction
# of a second and cannot disturb anything.
SWD_PRE_CFG=(-c "set WORK_AREA_SIZE 0")

breakpoints=()
peeks=()
pokes=()
THEN_WATCH=4
load=yes

while (( $# )); do
    case ${1:-} in
        --no-run) start=no; shift; continue ;;
        # Run whatever is already in flash instead of loading anything. The
        # breakpoint, peek and poke machinery is the same, and this is how the
        # firmware that works gets watched doing what this one cannot -- a
        # hardware breakpoint reaches flash, which is the half of the address
        # space this core's breakpoint unit can match.
        --no-load) load=no; shift; continue ;;
        --watch)  WATCH=$2; shift 2; continue ;;
        # A hardware breakpoint, so that nothing is written to the code under
        # test. The part has six. Stopping at a known function is how a
        # suspicion becomes an answer: put one on __NVIC_SystemReset and a
        # software reset announces itself, while a part that resets anyway did
        # it in hardware.
        --break) breakpoints+=("$2"); shift 2; continue ;;
        # Words to read once the part is stopped, as address:count. For looking
        # at a peripheral the firmware was using -- a watchdog's counter, a
        # controller's status -- without writing a script per register.
        --peek)  peeks+=("$2"); shift 2; continue ;;
        # A word to write once the part is stopped, as address:value, applied in
        # the order given. With --then-watch this tries a change to a running
        # system before any of it is written into the firmware: if a setting is
        # suspected of being the one that matters, poking it in and looking
        # again answers that in one run instead of a build, a load and a guess.
        --poke)  pokes+=("$2"); shift 2; continue ;;
        # Seconds to let it run again after the pokes, before stopping it and
        # repeating every --peek.
        --then-watch) THEN_WATCH=$2; shift 2; continue ;;
    esac
    swd_parse_option "$@"
    (( swd_shift < 0 )) && break
    shift "$swd_shift"
done

if [[ $load == no ]]; then
    (( $# == 0 )) || { echo "--no-load takes no image" >&2; exit 1; }
else
    (( $# == 1 )) || { echo "usage: $0 [options] <image.bin>" >&2; exit 1; }
    IMAGE=$1
    [[ -s $IMAGE ]] || { echo "$IMAGE is missing or empty" >&2; exit 1; }
fi

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
if [[ $load == yes ]]; then
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
else
    echo "running what is already in flash; nothing is loaded"
fi

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
)
for address in ${breakpoints[@]+"${breakpoints[@]}"}; do
    # This core's Flash Patch and Breakpoint unit is revision 1, which can only
    # match addresses below 0x20000000 -- it was built to patch flash, and SRAM
    # is out of its reach. Every address in an image running from SRAM is out
    # of its reach, so those have to be software breakpoints, which work by
    # writing a BKPT instruction into the code.
    #
    # That is ordinarily a reason to prefer hardware ones, but not here: this
    # code is in RAM and is reloaded from the file on every run, so there is
    # nothing durable to damage.
    if (( address >= SRAM_BASE )); then
        run+=(-c "bp $address 2")
    else
        run+=(-c "bp $address 2 hw")
    fi
done
run+=(
    -c "resume"
    -c "catch {wait_halt $(( WATCH * 1000 ))}"
    # Whether it faulted or is simply still going, stopping it here and reading
    # the program counter says where it actually is. "Still running" on its own
    # does not distinguish a working main loop from a spin that never ends, and
    # those need opposite things done about them.
    -c "catch {halt}"
    -c "targets"
    -c "reg pc"
    -c "reg lr"
    -c "reg sp"
    -c "reg xpsr"
    -c "mdw 0xE000ED24 7"
)
for spec in ${peeks[@]+"${peeks[@]}"}; do
    run+=(-c "mdw ${spec%%:*} ${spec##*:}")
done

if (( ${#pokes[@]} )); then
    run+=(-c "echo {=== applying pokes ===}")
    for spec in "${pokes[@]}"; do
        run+=(-c "mww ${spec%%:*} ${spec##*:}")
    done
    # Settling time before anything is believed: an oscillator that has just
    # been powered needs to start, and a PLL needs to lock.
    run+=(
        -c "sleep 300"
        -c "resume"
        -c "sleep $(( THEN_WATCH * 1000 ))"
        -c "catch {halt}"
        -c "echo {=== after the pokes ===}"
        -c "targets"
        # Which firmware this second reading describes has to be established,
        # not assumed. A run long enough to trip this firmware's own USB
        # timeout puts the part back in the bootloader, and every register read
        # afterwards then describes Corsair's code while looking exactly like a
        # result. The program counter says whose it is: in SRAM it is this
        # image, in flash it is not.
        -c "reg pc"
    )
    for spec in ${peeks[@]+"${peeks[@]}"}; do
        run+=(-c "mdw ${spec%%:*} ${spec##*:}")
    done
fi
[[ $start == no ]] && run=(-c "echo {left halted at the reset vector}")

prologue=()
if [[ $load == yes ]]; then
    prologue=(
        -c "mww $AHBCLKCTRLSET0 $SRAM_BANKS"
        -c "load_image $IMAGE $SRAM_BASE bin"
        -c "verify_image $IMAGE $SRAM_BASE bin"
        -c "mww $VTOR $SRAM_BASE"
        -c "reg msp $sp"
        -c "reg pc $pc"
    )
fi

started=$SECONDS
log=$(swd_run ${prologue[@]+"${prologue[@]}"} "${run[@]}" || true)
printf '%s\n' "$log"
elapsed=$(( SECONDS - started ))

swd_halted "$log" || {
    echo "the part did not halt, so nothing was loaded" >&2
    exit 1
}

if [[ $load == yes && ! $log =~ verified\ [0-9]+\ bytes ]]; then
    echo >&2
    echo "SRAM does not read back as the file, so the load did not take." >&2
    echo "Differences only in the first few dozen bytes mean something ran" >&2
    echo "there: check that the work area is off. Differences above" >&2
    echo "0x20008000 mean the bank clocks did not take." >&2
    exit 1
fi

echo
if [[ $start == no ]]; then
    echo "Loaded and halted at the reset vector. Nothing has executed yet."
elif [[ ! $log =~ 0xe000ed24: ]]; then
    # The fault registers are read last, so their absence means the session
    # stopped early -- OpenOCD abandons the rest of a command list after an
    # error. Saying so beats falling through to the branch below, which would
    # announce a crash that never happened because the firmware never ran.
    echo "The session did not get as far as running the image." >&2
    echo "An OpenOCD error above will say why." >&2
    exit 1
elif [[ $log =~ halted\ due\ to\ breakpoint ]]; then
    # A breakpoint reaching is a positive result, and it must not be read as a
    # fault: wait_halt returns successfully when one is hit, which would
    # otherwise land in the branch below and report a crash that did not happen.
    echo "A breakpoint was reached, after about ${elapsed}s."
    echo
    printf '%s\n' "$log" | python3 -I "$here/decode-fault.py"
elif [[ $load == yes ]] && (( ${#pokes[@]} )) \
     && [[ ${log##*=== after the pokes ===} =~ pc\ \(/32\):\ (0x[0-9a-fA-F]+) ]] \
     && (( BASH_REMATCH[1] < SRAM_BASE )); then
    printf 'After the pokes the program counter was %s, which is not in SRAM.\n' \
        "${BASH_REMATCH[1]}" >&2
    echo >&2
    echo "The part left this image at some point, so every reading above the" >&2
    echo "marker belongs to it and everything below belongs to whatever is in" >&2
    echo "flash. Shorten --watch and --then-watch: their sum has to stay under" >&2
    echo "this firmware's own USB timeout, or it hands the keyboard back to the" >&2
    echo "bootloader partway through the measurement." >&2
    exit 1
elif [[ $log =~ timed\ out\ while\ waiting\ for\ target\ halted ]]; then
    if [[ $log =~ external\ reset\ detected ]]; then
        echo "The part reset during the run, after about ${elapsed}s."
        echo
        echo "SRAM survives a reset but VTOR does not, so the part came back"
        echo "running whatever is in flash -- Corsair's firmware, not this"
        echo "image. Everything below describes that, not the image under test."
        echo "Shorten --watch until the program counter is back in SRAM to see"
        echo "where this firmware actually gets to."
    else
        echo "Ran for ${WATCH}s without taking a fault, and was then stopped."
        echo "Whatever holds this firmware up, it is not a fault. Where it was:"
    fi
    echo
    printf '%s\n' "$log" | python3 -I "$here/decode-fault.py"
else
    echo "It faulted. The core was caught before its own handler ran:"
    echo
    printf '%s\n' "$log" | python3 -I "$here/decode-fault.py"
fi
echo
echo "Flash was not touched. A power cycle brings back whatever is in it."
