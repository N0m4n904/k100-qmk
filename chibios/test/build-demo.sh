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

# Two properties of the linked image that no source file can assert, because the
# linker decides them. Shared with the QMK build, which needs the same ones.
python3 "$root/tools/check-firmware-image.py" "$elf"

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
