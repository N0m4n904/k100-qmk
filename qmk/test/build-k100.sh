#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds QMK firmware for the K100, assembling everything it needs first.
#
# Same reasoning as chibios/test/build-demo.sh: this repository carries only its
# own files, so the build has to fetch QMK, QMK's ChibiOS forks and NXP's device
# headers, patch them, drop this port in and build. Written as a script rather
# than as instructions, it either works or fails -- prose would drift.
#
#   nix develop .#qmk
#   qmk/test/build-k100.sh [work-dir] [-- extra make args]
#   LDSCRIPT=LPC546xx_sram qmk/test/build-k100.sh [work-dir]
#
# Downloads are cached in the work directory, so a second run is fast.
set -euo pipefail

qmk_ref=master
part=LPC54605
keyboard=corsair/k100
keymap=${KEYMAP:-default}

# Which memory layout to link for. The default is the real one: behind Corsair's
# bootloader, in flash. LDSCRIPT=LPC546xx_sram links an image that runs out of
# SRAM instead, for loading with a debugger and starting by hand -- which leaves
# nothing behind on the part and is how boot failures are chased without
# spending a flash sector on each attempt. A command-line variable beats the
# rules.mk assignment, which is what makes this work.
ldscript=${LDSCRIPT:-LPC546xx_app_at_64k}

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
work="${1:-$(mktemp -d)}"
mkdir -p "$work"
work="$(cd "$work" && pwd)"
cache="$work/cache"

echo "work directory: $work"

fetch() {
    local url="$1" out="$2"
    [ -s "$out" ] && return 0
    mkdir -p "$(dirname "$out")"
    curl -fsSL -o "$out" "$url"
}

# QMK pins its submodules to exact commits, and the branch heads are not those
# commits -- using a head gets a ChibiOS whose layout QMK's build does not
# expect, which surfaces as a missing chlib.h in completely unrelated code.
# Always resolve the pinned sha.
pinned_sha() {
    local path="$1"
    fetch "https://api.github.com/repos/qmk/qmk_firmware/contents/lib/$path" \
          "$cache/sha-$path.json"
    python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['sha'])" \
        "$cache/sha-$path.json"
}

unpack() {
    local tarball="$1" dir="$2"
    rm -rf "$dir"
    mkdir -p "$dir"
    tar xzf "$tarball" -C "$dir" --strip-components=1
}

qmk="$work/qmk_firmware"

echo "fetching QMK ($qmk_ref)"
fetch "https://github.com/qmk/qmk_firmware/archive/refs/heads/$qmk_ref.tar.gz" \
      "$cache/qmk.tar.gz"
unpack "$cache/qmk.tar.gz" "$qmk"

# Only the submodules an ARM ChibiOS build reaches. lufa is in the list because
# QMK's ChibiOS USB stack borrows LUFA's HID class headers, which is not
# obvious until it fails.
declare -A repos=(
    [chibios]=ChibiOS
    [chibios-contrib]=ChibiOS-Contrib
    [printf]=printf
    [lufa]=lufa
)
for sub in chibios chibios-contrib printf lufa; do
    sha="$(pinned_sha "$sub")"
    echo "fetching lib/$sub at ${sha:0:12}"
    fetch "https://github.com/qmk/${repos[$sub]}/archive/$sha.tar.gz" \
          "$cache/$sub-$sha.tar.gz"
    unpack "$cache/$sub-$sha.tar.gz" "$qmk/lib/$sub"
done

echo "fetching the NXP $part device headers"
nxp=https://raw.githubusercontent.com/nxp-mcuxpresso/mcux-sdk/main/devices/$part
for f in fsl_device_registers.h $part.h ${part}_features.h \
         system_$part.h system_$part.c; do
    fetch "$nxp/$f" "$cache/nxp/$part/$f"
done

contrib="$qmk/lib/chibios-contrib"

