"""Pentium MMX (P55C) instruction timing/pairing tables for pm_mca.

Sources (verbatim, from the author's current manuals which retain the P1/PMMX
retrospective chapters — both fetched 2026-08-19, sha-pinned in pm_mca.py):
  [IT] Agner Fog, "4. Instruction tables" (instruction_tables.pdf),
       chapter "Intel Pentium and Pentium MMX", pp. 177-180:
       integer clock cycles + pairability (u/v/uv/np); FPU clocks +
       FXCH-pairability + i-ov/fp-ov overlap columns; MMX summary:
       all MMX 1 clock except multiplies (3), pipelined 1/clock.
  [MA] Agner Fog, "3. The microarchitecture of Intel, AMD and VIA CPUs"
       (microarchitecture.pdf), chapter 4 "Intel Pentium 1 and Pentium MMX
       pipeline" (pp. 42-50): pairing rules 1-10, imperfect pairing + table
       4.1, AGI, prefix decode + 4-deep FIFO, FPU scheduling; chapter 3.3:
       PMMX BTB 256 entries (16 ways x 16 sets, allocate-on-first-jump),
       misprediction penalty jcc = 4 (U-pipe) / 5 (V-pipe), other control
       transfers = 4.

Encoding per row: (operand-class matcher, clocks, pairability, extra dict).
Operand class letters: r=reg32, r816=8/16-bit reg, m=memory, i=imm, accum=eax.
'clocks' is what [IT] lists: minimum P1/PMMX clocks assuming L1-hit aligned.
Extra keys:
  rm / rmw   : read-modify (2clk) / read-modify-write (3clk) timing class used
               by [MA] table 4.1 to compute pair occupancy.
  latency    : result-ready latency if it differs from pipe occupancy
               (loads/FPU/MMX-mul).
  i_ov/fp_ov : [IT] overlap windows for FP (last N clocks may overlap with
               integer / subsequent FP instructions).
  unit       : serializing resource ('fpu','fdiv','fmul','mmmul','mmshift').
  flags      : 'w' writes flags, 'r' reads flags ('rw' both); conditional
               jumps read flags.
  disp_imm   : true if form carries both displacement and immediate
               ([MA] rule 8: pairable in U-pipe only on PMMX).
  conf       : 'hi' = directly from [IT]; 'lo' = inferred/noted, verify.

Anything not listed here makes pm_mca refuse the slice (never guess a timing).
"""

# ---------------------------------------------------------------------------
# Integer instructions. clocks = [IT] column "Clock cycles", pair = u/v/uv/np.
# ---------------------------------------------------------------------------

