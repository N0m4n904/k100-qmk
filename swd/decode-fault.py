#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Turns a Cortex-M's fault status registers into a sentence.

Reads an OpenOCD session's output on stdin, picks out the core registers and
the System Control Block's fault words, and says what faulted and where.

The bit names are from ARMv7-M's own definitions, so this does not encode
anything about this keyboard and would read any Cortex-M3, M4 or M7 the same.

    swd/fault-report.sh | swd/decode-fault.py
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass

# System Control Block, from the address fault-report.sh dumps.
SCB_BASE = 0xE000ED24
SHCSR, CFSR, HFSR, DFSR, MMFAR, BFAR, AFSR = range(7)

# Each entry: bit, name, and what it means in the terms someone debugging
# actually wants -- not a restatement of the acronym.
MEMMANAGE = [
    (0, "IACCVIOL", "an instruction fetch from a region the MPU forbids"),
    (1, "DACCVIOL", "a data access the MPU forbids"),
    (3, "MUNSTKERR", "a fault unstacking on exception return"),
    (4, "MSTKERR", "a fault stacking on exception entry"),
    (5, "MLSPERR", "a fault during lazy floating-point stacking"),
]

BUS = [
    (0, "IBUSERR", "an instruction fetch the bus refused"),
    (1, "PRECISERR", "a data access the bus refused; BFAR holds the address"),
    (2, "IMPRECISERR", "a data access the bus refused, reported late, so the "
                       "address and the instruction are both unknown"),
    (3, "UNSTKERR", "a bus fault unstacking on exception return"),
    (4, "STKERR", "a bus fault stacking on exception entry"),
    (5, "LSPERR", "a bus fault during lazy floating-point stacking"),
]

USAGE = [
    (0, "UNDEFINSTR", "an undefined instruction"),
    (1, "INVSTATE", "an attempt to execute in the wrong instruction set, "
                    "usually a branch to an address with bit 0 clear"),
    (2, "INVPC", "an illegal exception return value"),
    (3, "NOCP", "a coprocessor instruction with that coprocessor disabled, "
                "which on this part means floating point before CPACR is set"),
    (8, "UNALIGNED", "an unaligned access"),
    (9, "DIVBYZERO", "a divide by zero"),
]

HARD = [
    (1, "VECTTBL", "a fault reading the vector table"),
    (30, "FORCED", "a fault that escalated because its own handler was "
                   "disabled or it happened at too high a priority"),
    (31, "DEBUGEVT", "a debug event"),
]


@dataclass
class Session:
    registers: dict[str, int]
    scb: list[int]


# OpenOCD prints a register two ways: "(15) pc (/32): 0x..." when dumping them
# all, and "pc (/32): 0x..." when asked for one by name. It also spells the
# program status register xpsr here and xPSR elsewhere. Accept all of it --
# matching only one form would silently find no registers and report nothing.
REGISTER = re.compile(r"^(?:\(\d+\)\s+)?(\S+)\s+\(/\d+\):\s+(0x[0-9a-fA-F]+)",
                      re.MULTILINE)


def parse(text: str) -> Session:
    """Pulls OpenOCD's `reg` and `mdw` output out of a session log."""
    registers = {
        m.group(1).lower(): int(m.group(2), 16)
        for m in REGISTER.finditer(text)
    }

    scb: list[int] = []
    for line in text.splitlines():
        m = re.match(r"^(0x[0-9a-fA-F]+):((?:\s+[0-9a-fA-F]{8})+)", line.strip())
        if m and int(m.group(1), 16) == SCB_BASE:
            scb = [int(w, 16) for w in m.group(2).split()]
    return Session(registers, scb)


def flags(value: int, table: list[tuple[int, str, str]]) -> list[str]:
    return [f"{name}: {meaning}" for bit, name, meaning in table if value >> bit & 1]


def main() -> int:
    session = parse(sys.stdin.read())

    if len(session.scb) < 7:
        print("No fault registers in that output. Was the part halted?",
              file=sys.stderr)
        return 1

    cfsr = session.scb[CFSR]
    mem, bus, usage = cfsr & 0xFF, cfsr >> 8 & 0xFF, cfsr >> 16 & 0xFFFF

    print("core")
    for name in ("pc", "lr", "sp", "msp", "psp", "xpsr"):
        if name in session.registers:
            print(f"  {name:<5} 0x{session.registers[name]:08x}")

    # The exception number the core thinks it is in. 0 is thread mode; 3 is
    # HardFault, and anything non-zero means a handler is running, which on a
    # keyboard that should be idling is itself the finding.
    if "xpsr" in session.registers:
        exception = session.registers["xpsr"] & 0x1FF
        where = {0: "thread mode, no exception active",
                 2: "NMI", 3: "HardFault", 4: "MemManage",
                 5: "BusFault", 6: "UsageFault"}.get(exception,
                                                     f"exception {exception}")
        print(f"  mode  {where}")

    print(f"\nCFSR 0x{cfsr:08x}  HFSR 0x{session.scb[HFSR]:08x}")

    found = False
    for label, value, table, address in (
        ("MemManage", mem, MEMMANAGE, session.scb[MMFAR] if mem & 0x80 else None),
        ("BusFault", bus, BUS, session.scb[BFAR] if bus & 0x80 else None),
        ("UsageFault", usage, USAGE, None),
        ("HardFault", session.scb[HFSR], HARD, None),
    ):
        for line in flags(value, table):
            print(f"  {label}: {line}")
            found = True
        if address is not None:
            print(f"  {label}: the address it touched was 0x{address:08x}")

    if not found:
        print("  no fault bits set: nothing has faulted since the last reset")

    return 0


if __name__ == "__main__":
    sys.exit(main())
