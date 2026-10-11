#!/usr/bin/env bash
# Builds the LPC546xx demo, assembling everything it needs from upstream first.
#
# This repository carries only its own files. ChibiOS, ChibiOS-Contrib and NXP's
# device headers are other people's, and vendoring them would mean
# redistributing them and pinning them to whatever they happened to be on the
# day they were copied. So this script fetches them, drops this port into place,
# and builds -- which also means the build instructions cannot rot into
# something that no longer works, because they are the thing that runs.
#
#   nix develop .#firmware
#   chibios/test/build-demo.sh
#
# Downloads are cached in the work directory, so a second run is fast. Pass a
# directory to keep the tree somewhere you can poke at it:
#
#   chibios/test/build-demo.sh ~/lpc546xx-build
#
set -euo pipefail

# Both snapshots come from the same branch pair, and ChibiOS-Contrib's default
# branch is named for the ChibiOS release it tracks.
chibios_ref=stable_21.11.x
contrib_ref=chibios-21.11.x
part=LPC54605
cpu=LPC54605J512ET100

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
work="${1:-$(mktemp -d)}"
mkdir -p "$work"
work="$(cd "$work" && pwd)"

echo "work directory: $work"

fetch() {
    local url="$1" out="$2"
    [ -s "$out" ] && return 0
    mkdir -p "$(dirname "$out")"
    curl -fsSL -o "$out" "$url"
}

unpack() {
    local tarball="$1" dir="$2"
    [ -d "$dir" ] && return 0
    mkdir -p "$dir"
    tar xzf "$tarball" -C "$dir" --strip-components=1
}

echo "fetching ChibiOS $chibios_ref and ChibiOS-Contrib $contrib_ref"
fetch "https://github.com/ChibiOS/ChibiOS/archive/refs/heads/$chibios_ref.tar.gz" \
      "$work/cache/chibios.tar.gz"
fetch "https://github.com/ChibiOS/ChibiOS-Contrib/archive/refs/heads/$contrib_ref.tar.gz" \
      "$work/cache/contrib.tar.gz"

# NXP's device headers, startup and the per-part feature list. Fetched for the
# same reason as above: they are NXP's to distribute, not this project's.
echo "fetching the NXP $part device headers"
nxp=https://raw.githubusercontent.com/nxp-mcuxpresso/mcux-sdk/main/devices/$part
for f in fsl_device_registers.h $part.h ${part}_features.h \
         system_$part.h system_$part.c; do
    fetch "$nxp/$f" "$work/cache/nxp/$part/$f"
done

chibios="$work/ChibiOS"
contrib="$work/ChibiOS-Contrib"
unpack "$work/cache/chibios.tar.gz" "$chibios"

# The Contrib tree is rebuilt from the tarball every run, because the patch and
# the port files are copied into it and a second apply would fail.
rm -rf "$contrib"
unpack "$work/cache/contrib.tar.gz" "$contrib"

echo "applying the shared USB driver patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/patches/0001-usb-family-seam.patch"

# Beside LPC11Uxx.h, which is where ChibiOS-Contrib already keeps an NXP device
# header. This is the path a build that cannot fetch anything has to take -- so
# it is the one exercised here, in preference to NXP_SDK_DEVICE_DIR.
echo "vendoring the NXP $part headers into the Contrib tree"
mkdir -p "$contrib/os/common/ext/CMSIS/LPC/$part"
cp "$work/cache/nxp/$part/"* "$contrib/os/common/ext/CMSIS/LPC/$part/"

echo "placing the LPC546xx port"
cp -r "$repo/os/hal/ports/LPC/LPC546xx" "$contrib/os/hal/ports/LPC/"
cp -r "$repo/os/common/startup/ARMCMx/devices/LPC546xx" \
      "$contrib/os/common/startup/ARMCMx/devices/"
cp "$repo/os/common/startup/ARMCMx/compilers/GCC/ld/"*.ld \
   "$contrib/os/common/startup/ARMCMx/compilers/GCC/ld/"
cp "$repo/os/common/startup/ARMCMx/compilers/GCC/mk/"*.mk \
   "$contrib/os/common/startup/ARMCMx/compilers/GCC/mk/"
cp -r "$repo/demos/LPC/LPC54605_GENERIC" "$contrib/demos/LPC/"

echo "building"
make -C "$contrib/demos/LPC/LPC54605_GENERIC" \
    CHIBIOS="$chibios" \
    CHIBIOS_CONTRIB="$contrib" \
    "${@:2}"

elf="$contrib/demos/LPC/LPC54605_GENERIC/build/ch.elf"
echo
echo "ok: the port builds and links"
echo

# Three properties of the linked image that nothing in the source can assert,
# because each is decided by the linker. All three fail silently on hardware:
# a bad checksum means the boot ROM never starts the image, and misplaced
# endpoint memory means USB quietly does not work.
python3 - "$elf" <<'PY'
import re, subprocess, sys

elf = sys.argv[1]

def run(*args):
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout

# The boot ROM sums the first eight vectors and refuses to start the image
# unless the result is zero; the linker script puts the complement at 0x1C.
dump = run("arm-none-eabi-objdump", "-s", "-j", ".vectors", elf)
words = []
for line in dump.splitlines():
    m = re.match(r"\s*([0-9a-f]{4,8})\s((?:[0-9a-f]{8} ){1,4})", line)
    if m:
        words += [int(w, 16) for w in
                  (bytes.fromhex(g)[::-1].hex() for g in m.group(2).split())]
# Without this the check would be worthless: a parse that matched nothing
# leaves an empty list, whose sum is zero, which is exactly the pass condition.
if len(words) < 8:
    print(f"vector checksum         FAIL   parsed {len(words)} vectors, "
          "so this check proves nothing -- has objdump's output format changed?")
    sys.exit(1)

checksum = sum(words[:8]) & 0xFFFFFFFF
print(f"vector checksum          {'ok' if checksum == 0 else 'FAIL'}"
      f"   first 8 vectors sum to 0x{checksum:08x}")

# The USB endpoint list and buffers: 256-byte aligned for EPLISTSTART, inside
# one 4MB window because DATABUFSTART holds only the high address bits, and in
# SRAM the USB controller can reach as a bus master rather than in SRAMX.
syms = run("arm-none-eabi-nm", "-S", elf)
m = re.search(r"^([0-9a-f]+) ([0-9a-f]+) . lpc_usb_sram$", syms, re.M)
if m is None:
    print("usb endpoint memory     FAIL   lpc_usb_sram not in the image")
    sys.exit(1)
addr, size = int(m.group(1), 16), int(m.group(2), 16)
checks = {
    "256-byte aligned": addr % 256 == 0,
    "single 4MB window": (addr >> 22) == ((addr + size - 1) >> 22),
    "in reachable SRAM": 0x20000000 <= addr < 0x20028000,
}
bad = [name for name, good in checks.items() if not good]
print(f"usb endpoint memory      {'ok' if not bad else 'FAIL ' + ', '.join(bad)}"
      f"   0x{addr:08x}, {size} bytes, " + ", ".join(checks))

if checksum != 0 or bad:
    sys.exit(1)
PY

echo
arm-none-eabi-size "$elf"
echo
echo "elf: $elf"