INT = {
    "nop":     [({"*": True}, 1, "uv", {})],
    "mov":     [({"kind": "any"}, 1, "uv", {"latency": 1, "conf": "hi"})],
    "xchg":    [({"dst": "accum"}, 2, "np", {}), ({"kind": "rr"}, 3, "np", {}), ({"kind": "rm"}, 15, "np", {"conf": "lo", "note": ">15 per IT"})],
    "xlat":    [({"*": True}, 4, "np", {})],
    "push":    [({"kind": "mem"}, 2, "np", {}),
                ({"kind": "any"}, 1, "uv", {"stack": "push"})],
    "pop":     [({"kind": "mem"}, 3, "np", {}),
                ({"kind": "any"}, 1, "uv", {"stack": "pop"})],
    "pushf":   [({"*": True}, 4, "np", {"clocks_range": (3, 5)})],
    "popf":    [({"*": True}, 5, "np", {"clocks_range": (4, 6), "flags": "w"})],
    "lahf":    [({"*": True}, 2, "np", {})],
    "sahf":    [({"*": True}, 2, "np", {"flags": "w"})],
    "lea":     [({"*": True}, 1, "uv", {"agi_aware": True, "conf": "hi"})],
    "add":     [({"kind": "rm"}, 2, "uv", {"rm": True, "flags": "w"}),
                ({"kind": "mr"}, 3, "uv", {"rmw": True, "flags": "w"}),
                ({"kind": "any"}, 1, "uv", {"flags": "w"})],
    "adc":     [({"kind": "rm"}, 2, "u", {"rm": True, "flags": "rw"}),
                ({"kind": "mr"}, 3, "u", {"rmw": True, "flags": "rw"}),
                ({"kind": "any"}, 1, "u", {"flags": "rw"})],
    "sub":     [({"kind": "rm"}, 2, "uv", {"rm": True, "flags": "w"}),
                ({"kind": "mr"}, 3, "uv", {"rmw": True, "flags": "w"}),
                ({"kind": "any"}, 1, "uv", {"flags": "w"})],
    "sbb":     [({"kind": "rm"}, 2, "u", {"rm": True, "flags": "rw"}),
                ({"kind": "mr"}, 3, "u", {"rmw": True, "flags": "rw"}),
                ({"kind": "any"}, 1, "u", {"flags": "rw"})],
    "and":     [({"kind": "rm"}, 2, "uv", {"rm": True, "flags": "w"}),
                ({"kind": "mr"}, 3, "uv", {"rmw": True, "flags": "w"}),
                ({"kind": "any"}, 1, "uv", {"flags": "w"})],
    "or":      [({"kind": "rm"}, 2, "uv", {"rm": True, "flags": "w"}),
                ({"kind": "mr"}, 3, "uv", {"rmw": True, "flags": "w"}),
                ({"kind": "any"}, 1, "uv", {"flags": "w"})],
    "xor":     [({"kind": "rm"}, 2, "uv", {"rm": True, "flags": "w"}),
                ({"kind": "mr"}, 3, "uv", {"rmw": True, "flags": "w"}),
                ({"kind": "any"}, 1, "uv", {"flags": "w"})],
    "cmp":     [({"kind": "any"}, 1, "uv", {"flags": "w"})],   # cmp mem,imm is 2+U-only via disp_imm rule
    "test":    [({"kind": "mem_imm"}, 2, "np", {"flags": "w"}),
                ({"kind": "reg_imm_nonaccum"}, 1, "u", {"flags": "w", "conf": "hi", "note": "IT f): r,i pairs only if accum (UV) else U? IT lists TEST r,i 1 with note f"},
                 ),
                ({"kind": "any"}, 1, "uv", {"flags": "w"})],
    "inc":     [({"kind": "mem"}, 3, "uv", {"rmw": True, "flags": "w"}), ({"kind": "any"}, 1, "uv", {"flags": "w"})],
    "dec":     [({"kind": "mem"}, 3, "uv", {"rmw": True, "flags": "w"}), ({"kind": "any"}, 1, "uv", {"flags": "w"})],
    "neg":     [({"kind": "mem"}, 3, "np", {"flags": "w"}), ({"kind": "any"}, 1, "np", {"flags": "w"})],
    "not":     [({"kind": "mem"}, 3, "np", {}), ({"kind": "any"}, 1, "np", {})],
    "mul":     [({"size_max": 16}, 11, "np", {"unit": "imul"}), ({"*": True}, 9, "np", {"unit": "imul"})],
    "imul":    [({"size_max": 16}, 11, "np", {"unit": "imul"}), ({"*": True}, 9, "np", {"unit": "imul"})],
    "div":     [({"size": 8}, 17, "np", {"unit": "idiv"}), ({"size": 16}, 25, "np", {"unit": "idiv"}), ({"*": True}, 41, "np", {"unit": "idiv"})],
    "idiv":    [({"size": 8}, 22, "np", {"unit": "idiv"}), ({"size": 16}, 30, "np", {"unit": "idiv"}), ({"*": True}, 46, "np", {"unit": "idiv"})],
    "cbw":     [({"*": True}, 3, "np", {})],
    "cwde":    [({"*": True}, 3, "np", {})],
    "cwd":     [({"*": True}, 2, "np", {})],
    "cdq":     [({"*": True}, 2, "np", {})],
    # shifts: imm form PU; CL form NP 4/5 (m/r). [IT p.178]
    "shl":     [({"src": "cl"}, 4, "np", {"flags": "w", "clocks_mem": 5}),
                ({"kind": "mem"}, 3, "u", {"flags": "w"}),
                ({"kind": "any"}, 1, "u", {"flags": "w"})],
    "shr":     [({"src": "cl"}, 4, "np", {"flags": "w", "clocks_mem": 5}),
                ({"kind": "mem"}, 3, "u", {"flags": "w"}),
                ({"kind": "any"}, 1, "u", {"flags": "w"})],
    "sal":     [({"src": "cl"}, 4, "np", {"flags": "w", "clocks_mem": 5}),
                ({"kind": "mem"}, 3, "u", {"flags": "w"}),
                ({"kind": "any"}, 1, "u", {"flags": "w"})],
    "sar":     [({"src": "cl"}, 4, "np", {"flags": "w", "clocks_mem": 5}),
                ({"kind": "mem"}, 3, "u", {"flags": "w"}),
                ({"kind": "any"}, 1, "u", {"flags": "w"})],
    "rol":     [({"src": "cl"}, 4, "np", {"clocks_mem": 5}),
                ({"src": "one"}, 1, "u", {}),
                ({"kind": "mem"}, 3, "np", {}),
                ({"kind": "any"}, 1, "np", {})],
    "ror":     [({"src": "cl"}, 4, "np", {"clocks_mem": 5}),
                ({"src": "one"}, 1, "u", {}),
                ({"kind": "mem"}, 3, "np", {}),
                ({"kind": "any"}, 1, "np", {})],
    # [IT p.178]: RCL/RCR r,1=1 u; r/m,i(>1)=8/10 np; r/m,CL=7/9 np
    "rcl":     [({"src": "cl"}, 7, "np", {"clocks_mem": 9, "conf": "hi"}),
                ({"src": "one"}, 1, "u", {"conf": "hi"}),
                ({"kind": "mem"}, 10, "np", {"conf": "lo", "note": "m,i 8/10; m,1 assumed 3? conservative 10"}),
                ({"kind": "any"}, 8, "np", {"conf": "hi", "note": "r,i(>1)=8"})],
    "rcr":     [({"src": "cl"}, 7, "np", {"clocks_mem": 9, "conf": "hi"}),
                ({"src": "one"}, 1, "u", {"conf": "hi"}),
                ({"kind": "mem"}, 10, "np", {"conf": "lo"}),
                ({"kind": "any"}, 8, "np", {"conf": "hi"})],
    "setcc_generic": [],  # expanded below per setXX
    "bswap":   [({"*": True}, 1, "np", {"conf": "hi"})],
    # [IT p.178]: SHLD/SHRD r,i/CL = 4 np; m,i/CL = 5 np
    "shld":    [({"kind": "mem"}, 5, "np", {"conf": "hi"}), ({"*": True}, 4, "np", {"flags": "w", "conf": "hi"})],
    "shrd":    [({"kind": "mem"}, 5, "np", {"conf": "hi"}), ({"*": True}, 4, "np", {"flags": "w", "conf": "hi"})],
    "movsx":   [({"*": True}, 3, "np", {"conf": "lo", "note": "IT '3 a)' incl P1 0F-decode; PMMX likely 2; keep 3 until calibrated"})],
    "movzx":   [({"*": True}, 3, "np", {"conf": "lo", "note": "same caveat as movsx"})],
    "jmp":     [({"kind": "regmem"}, 2, "np", {"branch": "indirect", "mispred": 4}),
                ({"*": True}, 1, "v", {"branch": "direct", "mispred": 4})],
    "call":    [({"kind": "regmem"}, 2, "np", {"branch": "indirect", "mispred": 4, "stack": "push"}),
                ({"*": True}, 1, "v", {"branch": "direct", "mispred": 4, "stack": "push"})],
    "ret":     [({"*": True}, 2, "np", {"branch": "ret", "mispred": 4, "stack": "pop", "conf": "hi", "note": "IT 2/5 (pred/mispred); ret imm may AGI-pop"})],
    "leave":   [({"*": True}, 2, "np", {"conf": "lo", "note": "not in IT Pentium table; = mov esp,ebp/pop ebp; U-only NP assumed"})],
    "pusha":   [({"*": True}, 5, "np", {"stack": "push", "conf": "hi", "note": "PUSHA/POPA 5-9; 9 if SP%4==0 [IT i]"})],
    "pushad":  [({"*": True}, 5, "np", {"stack": "push", "conf": "hi"})],
    "popa":    [({"*": True}, 5, "np", {"stack": "pop", "conf": "hi"})],
    "popad":   [({"*": True}, 5, "np", {"stack": "pop", "conf": "hi"})],
    "bound":   [({"*": True}, 8, "np", {"conf": "hi"})],
    "cld":     [({"*": True}, 2, "np", {"conf": "hi"})],
    "std":     [({"*": True}, 2, "np", {"conf": "hi"})],
    "clc":     [({"*": True}, 2, "np", {"conf": "hi"})],
    "stc":     [({"*": True}, 2, "np", {"conf": "hi"})],
}

