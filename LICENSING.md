# Licensing

`LICENSE` is **GPL-3.0**, and it governs every file that does not say otherwise. Three groups of
files say otherwise, and each has a concrete reason: GPL-3 is incompatible with one of the
upstream projects this work builds on, and unacceptable to another that this work is meant to be
contributed back to.

A per-file SPDX header takes precedence over a repository-wide `LICENSE`, so the exceptions below
are expressed as headers in the files themselves.

| Path | License | Why |
| --- | --- | --- |
| `chibios/os/hal/**`, `chibios/demos/**` | Apache-2.0 | Destined for ChibiOS-Contrib, whose LPC HAL tree is Apache-2.0 |
| `chibios/os/common/startup/**` | GPL-3.0 and MIT, per file | Same destination, different neighbours — see below |
| `chibios/patches/**` | — | Diffs against ChibiOS-Contrib; each hunk carries the licence of the file it changes |
| `qmk/**` | GPL-2.0-or-later | Destined for QMK, which is GPL-2.0-or-later |
| `tools/extract-ckb-keymap/` | GPL-2.0-or-later | Links against ckb-next, which is GPL-2 with no "or later" grant |
| everything else | GPL-3.0 | original work with no upstream constraint |

## The upstream licenses, as the projects themselves state them

**QMK** is **GPL-2.0-or-later.** Its `LICENSE` is the bare GPL v2 text, which is why automated
license detection reports it as GPL-2.0-only, but the per-file headers carry the upgrade clause —
`quantum/matrix.c` reads *"either version 2 of the License, or (at your option) any later
version."* GPL-2-or-later can be combined into a GPL-3 work, so **consuming** QMK under GPL-3 is
fine. **Contributing** to it is not: a GPL-3-only keyboard definition could not be merged, because
QMK could no longer distribute it under GPL-2. Hence `GPL-2.0-or-later` on `qmk/**`.

**ckb-next** is **GPL-2 only.** Its `LICENSE` is the GPL v2 text, its `README` states it is
*"licensed under the GNU General Public License (version 2)"* with no upgrade clause, and the two
files this project actually compiles against — `src/gui/keymap.cpp` and `src/daemon/keymap.c` —
carry no per-file header that would grant one. GPL-2-only and GPL-3 are mutually incompatible.

`tools/extract-ckb-keymap` is the only place this bites, and it bites hard: it is built *by
linking against ckb-next's own `keymap.cpp`*, so the resulting binary is a single combined work.
Under GPL-3 that binary would satisfy neither licence and could not lawfully be distributed by
anyone. Licensing the file `GPL-2.0-or-later` makes the combined binary distributable as GPL-2.

This only constrains that one directory. `tools/generate-qmk-layout.py` reads ckb-next's
`src/daemon/keymap.c` as *data* at runtime and links nothing, which is ordinary use rather than a
derivative work.

**ChibiOS-Contrib** has **no repository-level licence file at all** — licensing is entirely
per-file, and it is genuinely mixed. Every shared driver under `os/hal/ports/LPC/LLD/` that this
port builds on is Apache-2.0, as is the LPC11Uxx family directory it is modelled on. Apache-2.0
flows one way into GPL-3; GPL-3 does not flow back. So a GPL-3 `LPC546xx` family directory could
never be upstreamed, and upstreaming is not optional here: QMK requires new ARM families to land
in ChibiOS or ChibiOS-Contrib rather than in QMK itself, which is the whole premise of `chibios/`.
Those files therefore keep their neighbours' Apache-2.0 headers.

The startup files are the exception that proves the rule about reading per-file headers rather
than assuming a project has one licence. `os/common/startup/ARMCMx/devices/LPC11Uxx/cmparams.h`
is **GPL-3-or-later**, by the same author who licensed the HAL drivers Apache-2.0, and the LPC
linker scripts beside it are **MIT**. So `chibios/os/common/startup/**` follows its own
neighbours: GPL-3 for `cmparams.h`, MIT for the linker script. Both are acceptable to
ChibiOS-Contrib, since that is what is already there.

## Firmware images

Corsair's firmware images are copyrighted and are **not** redistributed here under any licence.
`tools/fetch-firmware.py` downloads them from Corsair's own public endpoint.

NXP's and ARM's device headers are likewise not vendored; `chibios/test/check-registers.sh`
fetches them, which is why that script exists in the form it does.

## Caveat

This is a reading of what these projects' own files say, not legal advice.
