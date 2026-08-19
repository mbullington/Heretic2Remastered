#!/usr/bin/env python3
"""Global loop census for pm-mca: dumps stats + DB gap report.

Usage: python3 scripts/pm_mca_scan.py [dump-path]
"""
import collections
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pm_mca import Ctx, analyze_slice, bind, parse_objdump_symbols  # noqa: E402


def main() -> int:
    dump = sys.argv[1] if len(sys.argv) > 1 else ".dos-build/pmmca/silver.dump"
    text = Path(dump).read_text(errors="replace")
    funcs = parse_objdump_symbols(text)
    print("functions:", len(funcs))
    rows = []
    skipped = {"ctl": 0, "unknown": 0, "big": 0}
    unknown_mnes = collections.Counter()
    for sym, instrs in funcs.items():
        for i, ins in enumerate(instrs):
            if not ins.mne.startswith("j") or ins.mne == "jmp":
                continue
            if not ins.ops or ins.ops[0].kind != "imm":
                continue
            mt = re.match(r"^(0x[0-9a-f]+|[0-9a-f]+)", ins.ops[0].text.strip())
            if not mt:
                continue
            target = int(mt.group(1), 16)
            if ins.addr is None or target >= ins.addr:
                continue
            start = next((j for j, x in enumerate(instrs) if x.addr == target), None)
            if start is None or not (2 <= i - start + 1 <= 240):
                skipped["big"] += 1
                continue
            body = instrs[start:i + 1]
            if any(x.mne in ("jmp", "call", "ret", "loop", "int", "leave", "iret")
                   for x in body[:-1]):
                skipped["ctl"] += 1
                continue
            try:
                res = analyze_slice(body, Ctx(), iterations=8)
            except (KeyError, ValueError):
                skipped["unknown"] += 1
                for x in body:
                    try:
                        bind(x)
                    except (KeyError, AttributeError):
                        unknown_mnes[x.mne] += 1
                continue
            st = res["stats"]
            rows.append((sym, body[0].addr, len(body), res["cycles_per_iter"],
                         round(st["memops_per_iter"], 1), round(st["bytes_rw_per_iter"], 0),
                         round(st["stalls"].get("agi", 0) / 8, 1)))
    print("PASS loops:", len(rows), "skipped:", skipped)
    print("unknown mnemonics:", unknown_mnes.most_common(25))
    print("\nrenderer-relevant clean loops:")
    keys = ("raster", "flex", "sky", "framebuffer", "modulate", "draw", "span",
            "present", "particle", "light", "sprite", "world")
    interesting = [r for r in rows if any(k in r[0] for k in keys)]
    for r in sorted(interesting, key=lambda r: -r[3])[:40]:
        print(f"  {r[0]:44s} @{r[1]:06x} n={r[2]:3d} cpi={r[3]:4d} mem/it={r[4]} bytes/it={r[5]} agi/it={r[6]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