# SETcc: 1/2 (r/m) np per IT.
for cc in ("a", "ae", "b", "be", "c", "e", "g", "ge", "l", "le", "na", "nae",
           "nb", "nbe", "nc", "ne", "ng", "nge", "nl", "nle", "no", "np", "ns",
           "nz", "o", "p", "pe", "po", "s", "z"):
    INT[f"set{cc}"] = [({"kind": "mem"}, 2, "np", {"flags": "r"}), ({"*": True}, 1, "np", {"flags": "r"})]

# Conditional jumps: 1 predicted / mispred 4 (U) 5 (V) [MA 3.3]; IT lists
# 1/4/5/6. Pairable in V-pipe only.
JCC = ("ja", "jae", "jb", "jbe", "jc", "je", "jg", "jge", "jl", "jle", "jna",
       "jnae", "jnb", "jnbe", "jnc", "jne", "jng", "jnge", "jnl", "jnle",
       "jno", "jnp", "jns", "jnz", "jo", "jp", "jpe", "jpo", "js", "jz",
       "jcxz", "jecxz")
for cc in JCC:
    INT[cc] = [({"*": True}, 1, "v", {"branch": "jcc", "mispred": 4, "flags": "r"})]
INT["loop"] = [({"*": True}, 5, "np", {"branch": "jcc", "mispred": 5, "clocks_range": (5, 10)})]
INT["loope"] = INT["loopz"] = INT["loopne"] = INT["loopnz"] = INT["loop"]

