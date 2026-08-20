#!/usr/bin/env python3
# ideas#D: path-classified per-px pricing for the FTB pow2 world lane.
# Cuts dominant-path instruction windows out of the draw_face objdump
# (bytes EXACT, unlike --asm estimates), appends a synthetic backward jne,
# and scores each class with pm_mca's Sim with inner jccs priced as the
# P55C steady state (1clk, predicted correctly; class branches mostly-taken
# or mostly-not-taken by construction).
import os
import re
import sys

sys.path.insert(0, "scripts")
import pm_mca  # noqa: E402

# regenerate the function dump when stale (script works on _draw_face;
# draw_face_spans is inlined there)
DUMP = "/tmp/drawface.asm"
if not os.path.exists(DUMP) or os.path.getmtime(DUMP) < os.path.getmtime(".dos-build/guest/SILVER.EXE"):
    import subprocess
    text = subprocess.run([pm_mca.find_djgpp_objdump()[0], "-d", "-Mintel", "-j", ".text",
                           ".dos-build/guest/SILVER.EXE"],
                          check=True, capture_output=True, text=True).stdout
    cur = None
    with open(DUMP, "w") as f:
        for l in text.splitlines(True):
            m = re.match(r"^([0-9a-f]+) <([^ >]+)>,?:", l) or re.match(r"^([0-9a-f]+) <([^ >]+)>:", l)
            if m:
                cur = m.group(2)
            if cur == "_draw_face":
                f.write(l)


def cut(a0: str, a1: str, drop: tuple = ()) -> list[str]:
    """Lines in [a0..a1] (hex addrs), minus branch-target holes per drop."""
    out = []
    lo, hi = int(a0, 16), int(a1, 16)
    for line in open(DUMP):
        m = re.match(r"^\s*([0-9a-f]+):", line)
        if not m:
            continue
        a = int(m.group(1), 16)
        if lo <= a <= hi:
            keep = not any(lo2 <= a <= hi2 for lo2, hi2 in drop)
            if keep:
                out.append(line.rstrip())
    if not out:
        raise SystemExit(f"empty window {a0}..{a1}")
    return out


def score(name: str, lines: list[str], trips: float, inner_jcc: str = "nottaken"):
    instrs = pm_mca.parse_objdump("\n".join(lines))
    # synthetic counted loop: paint-advance loops here are all direct jmps or
    # structured edges; Sim needs a final conditional. Append jne (row-priced).
    asm_tail = pm_mca.parse_asm("jne loop_head\nloop_head:")
    body = instrs + asm_tail[:1]  # jne only; the loop_head label emits nothing
    res = pm_mca.analyze_slice(body, pm_mca.Ctx(), iterations=12, inner_jcc=inner_jcc)
    cpi = res["cycles_per_iter"]
    n = len(body)
    stalls = res["stats"]["stalls"]
    print(f"{name:16s} n={n:3d} cpi={cpi:3d} trips/frame={trips:9,.0f} cycles/frame={cpi*trips:12,.0f}"
          f"  stalls={ {k: round(v,1) for k,v in stalls.items()} }")
    return cpi * trips


# ---------------- per-frame class frequencies (combat, 144-frame run) -------
# from COMBAT-EVIDENCE.TXT (post-E19c tree):
PAINT = 11250273 / 144      # bench-world-zaccepted
ZREJ = 932836 / 144         # bench-world-zreject (mixed-cover px; skipruns... )
LMOVE = 1258210 / 144       # light-cell crossings (census #92; paint classes only)

CLASSES = []
# A: painted px, no light move, no alpha-test, stripe off, tagxy off.
#   bit-make/test -> stripe-test -> texel -> alpha-je(nottaken) -> lidx
#   (cmp == prev: je TAKEN over the reload) -> modulate3 -> mag/tag -> stores
lines = (cut("1ceed", "1cf0b") + cut("1cf0d", "1cf45") + cut("1cf49", "1cf49") +
         cut("1cf54", "1cf7a") + cut("1cfb7", "1d085") + cut("1cea4", "1cee7"))
CLASSES.append(("paint_nomove", lines, PAINT - LMOVE))
# B: painted px with light-cell move: same + reload window + lidx-je nottaken
lines = (cut("1ceed", "1cf0b") + cut("1cf0d", "1cf45") + cut("1cf49", "1cf49") +
         cut("1cf54", "1cf7a") + cut("1cf7c", "1cfb1") + cut("1cfb7", "1d085") + cut("1cea4", "1cee7"))
