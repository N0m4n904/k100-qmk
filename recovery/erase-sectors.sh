#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Erases a range of flash sectors, and reads the range back to prove it.
#
#   sudo recovery/erase-sectors.sh 2 2      # just the first application sector
#   sudo recovery/erase-sectors.sh 2 6      # enough for the stock application
#
# Sectors are 32 KB. Sector 0 and sector 1 are Corsair's bootloader, which
# exists on no other device and in no public archive, so this refuses to touch
# them. --i-have-a-verified-dump lifts that, and is spelled out at that length
# on purpose: the only honest reason to erase them is that a dump of them has
# been taken, checked and copied somewhere else, and whoever types it is saying
# so.
#
# Options: --swclk, --swdio, --srst, --speed.

set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=recovery/swd-session.sh
source "$here/swd-session.sh"

SECTOR_SIZE=$(( 32 * 1024 ))
BOOTLOADER_SECTORS=2   # sectors 0 and 1
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

(( $# == 2 )) || { echo "usage: $0 [options] <first sector> <last sector>" >&2; exit 1; }

first=$1
last=$2

(( first <= last )) || { echo "first sector is above last" >&2; exit 1; }
(( last <= 15 )) || { echo "this part has sectors 0 to 15" >&2; exit 1; }

if (( first < BOOTLOADER_SECTORS )) && [[ $allow_bootloader == no ]]; then
    cat >&2 <<EOF
Sector $first is part of Corsair's bootloader (sectors 0 and 1).

No copy of it is published anywhere, and their update protocol exposes no way
to read it back, so erasing it without a dump destroys it for good -- for this
keyboard and for anyone who ever attempts this again.

If you have dumped it, checked it and copied it off this machine, say so:
  $0 --i-have-a-verified-dump $first $last
EOF
    exit 1
fi

address=$(( first * SECTOR_SIZE ))
length=$(( (last - first + 1) * SECTOR_SIZE ))

printf 'erasing sectors %d to %d: 0x%08x, %d bytes\n' \
    "$first" "$last" "$address" "$length"

readback=$(mktemp)
trap 'rm -f "$readback"' EXIT

log=$(swd_run \
    -c "flash erase_sector 0 $first $last" \
    -c "dump_image $readback $address $length" || true)
printf '%s\n' "$log"

swd_halted "$log" || {
    echo "the part did not halt, so the erase cannot be trusted" >&2
    exit 1
}

# An erase that reported success and did not happen is the failure worth
# catching, because everything after this assumes the range is empty. Erased
# NOR flash reads as 0xFF, so the range itself is the evidence.
python3 -I - "$readback" "$length" <<'PY'
import sys

data = open(sys.argv[1], "rb").read()
want = int(sys.argv[2])

if len(data) != want:
    sys.exit(f"read back {len(data)} bytes of {want}; cannot confirm the erase")

left = len(data) - data.count(0xFF)
if left:
    sys.exit(f"{left} of {len(data)} bytes are not 0xFF: the erase did not take")

print(f"\nverified: all {len(data)} bytes read back as 0xFF.")
PY
