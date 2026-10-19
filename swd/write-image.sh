#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Writes a raw binary into flash and then has the part prove it holds it.
#
#   sudo swd/write-image.sh build/corsair_k100_default.bin 0x00010000
#
# This is the development path, not the recovery one. A QMK build goes straight
# into flash here with no container header and no vendor protocol: the image is
# started by its own vector table rather than by Corsair's bootloader reading
# an entry address out of a header, so it has to be linked for wherever it is
# written. qmk/test/build-k100.sh links to 0x00010400 by default; see
# chibios/os/common/startup/ARMCMx/compilers/GCC/ld/.
#
# OpenOCD erases the sectors the image covers and no others. Afterwards
# verify_image reads the flash back and compares it against the file, so a
# write that silently did not take is caught here rather than as a keyboard
# that does nothing.
#
# Sectors 0 and 1 hold Corsair's bootloader; writing into them is refused for
# the reasons in erase-sectors.sh.
#
# Options: --swclk, --swdio, --srst, --speed.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=swd/swd-session.sh
source "$here/swd-session.sh"

BOOTLOADER_END=$(( 0x10000 ))
allow_bootloader=no

while (( $# )); do
    if [[ ${1:-} == --i-have-a-verified-dump ]]; then
        allow_bootloader=yes
        shift
        continue
    fi
    swd_parse_option "$@"
    (( swd_shift < 0 )) && break
    shift "$swd_shift"
done

(( $# == 2 )) || { echo "usage: $0 [options] <file> <address>" >&2; exit 1; }

IMAGE=$1
ADDRESS=$2

[[ -s $IMAGE ]] || { echo "$IMAGE is missing or empty" >&2; exit 1; }

if (( ADDRESS < BOOTLOADER_END )) && [[ $allow_bootloader == no ]]; then
    cat >&2 <<EOF
0x$(printf '%08x' "$ADDRESS") is inside Corsair's bootloader, which occupies the first 64 KB.

Writing there replaces the one piece of this keyboard that cannot be downloaded
again. If you have dumped it, checked it and copied it off this machine:
  $0 --i-have-a-verified-dump $IMAGE $ADDRESS
EOF
    exit 1
fi

printf 'writing %s (%s bytes) to 0x%08x\n' "$IMAGE" "$(stat -c %s "$IMAGE")" "$ADDRESS"

log=$(swd_run \
    -c "flash write_image erase $IMAGE $ADDRESS bin" \
    -c "verify_image $IMAGE $ADDRESS bin" || true)
printf '%s\n' "$log"

swd_halted "$log" || {
    echo "the part did not halt, so nothing here can be relied on" >&2
    exit 1
}

# OpenOCD says "verified" on success and reports a differing byte otherwise.
# Its exit status does not distinguish the two reliably, so match the wording.
if [[ $log =~ verified\ [0-9]+\ bytes ]]; then
    echo
    echo "The flash reads back as the file. Reset the part to run it:"
    echo "  sudo $here/reset.sh"
else
    echo >&2
    echo "The flash does not read back as the file, so the write did not take." >&2
    exit 1
fi