CLASSES.append(("paint_lmove", lines, LMOVE))
# C: covered px inside a mixed run: bit-test taken (1clk) -> counter ++ -> advance
lines = cut("1ceed", "1cf02") + ["\t\t\tjne 0\t; taken to skip"] + cut("1ce9e", "1ce9e") + cut("1cea4", "1cee7")
CLASSES.append(("zreject_px", lines, ZREJ))
# D: fully-covered run skip (per ~<=32px run): runmake + covered-je taken + run-advance
lines = cut("1cdfd", "1ce2e") + ["\t\t\tje 0\t; taken to skiprun"] + cut("1d08b", "1d0f9")
CLASSES.append(("skip_run", lines, ZREJ))  # zreject counts cover these px too; per-RUN pricing noted

tot = 0.0
for name, lines, trips in CLASSES:
    tot += score(name, lines, trips)
print(f"{'TOTAL ftb-pow2 lane':16s} {'':33s} cycles/frame={tot:12,.0f} ~= {tot/233e6*1e6:,.0f} us @PMMX-233")

# ---------------- VARIANT: #22 shade-row LUT (mul5/mul6 hot rows) -----------
# replaces the E19c modulate3 window (1cfb7..1d02d) with 3 table loads +
# shifts; length-correct synthetic bytes, dominant (no-clamp) path.
def synth(lines_):
    out = []
    a = 0x900000
    for text, nbytes in lines_:
        out.append(f"   {a:06x}:\t{"90 " * nbytes}\t{text}")
        a += nbytes
    return out

LUT_MODULATE = synth([
    ("mov    eax,edx", 2),          # texel copy
    ("shr    ax,0xb", 4),           # r5
    ("movzx  ecx,BYTE PTR [edi+eax]", 4),  # mul5 row for lr
    ("mov    eax,edx", 2),
    ("shr    ax,0x5", 4),           # g6
    ("and    eax,0x3f", 3),
    ("movzx  eax,BYTE PTR [esi+eax]", 4),  # mul6 row for lg
    ("shl    eax,0x5", 3),
    ("or     ecx,eax", 2),
    ("and    edx,0x1f", 2),         # b5
    ("movzx  edx,BYTE PTR [ebx+edx]", 4),  # mul5 row for lb
    ("or     ecx,edx", 2),          # out in ecx
    ("mov    edi,ecx", 2),          # da of downstream 'di' writer
])
LUT_RELOAD_EXTRA = synth([
    ("lea    ebx,[edi+eax*1]", 3),  # mul5 b-row = base + lb*32 (fold: lb in reg)
    ("lea    esi,[eax+ecx*8]", 3),  # mul6 g-row = base + lg*64
    ("lea    edi,[ecx+edx*4]", 3),  # mul5 r-row = base + lr*32
])
var_lines = (cut("1ceed", "1cf0b") + cut("1cf0d", "1cf45") + cut("1cf49", "1cf49") +
             cut("1cf54", "1cf7a") + LUT_MODULATE + cut("1d02f", "1d085") + cut("1cea4", "1cee7"))
score("VAR paint_nomove_LUT", var_lines, PAINT - LMOVE)
var_lines_m = (cut("1ceed", "1cf0b") + cut("1cf0d", "1cf45") + cut("1cf49", "1cf49") +
               cut("1cf54", "1cf7a") + cut("1cf7c", "1cfb1") + LUT_RELOAD_EXTRA + LUT_MODULATE +
               cut("1d02f", "1d085") + cut("1cea4", "1cee7"))
score("VAR paint_lmove_LUT", var_lines_m, LMOVE)

# ---------------- class C: world block prologue + advance (#13/#21a pool) ---
BLOCKS = 475409 / 144
score("block_prologue", cut("1cb0c", "1cdfb"), BLOCKS)
score("block_advance", cut("1d10d", "1d15d"), BLOCKS)

