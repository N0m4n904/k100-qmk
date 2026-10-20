#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
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
# Which board to build for. The default is a bare LPC54605, the part in the
# keyboard; LPCXPRESSO54628 is the devboard recommended as the first target,
# and building both is what keeps the port from quietly depending on one.
#
#   BOARD=LPCXPRESSO54628 chibios/test/build-demo.sh
# Which clock configuration. FRO is 48 MHz straight from the on-chip
# oscillator with no PLL, which is what a first bring-up should use; PLL is
# 180 MHz, which is what the stock keyboard firmware runs at. Both are built,
# because the PLL path is otherwise never compiled.
clock="${CLOCK:-FRO}"
case "$clock" in
  FRO) clockdefs= ;;
  PLL) clockdefs="-DLPC_MAINCLKSELB=LPC_MAINCLKSELB_PLL" ;;
  *) echo "unknown CLOCK: $clock" >&2; exit 2 ;;
esac

board="${BOARD:-GENERIC}"
case "$board" in
  GENERIC)          part=LPC54605; cpu=CPU_LPC54605J512ET100; boarddir= ;;
  LPCXPRESSO54628)  part=LPC54628; cpu=CPU_LPC54628J512ET180
                    boarddir=os/hal/boards/LPCXPRESSO54628 ;;
  *) echo "unknown BOARD: $board" >&2; exit 2 ;;
esac

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
root="$(cd "$here/../.." && pwd)"
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
echo "building for $board, part $part, clock $clock"
echo "fetching the NXP $part device headers"
nxp=https://raw.githubusercontent.com/nxp-mcuxpresso/mcux-sdk/main/devices/$part
for f in fsl_device_registers.h $part.h ${part}_features.h \
         system_$part.h system_$part.c; do
    fetch "$nxp/$f" "$work/cache/nxp/$part/$f"
done

chibios="$work/ChibiOS"
contrib="$work/ChibiOS-Contrib"

# Both trees are rebuilt from their tarballs every run, because both get patched
# and a second apply fails. The tarballs themselves stay cached, so this costs
# an extract rather than a download.
rm -rf "$chibios" "$contrib"
unpack "$work/cache/chibios.tar.gz" "$chibios"
unpack "$work/cache/contrib.tar.gz" "$contrib"

echo "applying the shared USB driver patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/patches/0001-usb-family-seam.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/patches/0003-bound-usb-controller-waits.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/patches/0004-usb-reset-before-irq.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/patches/0005-eplist-size-from-controller.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/patches/0006-eplist-buffer-base.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/patches/0009-eplist-nbytes-position.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/patches/0008-systick-freerunning-guard.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/patches/0010-devcmdstat-latched-bits.patch"

# And the two to ChibiOS proper, which are a pair: one makes enabling the DWT
# optional, the other supplies a realtime counter for the parts that have to turn
# it off. The demo's chconf.h does both, so this build is where they are actually
# compiled -- the register checks cannot reach them, and a patch nothing compiles
# rots against a newer ChibiOS without saying so.
echo "applying the ChibiOS-proper patches"
git -C "$chibios" init -q . 2>/dev/null || true
git -C "$chibios" apply --whitespace=nowarn \
    "$repo/patches/0002-optional-dwt-enable.patch"
git -C "$chibios" apply --whitespace=nowarn \
    "$repo/patches/0007-external-realtime-counter.patch"

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
mkdir -p "$contrib/os/hal/boards"
cp -r "$repo/os/hal/boards/LPCXPRESSO54628" "$contrib/os/hal/boards/"

echo "building"
make -C "$contrib/demos/LPC/LPC54605_GENERIC" \
    CHIBIOS="$chibios" \
    CHIBIOS_CONTRIB="$contrib" \
    ${boarddir:+BOARDDIR="$contrib/$boarddir"} \
    PARTDEFS="-D$cpu -D$part $clockdefs" \
    LPC_PART="$part" \
    "${@:2}"

elf="$contrib/demos/LPC/LPC54605_GENERIC/build/ch.elf"
echo
echo "ok: the port builds and links"
echo

# Two properties of the linked image that no source file can assert, because the
# linker decides them. Shared with the QMK build, which needs the same ones.
# --allow-dwt: this demo targets a devboard, whose debug block works, so it keeps
# ChibiOS's stock cycle counter. The keyboard cannot, and its build checks that.
python3 "$root/tools/check-firmware-image.py" "$elf" --allow-dwt

# The self-test structure exists only to be read by a debugger, so nothing in
# the program references it -- which means link-time optimisation will remove it
# unless it is marked used, and did exactly that once. Checked here because the
# failure is silent: the firmware still builds and runs, and the answers it
# exists to report are simply not in the image.
if ! arm-none-eabi-nm "$elf" | grep -qE ' [BbDd] selftest$'; then
    echo "selftest structure    FAIL   not in the image -- has it lost its used attribute?"
    exit 1
fi
echo "selftest structure       ok   $(arm-none-eabi-nm -S "$elf" \
    | awk '$4 == "selftest" { printf "at 0x%s, %d bytes", $1, strtonum("0x" $2) }')"

echo
arm-none-eabi-size "$elf"
echo
echo "elf: $elf"
