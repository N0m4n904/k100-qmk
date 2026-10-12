#!/usr/bin/env python3
"""Checks that every file destined to leave this repository states its licence.

The repository is GPL-3 by its LICENSE file, and three groups of files are not,
because they are written to be contributed to projects with other terms. A
per-file SPDX header is what carries that, and it is the only thing that travels
with a file once it is detached from this repository and merged into another.

So the rule this enforces is narrow and has a reason: **a file that will leave
must say its own licence; a file that stays may rely on LICENSE.** Nothing under
`chibios/`, `qmk/` or `tools/extract-ckb-keymap/` is going to stay.

It also checks the other direction, that no licence appears which LICENSING.md
does not explain, so the document cannot drift away from the files.

    tools/check-licensing.py
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

# Suffixes worth checking. Generated output and vendored third-party trees are
# not in the repository at all, so there is nothing to exclude.
SOURCE_SUFFIXES = {".c", ".h", ".cpp", ".py", ".rs", ".sh", ".mk", ".ld"}

# Longest matching prefix wins, so a single file can override its directory.
# This mirrors the table in LICENSING.md; if one changes the other must.
POLICY: list[tuple[str, set[str]]] = [
    # NXP's divider encoders, taken with their own licence attached.
    ("chibios/os/hal/ports/LPC/LPC546xx/lpc546xx_pll.c", {"BSD-3-Clause"}),
    # The HAL port and the demo go to ChibiOS-Contrib, whose LPC tree is Apache.
    ("chibios/os/hal/", {"Apache-2.0"}),
    ("chibios/demos/", {"Apache-2.0"}),
    # The startup files follow their own upstream neighbours, which are mixed.
    ("chibios/os/common/startup/", {"GPL-3.0-or-later", "MIT"}),
    # This project's own verification harness, which stays.
    ("chibios/test/", {"GPL-3.0-or-later"}),
    ("chibios/patches/", set()),
    # Destined for QMK, which is GPL-2-or-later.
    ("qmk/", {"GPL-2.0-or-later"}),
    # Linked against ckb-next, which is GPL-2 with no "or later" grant.
    ("tools/extract-ckb-keymap/", {"GPL-2.0-or-later"}),
]


def tracked_sources() -> list[Path]:
    out = subprocess.run(["git", "ls-files"], capture_output=True, text=True, check=True).stdout
    return [p for p in map(Path, out.split()) if p.suffix in SOURCE_SUFFIXES and p.is_file()]


def declared_licence(path: Path) -> str | None:
    """The SPDX identifier a file declares, if any."""
    try:
        text = path.read_text(errors="replace")
    except OSError:
        return None
    marker = "SPDX-License-Identifier:"

    # A declaration sits in the opening header and carries nothing but the
    # identifier. Prose *about* licensing also contains the marker -- one of
    # these scripts explains that NXP's headers carry a BSD-3-Clause one -- and
    # reading that as a declaration is a mistake this check made before the
    # trailing-text test was added.
    for line in text.splitlines()[:12]:
        if marker not in line:
            continue
        rest = line.split(marker, 1)[1].strip()
        identifier = rest.split()[0] if rest else ""
        trailing = rest[len(identifier):].strip().strip("*/#").strip()
        if identifier and not trailing:
            return identifier
    return None


def policy_for(path: Path) -> set[str] | None:
    """Which licences a path may declare, or None if it need not declare one."""
    best: tuple[int, set[str]] | None = None
    for prefix, allowed in POLICY:
        if str(path).startswith(prefix) and (best is None or len(prefix) > best[0]):
            best = (len(prefix), allowed)
    return None if best is None else best[1]


def main() -> int:
    problems: list[str] = []
    seen: set[str] = set()
    checked = 0

    for path in tracked_sources():
        allowed = policy_for(path)
        declared = declared_licence(path)

        if declared is not None:
            seen.add(declared)

        if allowed is None:
            # Stays in this repository, so LICENSE covers it. A header is
            # welcome but not required; if present it must still be explained.
            continue
        if not allowed:
            continue

        checked += 1
        if declared is None:
            problems.append(f"{path}: no SPDX-License-Identifier, but it is destined "
                            f"to leave this repository ({' or '.join(sorted(allowed))})")
        elif declared not in allowed:
            problems.append(f"{path}: declares {declared}, expected "
                            f"{' or '.join(sorted(allowed))}")

    # The document has to explain every licence actually present.
    explained = Path("LICENSING.md").read_text()
    for licence in sorted(seen):
        if licence not in explained:
            problems.append(f"LICENSING.md does not mention {licence}, which "
                            f"appears in the tree")

    if checked == 0:
        print("FAIL: no files were checked, so this proves nothing -- has the "
              "layout changed?")
        return 1

    for problem in problems:
        print(f"FAIL: {problem}")
    if problems:
        return 1

    print(f"ok: all {checked} files that will leave this repository declare a "
          f"licence, and LICENSING.md explains all {len(seen)} of them "
          f"({', '.join(sorted(seen))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