# ---------------- #21(a) prologue conversion-tax VARIANTS --------------------
# window zones (from the 0x1cb0c..0x1cdfb slice of the NEW build):
#   1cb47..1cb63   w fild/store + endpoint evals
#   1cb63..1cb7c   fld1 + fdiv (iz0) + max-test + fdivr (iz1)
#   1cb82..1cb98   fnstcw save + CW build (mov/or/mov)
#   1cbe7/1cbf3    fldcw chop / fldcw restore  (cast: iu)
#   1cc09/1cc0f/1cc15  dance (cast: it)         1cc3e/44/4a (cast: iu_step)
#   1cc6c/72/78    dance (cast: it_step)
#   1cd0e..1cd20   fmul + dance + fistp QWORD (ikey)
#   1cd37..1cd4f   fmul + dance + fistp QWORD (ikey_end)
#   1cd62          idiv (ikey_step)
def drop_between(lines, *ranges):
    return [l for l in lines
            if not any(int(a, 16) <= int(l.strip().split(":")[0], 16) <= int(b, 16)
                       for a, b in ranges)]

# V1 bare conversions: remove BOTH fldcw-dance classes (CW-build block +
# per-cast chop/restore pairs). Semantics NOTE: bare fistp = ambient RN mode,
# chop != trunc for NEGATIVE values and at .5 edges => DRIFT CLASS, not
# keep-class, for iu/it/steps (they can go negative pre-clamp); ikey is
# nonneg => the QWORD sites only differ at exact .5 (ultra-thin drift).
base = cut("1cb0c", "1cdfb")
v1 = drop_between(base, ("1cb82", "1cb98"))  # fnstcw + CW build
for chop, fistp, restore in (("1cbe7", None, "1cbf3"), ("1cc09", None, "1cc15"),
                              ("1cc3e", None, "1cc4a"), ("1cc6c", None, "1cc78"),
                              ("1cd14", None, "1cd20"), ("1cd3d", None, "1cd4f")):
    v1 = drop_between(v1, (chop, chop), (restore, restore))
score("VAR prologue_bareFISTP", v1, BLOCKS)

# V2 = V1 + the ikey QWORD casts folded to DWORD (value domain PROVABLY
# <2^31 by construction ('31-bit keys'); fistp qword 6clk -> dword 3clk-ish)
# synthesized: swap the two QWORD fistp lines for DWORD ones (same len class).
v2 = [l.replace("fistp  QWORD PTR", "fistp  DWORD PTR") for l in v1]
assert not any("QWORD" in l for l in v2)
score("VAR prologue_V1+ikeyDWORD", v2, BLOCKS)

# V3 = V2 + #13-style FDIV -> seed LUT + NR on both reciprocals (iz0, iz1):
# each fdiv* -> [mov index+and] + [movzx load, 4] + [fmul seed, 2] + [fmul sub, 2]
# hand-synthesized at plausible sizes; the surrounding fxch/test lines stay.
synth = lambda items: [f"   {a:06x}:\t{'90 ' * n}\t{t}" for a, (t, n) in
                       zip(range(0xA00000, 0xA00000 + 0x80, 8), items)]
v3 = drop_between(v2, ("1cb63", "1cb63"), ("1cb65", "1cb65"), ("1cb7c", "1cb7c"))
v3 += synth([("movzx  eax,BYTE PTR [esi+0x1234]", 4), ("fmul   DWORD PTR [esp]", 3),
             ("fmul   st,st", 2), ("movzx  eax,BYTE PTR [esi+0x1235]", 4),
             ("fmul   DWORD PTR [esp]", 3), ("fmul   st,st", 2)])
score("VAR prologue_V3+recipLUT", v3, BLOCKS)

# ---------------- texel-row variant (census-hot: 29% crossing) ---------------
# replaces 1cf0d..1cf45's sar/and/imul*width with a retained row pointer +
# crossing recompute priced at the MEASURED 29%-per-px crossing rate: model
# as NOP form (no imul) + recompute block every ~3.4th px => amortized.
recomp = synth([("mov    edi,DWORD PTR [esp]", 3), ("imul   edi,DWORD PTR [esp]", 6),
                ("lea    ebx,[edi+eax*4]", 3)])
texrow = (cut("1ceed", "1cf0b") + [l for l in cut("1cf0d", "1cf45")
                                    if "imul" not in l and "sar" not in l and "and" not in l] +
          cut("1cf49", "1cf49") + cut("1cf54", "1cf7a") + cut("1cfb7", "1d085") + cut("1cea4", "1cee7"))
score("VAR paint_texrow-imul-defer", texrow + recomp, PAINT - LMOVE)
