#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Decodes the replies the C side builds with the tool's own code, and prints the log.

The C side and the tool share the enums through the headers, but the reply layouts are written
once in each language. This is what holds the two layouts together.

    fault_log_test --replies | check_replies.py <path to tools/k100-fault-log.py>
"""

from __future__ import annotations

import contextlib
import importlib.util
import io
import sys
from pathlib import Path


def load_tool(path: Path):
    spec = importlib.util.spec_from_file_location("k100_fault_log", path)
    tool = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = tool
    spec.loader.exec_module(tool)
    return tool


def main() -> int:
    tool = load_tool(Path(sys.argv[1]))
    event_value = tool.via_value(tool.K100_HEADER, "K100_VIA_LOG_EVENT")
    crash_value = tool.via_value(tool.K100_HEADER, "K100_VIA_LOG_CRASH")

    replies: dict[tuple[int, int], bytes] = {}
    for line in sys.stdin.read().splitlines():
        what, code, *rest = line.split(" ")
        if what == "label":
            kind = {"event": tool.EventKind, "crash": tool.CrashCause}[code]
            name = tool.text_of(kind(int(rest[0])))
            text = " ".join(rest[1:])
            assert text.startswith(name + " "), f"label {text!r} is not named {name!r}"
            continue
        value = {"event": event_value, "crash": crash_value}[what]
        reply = bytearray.fromhex(rest[0])
        reply[1], reply[2] = tool.K100_CHANNEL, value
        replies[(value, int(code))] = bytes(reply)

    def ask(node, request):
        return replies[(request[2], request[3])]

    tool.ask = ask

    events = tool.read_events("node")
    crashes = tool.read_crashes("node")

    # The boot, all eight kinds, and the reset after the crashes, newest first.
    assert [event.kind for event in events[:2]] == [tool.EventKind.usb_reset,
                                                    tool.EventKind.bootloader_jump]
    assert len(events) == 10 and events[-1].kind == tool.EventKind.boot
    assert events[0].time_ms == 9000 and events[1].time_ms == 8000
    assert events[0].dropped == 0
    assert events[0].sequence == events[1].sequence + 1

    assert [crash.cause for crash in crashes] == [
        tool.CrashCause.stall, tool.CrashCause.usage, tool.CrashCause.bus, tool.CrashCause.mem,
        tool.CrashCause.hard]
    stall = crashes[0]
    assert stall.pc == 0x114D8 + 4 and stall.lr == stall.pc + 4
    assert stall.psr == 0x61000000 and stall.basepri == 0x60
    assert stall.cfsr == 0x10000 and stall.fault_address == 7 and stall.time_ms == 8500
    assert stall.after_sequence == events[1].sequence

    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        tool.print_log(events, crashes)
    printed = output.getvalue().splitlines()
    jump = next(at for at, line in enumerate(printed) if "bootloader jump" in line)
    assert "STALL" in printed[jump + 5] and "kernel locked" in printed[jump + 5], printed
    assert "usb reset" in printed[jump + 6], printed

    # The empty position and the end of each ring are told apart.
    assert replies[(event_value, 10)][4] == tool.ReplyStatus.empty.value
    assert replies[(event_value, 240)][4] == tool.ReplyStatus.past_end.value
    assert replies[(crash_value, 8)][4] == tool.ReplyStatus.past_end.value

    # A code these headers do not have is shown as such.
    unknown = bytearray(replies[(event_value, 0)])
    unknown[4] = 30
    assert tool.text_of(tool.Event.parse(bytes(unknown)).kind) == "unknown (30)"

    print("ok: the tool decodes what the firmware builds, on every field and enum")
    return 0


if __name__ == "__main__":
    sys.exit(main())
