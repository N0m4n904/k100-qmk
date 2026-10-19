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
#   sudo swd/dump-flash.sh
#
# Nothing here writes to the keyboard.
#
# Note that with the core halted out of reset, 0x00000000 may not show the
# flash: see "What is actually at 0x00000000" in README.md.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

OUT=$PWD
FLASH_BASE=0x00000000
FLASH_SIZE=0x80000      # 512 KB
BOOTLOADER_SIZE=0x10000 #  64 KB, the irreplaceable part

# --out is this script's own; everything else belongs to the session and is
# handed straight through.
session=()
while (( $# )); do
    case $1 in
        --out) OUT=$2; shift 2 ;;
        *) session+=("$1"); shift ;;
    esac
done

mkdir -p "$OUT"

# Opening the session, retrying a read that did not halt the part and comparing
# two passes all live in read-memory.sh; this adds the flash's address and
# size, and what to do with the result.
"$here/read-memory.sh" "${session[@]}" \
    "$FLASH_BASE" "$FLASH_SIZE" "$OUT/k100-flash-512k.bin"

dd if="$OUT/k100-flash-512k.bin" of="$OUT/k100-bootloader-64k.bin" \
   bs=4096 count=$(( BOOTLOADER_SIZE / 4096 )) status=none

cat <<EOF

  $OUT/k100-flash-512k.bin      all 512 KB
  $OUT/k100-bootloader-64k.bin  the first 64 KB

Copy both off the Pi before going any further.

Then check what the first 64 KB actually are, because with the core halted out
of reset the boot ROM may be what answers at 0x00000000 rather than the flash:

  sudo $here/read-memory.sh 0x03000000 0x10000 rom-64k.bin
  cmp rom-64k.bin $OUT/k100-bootloader-64k.bin

If those two are identical, the first 64 KB of the dump are NXP's ROM and
Corsair's bootloader has not been read yet.
EOF
