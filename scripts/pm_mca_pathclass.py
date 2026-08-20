#!/usr/bin/env python3
"""P55C path-class ruler for the dominant opaque FTB world lane.

Baseline CPIs and class frequencies were frozen from the validated 8e2d8e3
binary before SEG-14. A candidate kernel exposes four global labels delimiting
its real loop fragments; only those paint CPIs are replaced. Covered runs and
block setup keep their frozen C prices, so unrelated code layout cannot move
the ruler.
"""
import os
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

# Frozen deterministic 144-frame combat frequencies.
PAINT = 11250273 / 144
ZREJ = 932836 / 144
LMOVE = 1258210 / 144
BLOCKS = 475409 / 144

# Frozen 8e2d8e3 compiler CPIs, measured by the SEG-14 ruler.
BASE_CPI = {
    "paint_nomove": 102,
    "paint_lmove": 118,
    "zreject_px": 27,
    "skip_run": 95,
    "block_prologue": 387,
    "block_advance": 28,
}


def symbol_instructions(name: str):
    return [ins for ins in funcs[name] if ins.mne not in ("ret", "retf")]


def analyze_fragments(parts) -> tuple[int, int]:
    instrs = [ins for part in parts for ins in part if ins.mne not in ("jmp", "call", "ret", "retf")]
    tail = pm_mca.parse_asm("jne loop_head\nloop_head:")[:1]
    result = pm_mca.analyze_slice(instrs + tail, pm_mca.Ctx(), iterations=12, inner_jcc="nottaken")
    stalls = {key: round(value, 1) for key, value in result["stats"]["stalls"].items()}
    print(f"  candidate n={len(instrs) + 1} stalls={stalls}")
    return result["cycles_per_iter"], len(instrs) + 1


def emit(name: str, cpi: int, trips: float, n: int | None = None) -> float:
    cycles = cpi * trips
    shape = f"n={n:3d} " if n is not None else "frozen    "
    print(f"{name:18s} {shape}cpi={cpi:3d} trips/frame={trips:9,.0f} cycles/frame={cycles:12,.0f}")
    return cycles


symbols = {
    name: f"_sw_world_kernel_{name}"
    for name in ("px_prefix", "light_reload", "shade_store", "advance")
}
using_kernel = all(symbol in funcs for symbol in symbols.values())
paint_nomove_cpi = BASE_CPI["paint_nomove"]
paint_lmove_cpi = BASE_CPI["paint_lmove"]
paint_nomove_n = None
paint_lmove_n = None

if using_kernel:
    part = {name: symbol_instructions(symbol) for name, symbol in symbols.items()}
    paint_nomove_cpi, paint_nomove_n = analyze_fragments(
        [part["px_prefix"], part["shade_store"], part["advance"]]
    )
    paint_lmove_cpi, paint_lmove_n = analyze_fragments(
        [part["px_prefix"], part["light_reload"], part["shade_store"], part["advance"]]
    )
    print("PMCA-WORLD source=assembly-kernel+frozen-C-remainder")
else:
    print("PMCA-WORLD source=frozen-8e2d8e3-compiler")

total = 0.0
total += emit("paint_nomove", paint_nomove_cpi, PAINT - LMOVE, paint_nomove_n)
total += emit("paint_lmove", paint_lmove_cpi, LMOVE, paint_lmove_n)
total += emit("zreject_px", BASE_CPI["zreject_px"], ZREJ)
total += emit("skip_run", BASE_CPI["skip_run"], ZREJ)
total += emit("block_prologue", BASE_CPI["block_prologue"], BLOCKS)
total += emit("block_advance", BASE_CPI["block_advance"], BLOCKS)
print(f"{'TOTAL world lane':18s} cycles/frame={total:12,.0f} ~= {total / 233e6 * 1e6:,.0f} us @PMMX-233")
print(f"METRIC pmca_world_cycles={int(round(total))}")
print(f"METRIC pmca_world_est_us={int(round(total / 233e6 * 1e6))}")
