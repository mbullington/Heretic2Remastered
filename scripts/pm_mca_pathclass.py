#!/usr/bin/env python3
"""P55C path-class ruler for the dominant opaque FTB world lane.

The frozen baseline uses exact instruction windows from source commit 8e2d8e3.
A candidate assembly kernel exposes global labels for the same path fragments;
when those labels exist this script prices those real bytes instead. Frequencies
come from the deterministic 144-frame combat capture and stay frozen.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, "scripts")
import pm_mca  # noqa: E402

EXE = ".dos-build/guest/SILVER.EXE"
DUMP = "/tmp/h2-pmmx-world.dump"
if not os.path.exists(DUMP) or os.path.getmtime(DUMP) < os.path.getmtime(EXE):
    text = subprocess.run(
        pm_mca.find_djgpp_objdump() + ["-d", "-Mintel", "-j", ".text", EXE],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    with open(DUMP, "w") as out:
        out.write(text)
else:
    with open(DUMP) as source:
        text = source.read()

funcs = pm_mca.parse_objdump_symbols(text)


def cut(a0: str, a1: str) -> list[str]:
    lo, hi = int(a0, 16), int(a1, 16)
    lines = []
    for line in text.splitlines():
        match = re.match(r"^\s*([0-9a-f]+):", line)
        if match and lo <= int(match.group(1), 16) <= hi:
            lines.append(line)
    if not lines:
        raise SystemExit(f"empty frozen window {a0}..{a1}; baseline code moved")
    return lines


def raw_symbol(name: str) -> list[str]:
    if name not in funcs:
        raise KeyError(name)
    return [ins.raw for ins in funcs[name] if ins.mne not in ("ret", "retf")]


def score(name: str, lines: list[str], trips: float) -> float:
    instrs = [
        ins for ins in pm_mca.parse_objdump("\n".join(lines))
        if ins.mne not in ("jmp", "call", "ret", "retf")
    ]
    tail = pm_mca.parse_asm("jne loop_head\nloop_head:")[:1]
    result = pm_mca.analyze_slice(instrs + tail, pm_mca.Ctx(), iterations=12, inner_jcc="nottaken")
    cpi = result["cycles_per_iter"]
    stalls = {key: round(value, 1) for key, value in result["stats"]["stalls"].items()}
    cycles = cpi * trips
    print(
        f"{name:18s} n={len(instrs) + 1:3d} cpi={cpi:3d} "
        f"trips/frame={trips:9,.0f} cycles/frame={cycles:12,.0f} stalls={stalls}"
    )
    return cycles


# Frozen per-frame class frequencies from the 144-frame combat capture.
PAINT = 11250273 / 144
ZREJ = 932836 / 144
LMOVE = 1258210 / 144
BLOCKS = 475409 / 144

# Assembly candidates expose fragments of their real loop as global labels.
# Objdump assigns bytes through the next global label to each fragment.
fragments = {
    name: f"_sw_world_kernel_{name}"
    for name in ("px_prefix", "light_reload", "shade_store", "advance", "zreject", "skiprun")
}
using_kernel = all(symbol in funcs for symbol in fragments.values())

if using_kernel:
    part = {name: raw_symbol(symbol) for name, symbol in fragments.items()}
    paint_nomove = part["px_prefix"] + part["shade_store"] + part["advance"]
    paint_lmove = part["px_prefix"] + part["light_reload"] + part["shade_store"] + part["advance"]
    zreject = part["zreject"] + part["advance"]
    skiprun = part["skiprun"]
    print("PMCA-WORLD source=assembly-kernel")
else:
    # Exact compiler bytes for 8e2d8e3. The non-linear order mirrors GCC's
    # branch layout: advance is before the paint body and skip-run after it.
    paint_prefix = cut("1d237", "1d2c8")
    light_reload = cut("1d2ca", "1d312")
    shade_store = cut("1d318", "1d3c3")
    advance = cut("1d1ee", "1d22b")
    paint_nomove = paint_prefix + shade_store + advance
    paint_lmove = paint_prefix + light_reload + shade_store + advance
    zreject = cut("1d237", "1d24c") + cut("1d1e8", "1d22b")
    skiprun = cut("1d121", "1d178") + cut("1d3c8", "1d42d")
    print("PMCA-WORLD source=frozen-8e2d8e3-compiler")

# Prologue/advance remain in C for the first assembly-kernel experiment.
block_prologue = cut("1ce41", "1d09f")
block_advance = cut("1d44b", "1d498")

total = 0.0
total += score("paint_nomove", paint_nomove, PAINT - LMOVE)
total += score("paint_lmove", paint_lmove, LMOVE)
total += score("zreject_px", zreject, ZREJ)
total += score("skip_run", skiprun, ZREJ)
total += score("block_prologue", block_prologue, BLOCKS)
total += score("block_advance", block_advance, BLOCKS)
print(
    f"{'TOTAL world lane':18s} cycles/frame={total:12,.0f} "
    f"~= {total / 233e6 * 1e6:,.0f} us @PMMX-233"
)
print(f"METRIC pmca_world_cycles={int(round(total))}")
print(f"METRIC pmca_world_est_us={int(round(total / 233e6 * 1e6))}")