# ---------------------------------------------------------------------------
# x87. clocks/pair/i_ov/fp_ov straight from [IT] pp.179-180. pair here means
# 'pairable with FXCH' ('0' column = no, np = never); the only FP pairing is
# FU-CLASS instr (U) + FXCH (V) subject to [MA] 4.5 rules.
# ---------------------------------------------------------------------------

FP_PAIRABLE_U = {"fld", "fadd", "fsub", "fsubr", "fmul", "fdiv", "fdivr", "fcom", "fchs", "fabs"}  # [MA] 4.5 first-bullet list

FPU = {
    "fld":     [({"size": 80}, 3, "np", {}), ({"*": True}, 1, "fx", {"latency": 1, "stack": "fpush"})],
    "fst":     [({"kind": "mem"}, 2, "np", {"need_early": True, "i_ov": 0, "no_overlap": True})],
    "fstp":    [({"kind": "reg"}, 1, "np", {"stack": "fpop"}),
                ({"kind": "mem80"}, 3, "np", {"need_early": True, "no_overlap": True, "stack": "fpop"}),
                ({"*": True}, 2, "np", {"need_early": True, "no_overlap": True, "stack": "fpop"})],
    "fild":    [({"*": True}, 3, "np", {"fp_ov": 2, "i_ov": 2, "stack": "fpush"})],
    "fist":    [({"*": True}, 6, "np", {})],
    "fistp":   [({"*": True}, 6, "np", {"stack": "fpop"})],
    "fldz":    [({"*": True}, 2, "np", {"stack": "fpush"})],
    "fld1":    [({"*": True}, 2, "np", {"stack": "fpush"})],
    "fadd":    [({"*": True}, 3, "fx", {"fp_ov": 2, "i_ov": 2, "unit": "fpu"})],
    "faddp":   [({"*": True}, 3, "fx", {"fp_ov": 2, "i_ov": 2, "unit": "fpu", "stack": "fpop"})],
    "fsub":    [({"*": True}, 3, "fx", {"fp_ov": 2, "i_ov": 2, "unit": "fpu"})],
    "fsubp":   [({"*": True}, 3, "fx", {"fp_ov": 2, "i_ov": 2, "unit": "fpu", "stack": "fpop"})],
    "fsubr":   [({"*": True}, 3, "fx", {"fp_ov": 2, "i_ov": 2, "unit": "fpu"})],
    "fsubrp":  [({"*": True}, 3, "fx", {"fp_ov": 2, "i_ov": 2, "unit": "fpu", "stack": "fpop"})],
    "fmul":    [({"*": True}, 3, "fx", {"fp_ov": 2, "i_ov": 2, "unit": "fmul", "note": "1 extra if followed by FP mul [IT n]"})],
    "fmulp":   [({"*": True}, 3, "fx", {"fp_ov": 2, "i_ov": 2, "unit": "fmul", "stack": "fpop"})],
    # FDIV: 19/33/39 for 24/53/64-bit CW precision [IT p]; DJGPP CW = 53-bit
    # default (0x027f) -> 33. i_ov=38 listed is for 39-clk worst case; other
    # FP ops only overlap last 2. Non-pipelined divider.
    "fdiv":    [({"*": True}, 33, "fx", {"fp_ov": 2, "i_ov": 32, "unit": "fdiv", "clocks_range": (19, 39), "conf": "hi"})],
    "fdivp":   [({"*": True}, 33, "fx", {"fp_ov": 2, "i_ov": 32, "unit": "fdiv", "clocks_range": (19, 39), "stack": "fpop"})],
    "fdivr":   [({"*": True}, 33, "fx", {"fp_ov": 2, "i_ov": 32, "unit": "fdiv", "clocks_range": (19, 39)})],
    "fdivrp":  [({"*": True}, 33, "fx", {"fp_ov": 2, "i_ov": 32, "unit": "fdiv", "clocks_range": (19, 39), "stack": "fpop"})],
    "fidiv":   [({"*": True}, 36, "np", {"fp_ov": 2, "i_ov": 35, "unit": "fdiv", "clocks_range": (22, 42)})],
    "fidivr":  [({"*": True}, 36, "np", {"fp_ov": 2, "i_ov": 35, "unit": "fdiv", "clocks_range": (22, 42)})],
    "fsqrt":   [({"*": True}, 70, "np", {"fp_ov": 2, "i_ov": 69, "unit": "fdiv"})],
    "fiadd":   [({"*": True}, 6, "np", {"fp_ov": 2, "i_ov": 2})],
    "fisub":   [({"*": True}, 6, "np", {"fp_ov": 2, "i_ov": 2})],
    "fisubr":  [({"*": True}, 6, "np", {"fp_ov": 2, "i_ov": 2})],
    "fimul":   [({"*": True}, 6, "np", {"fp_ov": 2, "i_ov": 2})],
    "ficom":   [({"*": True}, 4, "np", {})],
    "ficomp":  [({"*": True}, 4, "np", {"stack": "fpop"})],
    "fcom":    [({"*": True}, 1, "fx", {})],
    "fcomp":   [({"*": True}, 1, "fx", {"stack": "fpop"})],
    "fcompp":  [({"*": True}, 1, "fx", {"stack": "fpop2"})],
    "fucom":   [({"*": True}, 1, "fx", {})],
    "fucomp":  [({"*": True}, 1, "fx", {"stack": "fpop"})],
    "fucompp": [({"*": True}, 1, "fx", {"stack": "fpop2"})],
    "ftst":    [({"*": True}, 1, "np", {})],
    "fxam":    [({"*": True}, 19, "np", {"i_ov": 4, "clocks_range": (17, 21)})],
    "fchs":    [({"*": True}, 1, "fx", {})],
    "fabs":    [({"*": True}, 1, "fx", {})],
    "frndint": [({"*": True}, 14, "np", {"clocks_range": (9, 20)})],
    "fscale":  [({"*": True}, 26, "np", {"i_ov": 5, "clocks_range": (20, 32)})],
    "fxtract": [({"*": True}, 39, "np", {"clocks_range": (12, 66)})],
    "fprem":   [({"*": True}, 40, "np", {"fp_ov": 2, "i_ov": 2, "clocks_range": (16, 64)})],
    "fprem1":  [({"*": True}, 45, "np", {"fp_ov": 2, "i_ov": 2, "clocks_range": (20, 70)})],
    "f2xm1":   [({"*": True}, 56, "np", {"fp_ov": 2, "i_ov": 2, "clocks_range": (53, 59)})],
    "fsin":    [({"*": True}, 82, "np", {"fp_ov": 2, "i_ov": 2, "clocks_range": (65, 100)})],
    "fcos":    [({"*": True}, 82, "np", {"fp_ov": 2, "i_ov": 2, "clocks_range": (65, 100)})],
    "fsincos": [({"*": True}, 100, "np", {"fp_ov": 2, "i_ov": 2, "clocks_range": (89, 112)})],
    "fptan":   [({"*": True}, 133, "np", {"i_ov": 36, "fp_ov": 0, "clocks_range": (120, 147)})],
    "fpatan":  [({"*": True}, 123, "np", {"fp_ov": 2, "i_ov": 2, "clocks_range": (112, 134), "stack": "fpop"})],
    "fyl2x":   [({"*": True}, 103, "np", {"fp_ov": 2, "i_ov": 2, "stack": "fpop"})],
    "fyl2xp1": [({"*": True}, 105, "np", {"fp_ov": 2, "i_ov": 2, "stack": "fpop"})],
    "fnop":    [({"*": True}, 1, "np", {})],
    "fxch":    [({"*": True}, 1, "fxch", {"note": "renames; pairs in V per [MA] 4.5; unpaired costs 1; imperfect +1 if next instr not FP"})],
    "fincstp": [({"*": True}, 2, "np", {})],
    "fdecstp": [({"*": True}, 2, "np", {})],
    "ffree":   [({"*": True}, 2, "np", {})],
    "fnstsw":  [({"*": True}, 6, "np", {"note": "first 4 clocks overlap preceding integer [IT q]"})],
    "fnstcw":  [({"*": True}, 2, "np", {})],
    "fldcw":   [({"*": True}, 8, "np", {})],
    "wait":    [({"*": True}, 1, "np", {})],
    "fwait":   [({"*": True}, 1, "np", {})],
    "fnclex":  [({"*": True}, 7, "np", {})],
    "fninit":  [({"*": True}, 17, "np", {})],
}

