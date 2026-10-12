# Licensing

**This file explains; `LICENSE` grants.** `LICENSE` is the GNU GPL version 3, unmodified, and it
is the licence this repository is under. This file is not a licence and grants nothing — it records
which files carry different terms and why, which is something a licence text cannot say about
itself.

Two files rather than one because they answer different questions. A tool or a licence scanner
wants `LICENSE`. A person wondering why the ChibiOS port is Apache-2.0 wants this. Mixing them
would mean editing the GPL text, which is both discouraged and would stop automated detection
recognising it.

`tools/check-licensing.py` keeps this file honest: it fails if a file destined for another project
does not declare its own licence, if one declares a licence this table does not allow, or if a
licence appears in the tree that this document does not mention.

`LICENSE` governs every file that does not say otherwise. Three groups of
files say otherwise, and each has a concrete reason: GPL-3 is incompatible with one of the
upstream projects this work builds on, and unacceptable to another that this work is meant to be
contributed back to.

A per-file SPDX header takes precedence over a repository-wide `LICENSE`, so the exceptions below
are expressed as headers in the files themselves. That is not a formality: **a header is the only
thing that travels with a file** once it is detached from this repository and merged into another,
which is the point of three of these four groups. Files that stay here may rely on `LICENSE`, and
the check enforces exactly that asymmetry.

| Path | License | Why |
| --- | --- | --- |
| `chibios/os/hal/**`, `chibios/demos/**` | Apache-2.0 | Destined for ChibiOS-Contrib, whose LPC HAL tree is Apache-2.0 |
| `chibios/os/hal/ports/LPC/LPC546xx/lpc546xx_pll.c` | BSD-3-Clause | Its divider encoders are NXP's, from the MCUXpresso SDK |
| `chibios/os/common/startup/**` | GPL-3.0-or-later and MIT, per file | Same destination, different neighbours — see below |
| `chibios/patches/**` | — | Diffs against ChibiOS-Contrib; each hunk carries the licence of the file it changes |
| `qmk/**` | GPL-2.0-or-later | Destined for QMK, which is GPL-2.0-or-later |
| `tools/extract-ckb-keymap/` | GPL-2.0-or-later | Links against ckb-next, which is GPL-2 with no "or later" grant |
| `chibios/test/**` | GPL-3.0-or-later | this project's verification harness |
| everything else | GPL-3.0 | original work, covered by `LICENSE` |

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

## The one NXP-derived source file

`lpc546xx_pll.c` keeps NXP's BSD-3-Clause notice because its contents are NXP's: the system PLL's
`NDEC`, `PDEC` and `MDEC` registers hold a position in a linear feedback shift register sequence
rather than a divider value, and the three encoders that compute it come from `fsl_clock.c` in the
MCUXpresso SDK, along with the loop filter coefficient selection.

There was no independent route to it. The encoding has to match the hardware's sequence exactly,
so writing it from a description would mean reproducing the same constants. BSD-3-Clause is
compatible with both Apache-2.0 and GPL-3 and asks only that the notice be retained, which it is.

Everything else under `chibios/` is this project's own.

## Firmware images

Corsair's firmware images are copyrighted and are **not** redistributed here under any licence.
`tools/fetch-firmware.py` downloads them from Corsair's own public endpoint.

NXP's and ARM's device headers are likewise not vendored; `chibios/test/check-registers.sh`
fetches them, which is why that script exists in the form it does.

## Caveat

This is a reading of what these projects' own files say, not legal advice.
