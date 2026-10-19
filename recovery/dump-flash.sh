#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Reads all 512 KB of the LPC54605's internal flash out over SWD, twice, and
# refuses to call it a dump unless the two agree.
#
# This is the one step of the whole recovery that cannot be repeated later.
# Corsair's bootloader occupies the first 64 KB, their update protocol exposes
# no file ID that reads it back, and no copy of it exists anywhere public. Erase
# it without a dump and it is gone for this keyboard and for everyone who tries
# this afterwards. Everything else here is reversible; this is not.
#
# Hence reading it twice. These are flying leads on a bare board, and a dump
# taken through a wire that moved is wrong in ways that only show up much later,
# when it is used to restore a part. Two independent passes that match cannot
# both have been corrupted the same way.
#
#   sudo recovery/dump-flash.sh --swclk 25 --swdio 24
#
# Nothing here writes to the keyboard.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

SWCLK=25
SWDIO=24
SRST=
OUT=$PWD
SPEED=100

FLASH_BASE=0x00000000
FLASH_SIZE=0x80000      # 512 KB
BOOTLOADER_SIZE=0x10000 #  64 KB, the irreplaceable part

while (( $# )); do
    case $1 in
        --swclk) SWCLK=$2; shift 2 ;;
        --swdio) SWDIO=$2; shift 2 ;;
        --srst)  SRST=$2;  shift 2 ;;
        --out)   OUT=$2;   shift 2 ;;
        --speed) SPEED=$2; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

mkdir -p "$OUT"

dump_once() {
    local target=$1
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
        -f "$here/lpc54605.cfg" \
        -c "init" \
        -c "dump_image $target $FLASH_BASE $FLASH_SIZE" \
        -c "shutdown"
}

echo "pass 1 of 2"
dump_once "$OUT/k100-flash-pass1.bin"

echo
echo "pass 2 of 2"
dump_once "$OUT/k100-flash-pass2.bin"

echo
if ! cmp "$OUT/k100-flash-pass1.bin" "$OUT/k100-flash-pass2.bin"; then
    cat >&2 <<'EOF'

The two passes disagree, so neither can be trusted.

Usually a lead that moved. Re-seat everything, and if it keeps happening lower
the clock:  --speed 50

Both passes have been kept so they can be compared, but do not erase anything
on the strength of either one.
EOF
    exit 1
fi

mv "$OUT/k100-flash-pass1.bin" "$OUT/k100-flash-512k.bin"
rm -f "$OUT/k100-flash-pass2.bin"

dd if="$OUT/k100-flash-512k.bin" of="$OUT/k100-bootloader-64k.bin" \
   bs=1 count=$((BOOTLOADER_SIZE)) status=none

# A broken SWD read does not always fail; it can succeed and hand back a
# constant. Two passes agreeing would not catch that, because they would agree
# on the constant. Counting the distinct byte values does catch it.
verdict=$(python3 - "$OUT/k100-flash-512k.bin" <<'PY'
import sys

distinct = len(set(open(sys.argv[1], "rb").read()))
print("looks like real content" if distinct > 2 else
      f"only {distinct} distinct byte value(s) -- NOT a real dump")
PY
)

cat <<EOF

Both passes matched.

  $OUT/k100-flash-512k.bin      all 512 KB, $verdict
  $OUT/k100-bootloader-64k.bin  Corsair's bootloader

Copy both off the Pi before going any further. Once they are somewhere else,
nothing after this point can lose anything that cannot be put back.
EOF