# ---------------------------------------------------------------------------
# MMX [IT p.180]: everything 1 clock except multiply family 3 (pipelined at
# 1/clk; last 2 clocks overlap like FP). PMMX transitions [IT p.180]:
# first FP after EMMS ~= +58; first MMX after FP ~= +38.
# Pairability [MA rule 10]:
#   - shift/pack/unpack: either pipe, never pair with mmshift-class peers;
#   - multiply: either pipe, never pair with multiply peers;
#   - accesses memory or integer regs: U-pipe ONLY, never pairs with non-MMX;
#   - EMMS: NP.
# Storing from an MMX register needs the data one clock early (like FST) [IT].
# ---------------------------------------------------------------------------

MM_SHIFT_PACK = {"psllw", "pslld", "psllq", "psrlw", "psrld", "psrlq",
                 "psraw", "psrad", "psraw", "packsswb", "packssdw", "packuswb",
                 "punpcklbw", "punpcklwd", "punpckldq", "punpckhbw",
                 "punpckhwd", "punpckhdq"}
MM_MUL = {"pmullw", "pmulhw", "pmaddwd", "pmulhuw"}

MMX = {}
for m in sorted(MM_SHIFT_PACK):
    MMX[m] = [({"*": True}, 1, "uv", {"unit": "mmshift"})]
