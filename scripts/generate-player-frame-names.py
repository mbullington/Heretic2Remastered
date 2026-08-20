#!/usr/bin/env python3

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/game/m_player.h"
OUTPUT = ROOT / "dos/ref_soft/ref_soft_player_frames.inc"
FRAME_PATTERN = re.compile(r"#define FRAME_(\S+)\s+(\d+)\s*$")


def generate() -> str:
    frames: list[tuple[int, str]] = []
    for line in SOURCE.read_text().splitlines():
        match = FRAME_PATTERN.match(line)
        if match:
            frames.append((int(match.group(2)), match.group(1)))

    for expected, (index, name) in enumerate(frames):
        if index != expected:
            raise ValueError(f"non-contiguous player frame index {index}, expected {expected}")
        if len(name.encode("ascii")) >= 16:
            raise ValueError(f"player frame name does not fit the FM name field: {name}")

    folded = [name.casefold() for _, name in frames]
    if len(set(folded)) != len(folded):
        raise ValueError("player frame names are not unique when case-folded")

    lines = [f'\t"{name}",' for _, name in frames]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    content = generate()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != content:
            print(f"out of date: {OUTPUT.relative_to(ROOT)}", file=sys.stderr)
            return 1
        return 0

    OUTPUT.write_text(content)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