echo "applying patches"
git -C "$contrib" init -q .
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/chibios/patches/0001-usb-family-seam.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/chibios/patches/0003-bound-usb-controller-waits.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/chibios/patches/0004-usb-reset-before-irq.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/chibios/patches/0005-eplist-size-from-controller.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/chibios/patches/0006-eplist-buffer-base.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/chibios/patches/0009-eplist-nbytes-position.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/chibios/patches/0008-systick-freerunning-guard.patch"
git -C "$contrib" apply --whitespace=nowarn \
    "$repo/chibios/patches/0010-devcmdstat-latched-bits.patch"
git -C "$qmk" init -q .
git -C "$qmk" apply --whitespace=nowarn \
    "$repo/qmk/patches/0001-mcu-selection-lpc546xx.patch"
git -C "$qmk" apply --whitespace=nowarn \
    "$repo/qmk/patches/0002-main-loop-checkpoints.patch"

# ChibiOS proper, not Contrib: port_init() enables the DWT cycle counter
# unconditionally, and this keyboard's debug block is not accessible to
# software, so that write faults inside chSysInit(). See the keyboard's
# chconf.h, which is what turns it off.
git -C "$qmk/lib/chibios" init -q .
git -C "$qmk/lib/chibios" apply --whitespace=nowarn \
    "$repo/chibios/patches/0002-optional-dwt-enable.patch"
git -C "$qmk/lib/chibios" apply --whitespace=nowarn \
    "$repo/chibios/patches/0007-external-realtime-counter.patch"

echo "placing the LPC546xx port into ChibiOS-Contrib"
cp -r "$repo/chibios/os/hal/ports/LPC/LPC546xx" "$contrib/os/hal/ports/LPC/"
cp -r "$repo/chibios/os/common/startup/ARMCMx/devices/LPC546xx" \
      "$contrib/os/common/startup/ARMCMx/devices/"
cp "$repo/chibios/os/common/startup/ARMCMx/compilers/GCC/ld/"*.ld \
   "$contrib/os/common/startup/ARMCMx/compilers/GCC/ld/"
cp "$repo/chibios/os/common/startup/ARMCMx/compilers/GCC/mk/"*.mk \
   "$contrib/os/common/startup/ARMCMx/compilers/GCC/mk/"

# QMK builds only against what it vendors, so the device headers go beside the
# LPC11Uxx.h that ChibiOS-Contrib already keeps there. They are BSD-3-Clause by
# their own SPDX identifier.
mkdir -p "$contrib/os/common/ext/CMSIS/LPC/$part"
cp "$cache/nxp/$part/"* "$contrib/os/common/ext/CMSIS/LPC/$part/"

echo "placing the QMK platform support and the keyboard"
cp -r "$repo/qmk/platforms/chibios/vendors/LPC" "$qmk/platforms/chibios/vendors/"
cp -r "$repo/qmk/platforms/chibios/boards/GENERIC_LPC_LPC54605" \
      "$qmk/platforms/chibios/boards/"
cp -r "$repo/qmk/drivers/led/issi/." "$qmk/drivers/led/issi/"
mkdir -p "$qmk/keyboards/$keyboard"
cp -r "$repo/qmk/keyboards/$keyboard/." "$qmk/keyboards/$keyboard/"

echo "building $keyboard:$keymap, linked by $ldscript"
make -C "$qmk" "$keyboard:$keymap" MCU_LDSCRIPT="$ldscript" "${@:2}"

elf="$qmk/.build/$(echo "$keyboard" | tr / _)_$keymap.elf"
echo
echo "ok: QMK builds for the K100"
echo

# The same linker-decided properties the ChibiOS demo checks: the boot ROM image
# checksum, and the alignment, region and bus-reachability of the USB endpoint
# memory. All of them fail silently on hardware.
python3 "$repo/tools/check-firmware-image.py" "$elf"

echo
arm-none-eabi-size "$elf"
echo
echo "elf: $elf"