for m in sorted(MM_MUL):
    MMX[m] = [({"*": True}, 1, "uv", {"unit": "mmmul", "latency": 3, "fp_ov": 2, "note": "3clk latency, 1/clk throughput; last 2 overlap"})]
for m in ("paddb", "paddw", "paddd", "paddsb", "paddsw", "paddusb", "paddusw",
          "psubb", "psubw", "psubd", "psubsb", "psubsw", "psubusb", "psubusw",
          "pcmpeqb", "pcmpeqw", "pcmpeqd", "pcmpgtb", "pcmpgtw", "pcmpgtd",
          "pand", "pandn", "por", "pxor"):
    MMX[m] = [({"*": True}, 1, "uv", {})]
MMX["movq"] = [({"kind": "mr"}, 1, "u", {"need_early": True, "note": "store: data needed 1 clk early"}),
               ({"kind": "any"}, 1, "u", {"note": "mem/int-reg form: U-only, pairs only with MMX [MA r10]"})]
MMX["movd"] = MMX["movq"]
MMX["emms"] = [({"*": True}, 1, "np", {"transition": "mmx_exit"})]

MMX_FP_AFTER_EMMS = 58   # first FP instr after EMMS, extra clocks [IT p.180]
MMX_AFTER_FP = 38        # first MMX instr after an FP instr, extra clocks

# ---------------------------------------------------------------------------
# Machine constants [MA ch.4 + IT footnotes].
# ---------------------------------------------------------------------------

JCC_MISPRED_U = 4     # [MA 3.3]
JCC_MISPRED_V = 5     # [MA 3.3]
OTHER_CTI_MISPRED = 4 # [MA 3.3]
BRANCH_ON_NONJUMP_PREDICT = 3  # predicting non-jump pair as jump [MA ~p.21]

MISALIGN_LINE_CROSS = 3   # data access crossing 32B L1 line [IT/MA, est-class]
FIFO_DEPTH = 4            # decoded-instruction FIFO [MA 4.4]
MAX_DECODE_LEN_FOR_PAIR = 7  # >7 bytes => decodes alone [MA 4.4 / rule 8]

# FDIV precision selection: DJGPP libm/default CW = 53-bit (0x027f).
FDIV_CLOCKS = {24: 19, 53: 33, 64: 39}
FDIV_ACTIVE = 33
