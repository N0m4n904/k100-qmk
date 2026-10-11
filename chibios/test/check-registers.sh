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

family="$here/../os/hal/ports/LPC/LPC546xx"
cc=(arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Wall -Wextra -Werror
    -I"$work" -I"$work/CMSIS" -I"$family" "-D$cpu" "-D$part")

echo "compiling the register check"
"${cc[@]}" -c "$here/register-check.c" -o "$work/register-check.o"

echo "ok: every register field used by the hal_lld, the PAL driver and the USB compat header exists in $part.h"

# register-check.c mirrors the port's expressions rather than including it, so
# it cannot catch a mistake in the port's own macros. pal-check.c includes the
# real PAL header and asserts the exact word each pad mode writes.
echo "compiling the PAL encoding check"
"${cc[@]}" -c "$here/pal-check.c" -o "$work/pal-check.o"

echo "ok: every PAL pad mode encodes to the documented IOCON word"

# Both checks above pass by compiling, so each needs proving it can fail --
# otherwise a run that reports success says nothing at all.
#
# reject <description> <source> <sed expression>
#
# Breaks one thing in a copy of the source and requires the compiler to refuse
# it. -Werror is dropped so that the copy fails on the seeded fault rather than
# on a warning it happens to provoke.
reject() {
    local what="$1" source="$2" edit="$3"

    echo "verifying the check rejects $what"
    sed "$edit" "$source" > "$work/bad.c"
    if "${cc[@]/-Werror/}" -c "$work/bad.c" -o /dev/null 2>/dev/null; then
        echo "FAIL: $what compiled, so this check proves nothing" >&2
        exit 1
    fi
    echo "ok: $what is rejected"
}

reject "a nonexistent register field" "$here/register-check.c" \
    's/SYSCON_FROCTRL_HSPDCLK_MASK/SYSCON_FROCTRL_NO_SUCH_FIELD/'

# The LPC11Uxx pull-mode shift, which is the specific mistake a port adapted
# from the sibling family would make: it compiles, and configures the wrong pad.
reject "a pad field at its LPC11Uxx bit position" "$here/pal-check.c" \
    's/#define UM_MODE(n).*/#define UM_MODE(n) ((uint32_t)(n) << 3)/'
