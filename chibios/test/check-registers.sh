#!/usr/bin/env bash
# Compiles register-check.c against NXP's real device header for the LPC54605,
# so that a register field the hal_lld invented fails here rather than silently
# on hardware.
#
# Headers are fetched rather than vendored: NXP's and ARM's are theirs, and
# pinning them here would mean redistributing them.
#
#   nix develop .#firmware    # for arm-none-eabi-gcc
#   chibios/test/check-registers.sh
set -euo pipefail

part=LPC54605
cpu=CPU_LPC54605J512ET100
here="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

nxp=https://raw.githubusercontent.com/nxp-mcuxpresso/mcux-sdk/main/devices/$part
cmsis=https://raw.githubusercontent.com/ARM-software/CMSIS_5/develop/CMSIS/Core/Include

echo "fetching $part and CMSIS headers"
curl -fsS -o "$work/$part.h" "$nxp/$part.h"
mkdir -p "$work/CMSIS"
for header in core_cm4.h cmsis_compiler.h cmsis_gcc.h cmsis_version.h mpu_armv7.h; do
    curl -fsS -o "$work/CMSIS/$header" "$cmsis/$header"
done

# NXP's header pulls in a per-part system header that only declares the clock
# variable and init hooks, and which lives in the SDK rather than beside the
# device header. A stub is enough for a register name check.
cat > "$work/system_$part.h" <<EOF
#ifndef _SYSTEM_${part}_H_
#define _SYSTEM_${part}_H_
#include <stdint.h>
extern uint32_t SystemCoreClock;
void SystemInit(void);
void SystemCoreClockUpdate(void);
#endif
EOF

echo "compiling the register check"
arm-none-eabi-gcc -c "$here/register-check.c" -o "$work/check.o" \
    -mcpu=cortex-m4 -mthumb -Wall -Wextra -Werror \
    -I"$work" -I"$work/CMSIS" "-D$cpu"

echo "ok: every register field used by the LPC546xx hal_lld exists in $part.h"

# Prove the check can fail, so a passing run means something.
echo "verifying the check rejects a bad field name"
sed 's/SYSCON_FROCTRL_HSPDCLK_MASK/SYSCON_FROCTRL_NO_SUCH_FIELD/' \
    "$here/register-check.c" > "$work/bad.c"
if arm-none-eabi-gcc -c "$work/bad.c" -o /dev/null \
       -mcpu=cortex-m4 -mthumb -I"$work" -I"$work/CMSIS" "-D$cpu" 2>/dev/null; then
    echo "FAIL: a nonexistent field compiled, so this check proves nothing" >&2
    exit 1
fi
echo "ok: a nonexistent field is rejected"
