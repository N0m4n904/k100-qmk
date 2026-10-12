#!/usr/bin/env python3
"""Tests a candidate Bragi-index to LedInfoMap-index mapping against geometry.

The firmware's `LedInfoMap` says which driver and channels light LED *n*, and
ckb-next's `keymap_bragi` says which key Bragi index *b* is. Nothing found so far
says how *n* and *b* relate, and the firmware contains no translation table: the
190 `LedInfoMap` entries are a compacted space, while `LedPresenceFlag` is 193
wide. Any mapping between them is therefore a hypothesis.

A wrong one is not loud. It lights the wrong key, and every key still lights, so
it looks like it works. This exists so a candidate can be falsified cheaply
before it reaches a generated table.

The test: the three drivers are physically placed on one PCB, and a channel
advances by 18 per switch row, so a correct mapping should put each driver's
channels in a *region* of the keyboard rather than scattered across it. A
mapping under which every driver spans the whole board is wrong, or the wiring
is stranger than any keyboard's has reason to be.

    tools/check-led-mapping.py firmware.bin --ckb-keymap keymap.c

`keymap.c` is ckb-next's `src/daemon/keymap.c`.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from k100_firmware import Firmware  # noqa: E402

LED_PRESENCE_FLAG = 0x3214A - 0x10000
LED_PRESENCE_COUNT = 193

# Channels advance by this many per switch row on an IS31FL3743B's 18 x 11.
CHANNELS_PER_ROW = 18


def bragi_names(keymap_c: Path) -> dict[int, str]:
    """Bragi LED index to key name, from ckb-next's keymap_bragi."""
    source = keymap_c.read_text()
    start = source.index("const key keymap_bragi[")
    body = source[source.index("{", start) + 1 : source.index("\n};", start)]
    entry = re.compile(r'\{\s*(?:"([^"]*)"|0)\s*,\s*(-?\w+)\s*,\s*([A-Za-z0-9_|\s]+?)\s*\}')
    return {index: m.group(1) for index, m in enumerate(entry.finditer(body)) if m.group(1)}


def rank_mapping(firmware: Firmware, image: bytes) -> tuple[list[int], list[int]]:
    """The candidate: the Nth present Bragi LED is the Nth RGB entry of LedInfoMap.

    Returns the present Bragi indices and the LedInfoMap index each maps to.
    """
    presence = image[LED_PRESENCE_FLAG : LED_PRESENCE_FLAG + LED_PRESENCE_COUNT]
    present = [i for i, value in enumerate(presence) if value != 0]
    rgb_slots = [led.index for led in firmware.leds() if led.is_rgb]
    return present, rgb_slots


def correlation(xs: list[float], ys: list[float]) -> float:
    try:
        return statistics.correlation(xs, ys)
    except (statistics.StatisticsError, ValueError):
        return float("nan")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("image", type=Path, help="an official firmware image")
    parser.add_argument(
        "--ckb-keymap", type=Path, required=True, help="ckb-next's src/daemon/keymap.c"
    )
    parser.add_argument(
        "--geometry",
        type=Path,
        default=Path("tools/extract-ckb-keymap/k100-de.json"),
        help="geometry from extract-ckb-keymap",
    )
    args = parser.parse_args(argv[1:])

    image = args.image.read_bytes()
    firmware = Firmware(image)
    leds = firmware.leds()

    present, rgb_slots = rank_mapping(firmware, image)
    if len(present) != len(rgb_slots):
        print(
            f"{len(present)} present Bragi LEDs against {len(rgb_slots)} RGB entries: "
            "the candidate mapping cannot even be formed",
            file=sys.stderr,
        )
        return 1

    names = bragi_names(args.ckb_keymap)
    geometry = {k["name"]: k for k in json.loads(args.geometry.read_text())["keys"]}

    joined = []
    for rank, bragi_index in enumerate(present):
        name = names.get(bragi_index)
        if name is None or name not in geometry:
            continue
        chip, channel = leds[rgb_slots[rank]].channels[0]
        joined.append((chip, channel, geometry[name]["x"], geometry[name]["y"]))

    print(f"{len(present)} present Bragi LEDs, {len(joined)} of them with a position\n")

    board_width = max(x for _, _, x, _ in joined) - min(x for _, _, x, _ in joined)

    print("  driver  LEDs   x span   as a fraction of the board")
    spans = {}
    for chip in sorted({c for c, *_ in joined}):
        xs = [x for c, _, x, _ in joined if c == chip]
        span = (max(xs) - min(xs)) / board_width
        spans[chip] = span
        print(f"  {chip:6}  {len(xs):4}   {max(xs) - min(xs):6.0f}   {span:.2f}")

    print("\n  driver  corr(channel row, y)  corr(channel column, x)")
    for chip in sorted({c for c, *_ in joined}):
        sel = [(ch, x, y) for c, ch, x, y in joined if c == chip]
        row = correlation([c // CHANNELS_PER_ROW for c, _, _ in sel], [y for _, _, y in sel])
        col = correlation([c % CHANNELS_PER_ROW for c, _, _ in sel], [x for _, x, _ in sel])
        print(f"  {chip:6}  {row:+.3f}                {col:+.3f}")

    # A driver covering most of the board is the signature of a wrong mapping:
    # three drivers sharing one PCB have no reason to interleave that finely.
    worst = max(spans.values())
    print()
    if worst > 0.8:
        print(
            f"REJECTED: every driver spans up to {worst:.0%} of the board, so this "
            "mapping does not place a driver's channels anywhere near each other."
        )
        return 1

    print(f"not rejected: the widest driver spans {worst:.0%} of the board.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
