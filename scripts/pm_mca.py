#!/usr/bin/env python3
"""pm_mca: static Pentium MMX (P55C) basic-block/loop cycle analyzer.

Statically scores *instruction-window* cost of a straight-line slice ending in
a loop backedge: U/V pairing + imperfect pairing, AGI, register/FP-stack
dependencies, decode/prefix bandwidth, x87 pipelining & FDIV serialization,
MMX issue resources and FP<->MMX transition costs, L1-hit latency classes.

Deliberately NOT modeled (reported as counts/flags instead):
  * I$/D$ misses, DRAM, PCI/VGA, write-buffer occupancy
  * runtime addresses: bank conflicts only computed when the manifest declares
    the relationship between the two bases; otherwise counted as bank_unknown
  * branch prediction beyond: loop backedge steady-state predicted-taken,
    inner data-dependent branches OUT OF SCOPE for slices (v1 rejects slices
    containing any control transfer other than the final backward jcc)
Unknown opcodes are a hard error - never guess. Timing sources cited per row
in pm_mca_data.py: [IT] = Agner Fog instruction tables (Pentium/PMMX section,
pp.177-180), [MA] = Agner Fog microarchitecture manual (ch.3.3 + ch.4).

Subcommands:
  analyze --asm FILE                 analyze one hand-written loop
  analyze --objdump FILE             analyze every function in a dump
  analyze --exe SILVER.EXE --symbol SYM [--start RE] [--end RE]
  selftest                           Agner worked examples (published clocks)
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pm_mca_data import (  # noqa: E402
    FDIV_ACTIVE, FPU, INT, MMX, MM_MUL, MM_SHIFT_PACK, FP_PAIRABLE_U,
    JCC_MISPRED_U, JCC_MISPRED_V, MAX_DECODE_LEN_FOR_PAIR,
    MMX_FP_AFTER_EMMS, MMX_AFTER_FP,
)

VERSION = "pm-mca-0.2.0"

# ==========================================================================
# Parsing
# ==========================================================================

FULL_REG = {
    "al": "eax", "ah": "eax", "ax": "eax", "eax": "eax", "r0": "eax",
    "bl": "ebx", "bh": "ebx", "bx": "ebx", "ebx": "ebx",
    "cl": "ecx", "ch": "ecx", "cx": "ecx", "ecx": "ecx",
    "dl": "edx", "dh": "edx", "dx": "edx", "edx": "edx",
    "si": "esi", "esi": "esi", "di": "edi", "edi": "edi",
    "bp": "ebp", "ebp": "ebp", "sp": "esp", "esp": "esp",
}
REG_SIZE = {k: (8 if len(k) == 2 and k[1] in "lh" else 16 if len(k) == 2 else 32)
            for k in FULL_REG}
MMREGS = {f"mm{n}" for n in range(8)}

PREFIX_BYTES = {0x66: "opsz", 0x67: "addrsz",
                0x2E: "seg", 0x3E: "seg", 0x26: "seg", 0x64: "seg",
                0x65: "seg", 0x36: "seg", 0xF0: "lock", 0xF2: "rep",
                0xF3: "rep"}
# [MA 4.4] PMMX decode delay: segment/repeat prefix +1; addr/opsize +2 and
# decode-alone; each further prefix +1. 0FH is part of the opcode (free).
PREFIX_DELAY = {"seg": 1, "rep": 1, "lock": 1, "opsz": 2, "addrsz": 2}
SOLO_DECODE = {"opsz", "addrsz"}
PREFIX_INV = {"ds": 0x3E, "es": 0x26, "fs": 0x64, "gs": 0x65, "cs": 0x2E,
              "ss": 0x36, "lock": 0xF0, "rep": 0xF3, "repz": 0xF3,
              "repne": 0xF2, "repnz": 0xF2, "data16": 0x66, "addr16": 0x67}


@dataclass
class MemRef:
    base: str | None
    index: str | None
    scale: int
    disp: int
    size: int  # bits
    raw: str = ""


@dataclass
class Op:
    kind: str            # reg | mem | imm | fpreg | mmreg | seg
    text: str
    reg: str | None = None   # full-register key
    size: int = 32
    mem: MemRef | None = None


@dataclass
class Ins:
    addr: int | None
    nbytes: int
    mne: str
    ops: list[Op]
    raw: str
    prefixes: tuple = ()
    # bound at analysis time
    row: object = None          # Row
    kind: str = "any"
    rd: tuple = ()              # integer-reg full keys read at issue
    wr: tuple = ()              # (reg, result-latency-from-issue)
    mem_r: tuple = ()           # MemRef reads
    mem_w: tuple = ()           # MemRef writes
    mm_rd: tuple = ()
    mm_wr: tuple = ()
    fp_rd: tuple = ()           # stack positions read (0 = st0)
    fp_wr: tuple = ()
    fp_push: bool = False
    fp_pop: int = 0

    def prefix_delay(self) -> int:
        # [MA 4.4]
        d = 0
        for p in self.prefixes:
            kind = PREFIX_BYTES.get(p)
            if kind:
                d += PREFIX_DELAY[kind]
        return d

    def decodes_solo(self) -> bool:
        return (self.nbytes > MAX_DECODE_LEN_FOR_PAIR or
                any(PREFIX_BYTES.get(p) in SOLO_DECODE for p in self.prefixes) or
                self.prefix_delay() > 0 and any(
                    PREFIX_BYTES.get(p) in SOLO_DECODE for p in self.prefixes))

    def is_branch(self) -> bool:
        return self.row is not None and self.row.extra.get("branch") is not None


_MEM_RE = re.compile(r"^(?:(BYTE|WORD|DWORD|QWORD|TBYTE|XWORD) PTR )?\[(.*)\]$")
_PTR_SIZE = {"BYTE": 8, "WORD": 16, "DWORD": 32, "QWORD": 64, "TBYTE": 80, "XWORD": 128}


def parse_operand(text: str) -> Op:
    t = text.strip()
    low = t.lower().replace("ptr", "").strip() if t.startswith(("BYTE ", "WORD ", "DWORD ", "QWORD ", "TBYTE ", "XWORD ")) else t.lower()
    if low in FULL_REG:
        return Op("reg", t, reg=FULL_REG[low], size=REG_SIZE.get(low, 32))
    if low in MMREGS:
        return Op("mmreg", t, reg=low, size=64)
    if re.fullmatch(r"ds:", low):
        return Op("seg", t, reg="ds")
    mst = re.fullmatch(r"st(?:\(?([0-7])\)?)?", low)
    if mst:
        n = int(mst.group(1)) if mst.group(1) else 0
        return Op("fpreg", t, reg=f"st{n}", size=80)
    m = _MEM_RE.match(t)
    if m:
        size = _PTR_SIZE.get(m.group(1) or "DWORD", 32)
        return Op("mem", t, mem=parse_memref(m.group(2), size), size=size)
    # absolute moffs addressing: "DWORD PTR ds:0x399e10" / "fs:0x1c" (no [])
    mabs = re.match(r"^(?:(BYTE|WORD|DWORD|QWORD|TBYTE|XWORD) PTR )?([cdefgs]s:)(0x[0-9a-f]+|[0-9a-f]+)$", t)
    if mabs:
        size = _PTR_SIZE.get(mabs.group(1) or "DWORD", 32)
        return Op("mem", t, mem=MemRef(None, None, 1, int(mabs.group(3), 16),
                                       size, f"{mabs.group(2)}{mabs.group(3)}"), size=size)
    if low in ("es", "cs", "ss", "ds", "fs", "gs"):
        return Op("seg", t, reg=low)
    return Op("imm", t)


def parse_memref(inner: str, size: int) -> MemRef:
    base = index = None
    scale = 1
    disp = 0
    inner = inner.replace("ds:", "").strip()
    for term in re.split(r"(?=[+-])", inner):
        term = term.strip()
        if not term or term == "+":
            continue
        sign = 1
        if term[0] == "-":
            sign, term = -1, term[1:]
        elif term[0] == "+":
            term = term[1:]
        term = term.strip()
        m = re.match(r"^(eax|ebx|ecx|edx|esi|edi|ebp|esp)(?:\*(\d))?$", term)
        if m:
            reg = m.group(1)
            if m.group(2):
                index, scale = reg, int(m.group(2))
            elif base is None:
                base = reg
            else:
                index = reg
            continue
        mnum = re.match(r"^(0x[0-9a-fA-F]+|\d+)", term)
        if mnum:
            disp += sign * int(mnum.group(1), 0)
        # else: bare symbol => unknown static address, disp contribution 0
    return MemRef(base, index, scale, disp, size, inner.strip())


def split_operands(text: str) -> list[str]:
    parts, depth, cur = [], 0, ""
    for ch in text:
        if ch == "[":
            depth += 1
        elif ch == "]":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    return parts


def parse_asm(text: str) -> list[Ins]:
    """nasm-ish lines for tests/authoring: 'mne op1, op2'."""
    out = []
    for line in text.splitlines():
        line = line.split(";", 1)[0].strip()
        if not line or line.endswith(":"):
            continue
        m = re.match(r"^([a-z][a-z0-9.]*)\s*(.*)$", line, re.I)
        if not m:
            raise ValueError(f"bad asm line: {line!r}")
        mne = m.group(1).lower()
        ops = [parse_operand(p) for p in split_operands(m.group(2))] if m.group(2).strip() else []
        # crude byte-length estimate (asm text has no encoding): opcode(1) +
        # modrm(1) + mem: bare symbol => disp32 (+4), reg-based => (+0/1) +
        # imm (+1). Only decode bandwidth scheduling depends on this [MA 4.4].
        memcost = 0
        for op in ops:
            if op.kind == "mem":
                memcost += 4 if (op.mem.base is None and op.mem.index is None) else 2
        est = 2 + memcost + sum(1 for op in ops if op.kind == "imm")
        out.append(Ins(None, min(est, 7), mne, ops, line.strip()))
    return out


_OBJDUMP_RE = re.compile(r"^\s*([0-9a-f]+):\s*((?:[0-9a-f]{2}\s+)+)\t(.*)$")
_FUNC_RE = re.compile(r"^[0-9a-f]+ <([^>]+)>:")


def parse_objdump(text: str) -> list[Ins]:
    out = []
    for line in text.splitlines():
        m = _OBJDUMP_RE.match(line)
        if not m:
            continue
    pending_prefix: tuple = ()
    pending_n = 0
    for line in text.splitlines():
        m = _OBJDUMP_RE.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        bytes_ = m.group(2).split()
        asm = m.group(3).strip()
        asm = re.sub(r"<[^>]+>", "", asm)            # drop <sym+off> decorations
        asm = re.sub(r"\s+", " ", asm).strip()
        # prefixes are LEADING bytes only (0F is opcode, not prefix, on PMMX)
        prefixes: list[int] = []
        for b in bytes_:
            if int(b, 16) in PREFIX_BYTES:
                prefixes.append(int(b, 16))
            else:
                break
        tab = asm.split(None, 1)
        mne = tab[0].lower()
        ops_text = tab[1] if len(tab) == 2 else ""
        # objdump emits a lone prefix as its own line for >7B instructions;
        # fold it into the following instruction.
        if mne in PREFIX_INV and not ops_text:
            pending_prefix = tuple(prefixes) if prefixes else (PREFIX_INV[mne],)
            pending_n = len(bytes_)
            continue
        if pending_prefix:
            prefixes = list(pending_prefix) + prefixes
            nbytes_extra = pending_n
            pending_prefix, pending_n = (), 0
        else:
            nbytes_extra = 0
        try:
            ops = [parse_operand(p) for p in split_operands(ops_text)] if ops_text.strip() else []
        except ValueError:
            ops = []
        out.append(Ins(addr, len(bytes_) + nbytes_extra, mne, ops, asm, tuple(prefixes)))
    return out


def parse_objdump_symbols(text: str) -> dict[str, list[Ins]]:
    """Split a full objdump into {symbol: instructions} on function labels."""
    funcs: dict[str, list[Ins]] = {}
    cur_name: str | None = None
    cur_lines: list[str] = []

    def flush():
        nonlocal cur_name, cur_lines
        if cur_name is not None and cur_lines:
            if cur_name not in funcs:
                funcs[cur_name] = parse_objdump("\n".join(cur_lines))
        cur_name, cur_lines = None, []

    for line in text.splitlines():
        f = _FUNC_RE.match(line)
        if f:
            flush()
            cur_name = f.group(1)
            continue
        if cur_name is not None and _OBJDUMP_RE.match(line):
            cur_lines.append(line)
    flush()
    return funcs


# ==========================================================================
# Classification & row lookup
# ==========================================================================

@dataclass
class Row:
    clocks: int
    pair: str          # uv | u | v | np | fx | fxch
    extra: dict


def classify(ins: Ins) -> str:
    """Operand-shape class used to select a DB row."""
    ops = ins.ops
    if not ops:
        return "any"
    if ins.mne in ("jmp", "call") and ops[0].kind in ("reg", "mem"):
        return "regmem"
    if ins.mne in ("shl", "shr", "sar", "sal", "rol", "ror", "rcr", "rcl"):
        if ops[0].kind == "mem":
            return "mem" if len(ops) < 2 or ops[1].text.strip() != "cl" else "mem_cl"
    if ops[0].kind == "mem":
        if len(ops) > 1 and ops[1].kind == "reg":
            return "mr"
        if len(ops) > 1 and ops[1].kind == "imm":
            return "mem_imm"
        return "mem"
    if len(ops) > 1 and ops[1].kind == "mem":
        return "rm"
    if len(ops) > 1 and ops[0].kind in ("reg", "mmreg") and ops[1].kind in ("reg", "mmreg"):
        return "rr"
    if ins.mne == "test" and any(o.kind == "imm" for o in ops) and any(o.kind == "reg" for o in ops):
        return "reg_imm"
    return "any"


def operand_size(ins: Ins) -> int:
    sizes = [o.mem.size if o.kind == "mem" else o.size
             for o in ins.ops if o.kind in ("reg", "mem", "mmreg")]
    return max(sizes) if sizes else 32


def lookup(ins: Ins) -> Row:
    mne = ins.mne
    if ins.prefixes and any(PREFIX_BYTES.get(p) in ("rep", "lock") for p in ins.prefixes):
        raise KeyError(f"rep/lock-prefixed {mne}: out of slice scope (report as unsupported)")
    table = INT.get(mne) or FPU.get(mne) or MMX.get(mne)
    if table is None:
        raise KeyError(f"unknown instruction {mne!r} (refusing to guess timing)")
    kind = classify(ins)
    size = operand_size(ins)
    for match, clocks, pair, extra in table:
        ok = True
        if not match.get("*"):
            for key, want in match.items():
                if key == "size_max":
                    ok &= size <= want
                elif key == "size":
                    ok &= size == want
                elif key == "dst":
                    ok &= (bool(ins.ops) and ins.ops[0].kind == "reg"
                           and ins.ops[0].reg == "eax") == (want == "accum")
                elif key == "src":
                    src = ins.ops[1].text.strip().lower() if len(ins.ops) > 1 else ""
                    got = "cl" if src == "cl" else ("one" if src in ("1", "0x1") else src)
                    ok &= got == want
                elif key == "kind":
                    if want == "mem":
                        ok &= kind in ("mem", "mr", "mem_imm", "rm") if mne in ("push", "pop", "fst", "fstp") else kind == "mem"
                        ok = True if want == "mem" and ins.ops and ins.ops[0].kind == "mem" else ok
                    elif want == "reg":
                        ok &= bool(ins.ops) and ins.ops[0].kind in ("reg", "fpreg", "mmreg")
                    elif want == "any":
                        ok &= True
                    else:
                        ok &= kind == want
        if ok:
            row = Row(clocks, pair, dict(extra))
            disp = any(o.kind == "mem" and o.mem and o.mem.disp for o in ins.ops)
            has_imm = any(o.kind == "imm" for o in ins.ops)
            if disp and has_imm and pair == "uv":
                row.pair = "u"     # [MA rule 8]: disp+imm => U-pipe only on PMMX
            return row
    raise KeyError(f"no timing row for {mne!r} kind={kind} size={size}")


def bind(ins: Ins):
    """Precompute row + read/write sets (integer side; FP deps are dynamic)."""
    ins.row = lookup(ins)
    ins.kind = classify(ins)
    mne, ops = ins.mne, ins.ops
    rd, wr = [], []
    mem_r, mem_w = [], []
    mm_rd, mm_wr = [], []

    def memregs(o):
        if o.mem is None:
            return []
        return [r for r in (o.mem.base, o.mem.index) if r]

    if mne == "lea":
        rd += memregs(ops[1])
        wr.append((ops[0].reg, 1))
    elif mne == "mov":
        if ins.kind == "mr":     # store
            mem_w.append(ops[0].mem)
            rd += memregs(ops[0])
            rd.append(ops[1].reg)
        elif ins.kind == "mem_imm":
            mem_w.append(ops[0].mem)
            rd += memregs(ops[0])
        else:                    # load or reg-reg
            wr.append((ops[0].reg, 1))
            if ops[1].kind == "mem":
                mem_r.append(ops[1].mem)
                rd += memregs(ops[1])
            else:
                rd.append(ops[1].reg)
    elif mne in ("movzx", "movsx"):
        wr.append((ops[0].reg, ins.row.clocks))
        if ops[1].kind == "mem":
            mem_r.append(ops[1].mem)
            rd += memregs(ops[1])
        else:
            rd.append(ops[1].reg)
    elif mne == "push":
        rd.append("esp")
        wr.append(("esp", 0))
        if ops[0].kind == "mem":
            mem_r.append(ops[0].mem)
            rd += memregs(ops[0])
        elif ops[0].kind == "reg":
            rd.append(ops[0].reg)
    elif mne == "pop":
        rd.append("esp")
        wr.append(("esp", 0))
        mem_r.append(MemRef("esp", None, 1, 0, 32))
        if ops[0].kind == "reg":
            wr.append((ops[0].reg, 1))
        else:
            mem_w.append(ops[0].mem)
            rd += memregs(ops[0])
    elif mne in ("call", "jmp", "ret", "leave"):
        if mne == "call":
            rd.append("esp")
            wr.append(("esp", 0))
        if mne in ("ret", "leave"):
            rd.append("esp")
            if mne == "leave":
                rd.append("ebp")
                wr.append(("ebp", 2))
                wr.append(("esp", 2))
            else:
                wr.append(("esp", 0))
    elif mne == "cdq":
        rd.append("eax")
        wr.append(("edx", ins.row.clocks))
    elif mne == "cwde":
        rd.append("eax")
        wr.append(("eax", ins.row.clocks))
    elif mne in ("mul", "div", "idiv", "imul") and len(ops) == 1:
        rd.append("eax")
        rd += [ops[0].reg] if ops[0].kind == "reg" else memregs(ops[0])
        if ops[0].kind == "mem":
            mem_r.append(ops[0].mem)
        wr.append(("eax", ins.row.clocks))
        wr.append(("edx", ins.row.clocks))
    elif mne in MMX:
        # dst-first; mm regs and mem/int-reg mix handled via kind/mmx_mem_int
        for i, o in enumerate(ops):
            if o.kind == "mmreg":
                (mm_wr if i == 0 and ins.kind != "mr" else mm_rd).append(o.reg)
                if ins.kind == "mr":
                    mm_rd.append(o.reg)
            elif o.kind == "reg":
                (wr if i == 0 and ins.kind == "any" else rd).append(o.reg)
                if ins.kind == "mr":
                    rd.append(o.reg)
            elif o.kind == "mem":
                (mem_w if ins.kind == "mr" else mem_r).append(o.mem)
                rd += memregs(o)
        if ins.mne in MMX and ins.kind == "any" and ops and ops[0].kind == "mmreg":
            if ops[0].reg in mm_rd:
                mm_rd.remove(ops[0].reg)
            if ops[0].reg not in mm_wr and mne != "emms":
                mm_wr.append(ops[0].reg)
        if mne == "emms":
            mm_rd.clear(); mm_wr.clear()
    elif mne in FPU:
        pass  # handled dynamically on the FP stack
    else:
        # generic ALU family: dst-first
        if ops and ops[0].kind == "reg":
            rd.append(ops[0].reg)
            wr.append((ops[0].reg, ins.row.clocks))
        if len(ops) > 1:
            src = ops[1]
            if src.kind == "reg":
                if src.reg != "cl" or mne in ("shl", "shr", "sar", "sal", "rol", "ror", "rcr", "rcl"):
                    rd.append(src.reg)
            elif src.kind == "mem":
                mem_r.append(src.mem)
                rd += memregs(src)
        if ins.kind in ("mr", "mem_imm") and ops and ops[0].kind == "mem":
            mem_w.append(ops[0].mem)
            mem_r.append(ops[0].mem)
            rd += memregs(ops[0])
    if ins.row.extra.get("flags") in ("r", "rw") or ins.row.extra.get("branch") == "jcc":
        rd.append("flags")
    if ins.row.extra.get("flags") in ("w", "rw"):
        wr.append(("flags", ins.row.clocks))
    # dedupe preserving order
    ins.rd = tuple(dict.fromkeys(r for r in rd if r))
    ins.wr = tuple(w for w in wr if w[0])
    ins.mem_r = tuple(mem_r)
    ins.mem_w = tuple(mem_w)
    ins.mm_rd = tuple(dict.fromkeys(mm_rd))
    ins.mm_wr = tuple(dict.fromkeys(mm_wr))
    # FP stack positions
    if mne == "fxch":
        k = 1
        for o in ops:
            if o.kind == "fpreg":
                k = int(o.reg[2])
        ins.fp_rd, ins.fp_wr = (0, k), (0, k)
    elif mne in ("fld", "fild", "fldz", "fld1"):
        ins.fp_push, ins.fp_rd, ins.fp_wr = True, (), (0,)
    elif mne in ("fstp", "fistp"):
        ins.fp_rd, ins.fp_wr, ins.fp_pop = (0,), (), 1
    elif mne in ("fcom", "fucom"):
        ins.fp_rd = (0, 1) if any(o.kind == "fpreg" and o.reg != "st0" for o in ops) else (0,)
    elif mne in ("fcomp", "fucomp"):
        ins.fp_rd, ins.fp_pop = (0,), 1
    elif mne in ("fcompp", "fucompp"):
        ins.fp_rd, ins.fp_pop = (0, 1), 2
    elif mne == "fst":
        ins.fp_rd = (0,) if not any(o.kind == "fpreg" and o.reg != "st0" for o in ops) else (0,)
    elif mne in FPU:
        ps = tuple(int(o.reg[2]) for o in ops if o.kind == "fpreg")
        if not ps:
            ps = (0,)
        ins.fp_rd = ps
        pop = ins.row.extra.get("stack")
        ins.fp_pop = 2 if pop == "fpop2" else (1 if pop == "fpop" else 0)
        ins.fp_wr = (ps[0],)


def bank_conflict(a: MemRef, b: MemRef, deltas: dict) -> bool | None:
    """[MA 4.1 imperfect rule 2-3]: conflict iff addr bits 2-4 equal.
    None = unknown: two accesses we cannot relate are never assumed equal."""
    if (a.base, a.index, a.scale) == (b.base, b.index, b.scale):
        if a.base is None and a.raw != b.raw:
            return None  # distinct bare symbols: unknown relationship
        return ((a.disp - b.disp) >> 2) & 7 == 0
    key = f"{(b.base or '')}-{(a.base or '')}"
    if key in deltas:
        return ((a.disp + deltas[key] - b.disp) >> 2) & 7 == 0
    return None


# ==========================================================================
# Simulator
# ==========================================================================

@dataclass
class Ctx:
    align: dict = field(default_factory=dict)   # base reg -> assumed alignment
    deltas: dict = field(default_factory=dict)  # "rY-rX" -> known byte delta
    fd_precision: int = FDIV_ACTIVE


class Sim:
    """Single-pass issue simulation over an unrolled loop body.

    State is 'next usable cycle' per resource; the integer stream advances one
    U/V slot at a time. FP ops occupy the U slot for their issue cycle but
    retire into the pipelined x87 scoreboard, so integer issue continues [MA
    4.5]; the divider is non-pipelined [IT p]."""

    def __init__(self, instrs: list[Ins], ctx: Ctx):
        if len(instrs) < 2:
            raise ValueError("slice needs >= 2 instructions")
        for ins in instrs:
            bind(ins)
        last = instrs[-1]
        if last.row.extra.get("branch") != "jcc":
            raise ValueError("v1 slice must end in the (backward) conditional jump")
        for ins in instrs[:-1]:
            if ins.is_branch():
                raise ValueError(f"v1 slice contains inner control flow: {ins.raw!r}")
        self.ins = instrs
        self.ctx = ctx
        self.avail = self._decode_schedule()
        self.period = self.avail[-1] + 1
        # machine state
        self.c = 0
        self.ready = {}            # int regs & memory-side timing
        self.flags_ready = 0
        self.stack = []            # fp ready cycles, top-first
        self.mmready = {}
        self.fp_next = 0           # next cycle the FP pipe accepts an FP op
        self.fdiv_free = 0         # divider busy until
        self.fdiv_intmul_free = 0  # integer mul blocked until (FDIV i_ov, [IT o])
        self.fmul_last = -9
        self.mmmul_free = 0
        self.mode = "fpu"          # fpu | mmx_dirty | mmx_clean (post-EMMS)
        self.fp_dirty = False      # fp executed since last EMMS/MMX
        self.esp_stackop = True
        self.blocked_until = 0     # integer serializing ops (imul/div/ret...)
        self.last_fp_u_issue = -9  # cycle an FP_U-class instr last issued
        # stats
        self.stalls = {}
        self.agi = 0
        self.bank_conflicts = 0
        self.bank_unknown = 0
        self.imperfect = 0
        self.fxch_pairs = 0
        self.memops = 0
        self.stores = 0
        self.bytes_rw = 0
        self.misalign_risk = 0
        self.transitions = {"fp_after_emms": 0, "mmx_after_fp": 0}
        self.fmul_bubbles = 0
        self.log = []

    # ------------- decode -------------
    def _decode_schedule(self) -> list[int]:
        avail, i, c = [], 0, 0
        n = len(self.ins)
        while i < n:
            ins = self.ins[i]
            solo = ins.nbytes > MAX_DECODE_LEN_FOR_PAIR or any(
                PREFIX_BYTES.get(p) in SOLO_DECODE for p in ins.prefixes)
            avail.append(c)
            c += 1 + ins.prefix_delay()
            i += 1
            if (not solo and ins.prefix_delay() == 0 and i < n and
                    self.ins[i].nbytes <= MAX_DECODE_LEN_FOR_PAIR and
                    not self.ins[i].prefixes):
                avail.append(c - 1)
                i += 1
        return avail

    # ------------- helpers -------------
    def rdy(self, reg) -> int:
        if not reg:
            return 0
        return self.ready.get(reg, 0)

    def stall(self, why: str, n=1):
        self.stalls[why] = self.stalls.get(why, 0) + n

    def agi_check(self, ins: Ins, at: int) -> bool:
        """[MA 4.2]: address reg written by the immediately-preceding cycle."""
        if at <= 0:
            return False
        refs = []
        if ins.mne == "lea" and len(ins.ops) > 1 and ins.ops[1].kind == "mem":
            refs = [ins.ops[1].mem]
        else:
            refs = list(ins.mem_r) + list(ins.mem_w)
        if ins.mne in ("push", "pop", "call", "ret"):
            return not self.esp_stackop and self.ready.get("esp", 0) == at
        for m in refs:
            for r in (m.base, m.index):
                # [MA 4.2]: address reg's PRODUCER executed in the immediately
                # preceding cycle => its result becomes usable this cycle but
                # the address stage already missed it (+1). ready > at is an
                # ordinary dependency stall, not an AGI.
                if r and r != "esp" and self.ready.get(r, -99) == at:
                    return True
        return False

    def count_mem(self, ins: Ins):
        for m in list(ins.mem_r) + list(ins.mem_w):
            self.memops += 1
            self.bytes_rw += max(1, m.size // 8)
            al = self.ctx.align.get(m.base or "", 4)
            granularity = max(al, 1)
            pos = (m.disp % 32 + (32 - al) % 32) if al < 32 else m.disp % al + m.disp // al % 32
            # conservative: flag if size may cross 32B line under declared align
            if m.size // 8 > granularity and (m.disp % 32 + m.size // 8) > 32:
                self.misalign_risk += 1
        self.stores += len(ins.mem_w)

    # ------------- pairing -------------
    def can_pair(self, u: Ins, v: Ins) -> tuple[bool, str]:
        ur, vr = u.row, v.row
        um, vm = u.mne, v.mne
        upipe = ur.pair in ("uv", "u") or (ur.pair == "fx" and um in FP_PAIRABLE_U)
        vpipe = vr.pair in ("uv", "v") or vm == "fxch"
        if not upipe or not vpipe:
            return False, "pair-class"
        # FP: only FP_U + FXCH pairs, else nothing
        if um in FPU or vm in FPU:
            if um in FP_PAIRABLE_U and vm == "fxch":
                return True, "fxch-pair"
            return False, "fp-single"
        # translation: U-slot MMX memory/int-reg cannot pair with non-MMX;
        # such MMX never sits in V [MA rule 10]
        u_mm_mem = um in MMX and (bool(u.mem_r) or bool(u.mem_w) or
                                  any(o.kind == "reg" for o in u.ops))
        v_mm_mem = vm in MMX and (bool(v.mem_r) or bool(v.mem_w) or
                                  any(o.kind == "reg" for o in v.ops))
        if v_mm_mem or (u_mm_mem and vm not in MMX):
            return False, "mmx-mem-int-u-only"
        if um in MMX and vm in MMX:
            if ur.extra.get("unit") == vr.extra.get("unit") == "mmshift":
                return False, "mmx-one-shifter"
            if ur.extra.get("unit") == vr.extra.get("unit") == "mmmul":
                return False, "mmx-one-multiplier"
        # rule 2/3: v must not read or write what u writes (partial = full)
        uw = {w for w, _ in u.wr}
        flags_case = "flags" in uw and (
            vr.extra.get("flags") in ("w", "rw") or vr.extra.get("branch") == "jcc"
            or "flags" in {w for w, _ in v.wr})
        esp_case = (um in ("push", "call") and vm in ("push", "call")) or (um == "pop" and vm == "pop")
        if not flags_case and "flags" in uw and ("flags" in v.rd or "flags" in {w for w, _ in v.wr}):
            return False, "flags"
        for w in uw - {"flags"}:
            if w in v.rd or any(w == vw for vw, _ in v.wr):
                if w == "esp" and esp_case:
                    continue
                return False, f"raw-waw:{w}"
        # v writing regs u READS is fine (write-after-read): checked implicitly
        return True, "ok"

    # ------------- issue ---------------
    def issue_fp(self, ins: Ins, e: int, it: int) -> int:
        """Issue FP at cycle e; returns the cycle the U slot is freed."""
        row = ins.row
        if self.mode == "mmx_clean":
            self.transitions["fp_after_emms"] += 1
            self.stall("fp-after-emms", MMX_FP_AFTER_EMMS)
            e += MMX_FP_AFTER_EMMS
        elif self.mode == "mmx_dirty":
            # FP touching the stack without EMMS: tags alias MMX data => the
            # VALUES would be garbage (a guest bug, and gcc never does this),
            # but there is no documented timing penalty [IT p.180]. Flag
            # loudly; charge nothing.
            self.stall("fp-no-emms-FLAG", 0)
            self.log.append(f"{e:6d} !! FP without prior EMMS (data-garbage warning): {ins.raw}")
        self.mode = "fpu"
        self.fp_dirty = True
        unit = row.extra.get("unit", "fpu")
        lat = ins.row.clocks
        if unit == "fdiv":
            lat = self.ctx.fd_precision
        start = max(e, self.fp_next)
        if unit == "fdiv":
            if self.fdiv_free > start:
                self.stall("fdiv-wait", self.fdiv_free - start)
                start = self.fdiv_free
        if unit == "fmul" and self.fmul_last >= 0 and start < self.fmul_last + 2:
            self.stall("fmul-bubble", self.fmul_last + 2 - start)
            start = self.fmul_last + 2
        # operand readiness (fp stack positions)
        need = start
        for p in ins.fp_rd:
            if p < len(self.stack):
                need = max(need, self.stack[p])
        if need > start:
            self.stall("fp-operand", need - start)
            start = need
        if row.extra.get("need_early"):
            # [IT m]: FST(P)/MMX-store data needed one clock early
            src_rdy = self.stack[0] if self.stack else 0
            if src_rdy >= start:
                self.stall("store-early", src_rdy - start + 1)
                start = src_rdy + 1
        if unit == "fmul":
            self.fmul_last = start
        if unit == "fdiv":
            self.fdiv_free = start + lat
            self.fdiv_intmul_free = start + lat
        noov = row.extra.get("no_overlap")
        fo = 0 if noov else row.extra.get("fp_ov", 0)
        self.fp_next = start + (lat if noov else max(1, lat - fo))
        # stack effects
        if ins.fp_push:
            self.stack.insert(0, start + lat)
            if len(self.stack) > 8:
                self.stall("fp-stack-overflow")
                self.stack.pop()
        for p in ins.fp_wr:
            if p < len(self.stack):
                self.stack[p] = start + lat
        for _ in range(ins.fp_pop):
            if self.stack:
                self.stack.pop(0)
        if ins.mne in ("fcom", "fcomp", "fcompp", "fucom", "fucomp", "fucompp", "ftst"):
            self.flags_ready = start + lat
            self.ready["flags"] = self.flags_ready
        # integer slot freeing: FST(P)-mem EX2 cannot overlap subsequent [MA]
        occupy = 2 if noov else 1
        if ins.mne in FP_PAIRABLE_U:
            self.last_fp_u_issue = start
        self.log.append(f"{start:6d} U  {ins.raw}   [fp start={start} ready={start + lat}]")
        self.memops += len(ins.mem_r) + len(ins.mem_w)
        self.bytes_rw += sum(max(1, m.size // 8) for m in list(ins.mem_r) + list(ins.mem_w))
        return start + occupy, start

    def issue_fxch(self, ins: Ins, e: int, it: int, idx: int) -> int:
        """[MA 4.5]: FXCH pairs in V with an FP_U-class issue in the SAME
        cycle (cost 0); standalone takes 1; imperfect +1 if the instruction
        after a paired FXCH is not FP."""
        _, k = ins.fp_rd
        if 0 < k < len(self.stack):
            self.stack[0], self.stack[k] = self.stack[k], self.stack[0]
        # FXCH pairs into the V slot of the SAME cycle as the FP_U instr
        # that just issued; self.c was already advanced past that cycle.
        if self.last_fp_u_issue == self.c - 1:
            self.fxch_pairs += 1
            nxt = self.ins[idx + 1] if idx + 1 < len(self.ins) - 1 else None
            self.log.append(f"{self.last_fp_u_issue:6d} V  {ins.raw}  [paired]")
            if nxt is not None and nxt.mne not in FPU:
                self.stall("fxch-imperfect")
                self.imperfect += 1
                return self.c + 1
            return self.c
        self.log.append(f"{e:6d} U  {ins.raw}")
        return e + 1

    def issue_int(self, ins: Ins, e: int, it: int, occupy: int | None = None) -> int:
        row = ins.row
        unit = row.extra.get("unit")
        if unit in ("imul", "idiv"):
            if self.fdiv_intmul_free > e:
                self.stall("imul-under-fdiv", self.fdiv_intmul_free - e)
                e = self.fdiv_intmul_free
        # row.clocks already encodes rm/rmw classes ([IT] 2/3) and multi-cycle
        # np instructions (cdq=2, ret=2, imul=9, ...).
        oc = occupy if occupy is not None else row.clocks
        finish = e + oc
        for w, lat in ins.wr:
            self.ready[w] = e + max(oc, lat)
        if ins.row.extra.get("flags") in ("w", "rw"):
            self.flags_ready = finish
        if ins.mne in ("push", "pop", "call", "ret"):
            self.ready["esp"] = finish
            self.esp_stackop = ins.mne != "ret" or True
            self.esp_stackop = True
        elif any(w == "esp" for w, _ in ins.wr):
            self.esp_stackop = False
        if unit in ("imul", "idiv"):
            self.blocked_until = max(self.blocked_until, finish)
        if row.extra.get("branch") == "jcc":
            pass
        self.count_mem(ins)
        self.log.append(f"{e:6d} U  {ins.raw}" + (f"   [{oc}clk]" if oc > 1 else ""))
        return finish

    def issue_mmx(self, ins: Ins, e: int, it: int, in_v=False) -> int:
        row = ins.row
        if ins.mne == "emms":
            self.mode = "mmx_clean"
            self.fp_dirty = False
        elif self.mode == "fpu" and self.fp_dirty:
            self.transitions["mmx_after_fp"] += 1
            self.stall("mmx-after-fp", MMX_AFTER_FP)
            e += MMX_AFTER_FP
            self.mode = "mmx_dirty"
        elif self.mode != "mmx_clean":
            self.mode = "mmx_dirty"
        lat = row.extra.get("latency", 1)
        need = e
        for r in ins.mm_rd:
            need = max(need, self.mmready.get(r, 0))
        for m in list(ins.mem_r) + list(ins.mem_w):
            for rr in (m.base, m.index):
                need = max(need, self.rdy(rr))
        if row.extra.get("need_early"):
            src = next((r for r in ins.mm_rd), None)
            if src is None:
                src = next((o.reg for o in ins.ops if o.kind == "mmreg"), None)
            rdy = self.mmready.get(src, 0) if src else 0
            if rdy >= need:
                self.stall("mmx-store-early", rdy - need + 1)
                need = rdy + 1
        if row.extra.get("unit") == "mmmul":
            if self.mmmul_free > need:
                self.stall("mmmul-wait", self.mmmul_free - need)
                need = self.mmmul_free
            self.mmmul_free = need + 1
        finish = need + 1
        for w in ins.mm_wr:
            self.mmready[w] = need + lat
        for w, lat2 in ins.wr:
            self.ready[w] = finish
        for m in list(ins.mem_r) + list(ins.mem_w):
            self.memops += 1
            self.bytes_rw += max(1, m.size // 8)
        self.stores += len(ins.mem_w)
        if row.extra.get("transition") == "mmx_exit":
            pass
        self.log.append(f"{need:6d} {'V' if in_v else 'U'}  {ins.raw}")
        return finish

    def intdeps_at(self, ins: Ins, at: int) -> int:
        """Cycle at which ins's integer-side deps are satisfied."""
        dep = at
        for r in ins.rd:
            dep = max(dep, self.flags_ready if r == "flags" else self.rdy(r))
        return dep

    def mmdeps_at(self, ins: Ins, at: int) -> int:
        dep = at
        for r in ins.mm_rd:
            dep = max(dep, self.mmready.get(r, 0))
        if ins.row.extra.get("unit") == "mmmul":
            dep = max(dep, self.mmmul_free)
        if ins.row.extra.get("need_early"):
            src = next((o.reg for o in ins.ops if o.kind == "mmreg"), None)
            if src is not None:
                rdy = self.mmready.get(src, 0)
                if rdy >= dep:
                    dep = rdy + 1
        return dep

    def pair_gate(self, u: Ins, v: Ins, e: int, slot_v: int) -> tuple[bool, str]:
        if slot_v > e or self.blocked_until > e:
            return False, "not-decoded-or-blocked"
        # mode transition inside a pair: fall back to singles (approx area)
        if (u.mne in MMX or v.mne in MMX) and self.mode == "fpu" and self.fp_dirty:
            return False, "mmx-transition"
        ok, why = self.can_pair(u, v)
        if not ok:
            return False, why
        for x, is_v in ((u, False), (v, True)):
            dep = self.intdeps_at(x, e)
            dep = self.mmdeps_at(x, dep)
            if dep > e:
                # flag-fusion: v=jcc may read flags the U instr writes [MA r5]
                if is_v and x.rd and all(r == "flags" for r in x.rd) and u.row.extra.get("flags") in ("w", "rw"):
                    continue
                return False, f"dep:{why if False else ''}"
        return True, "ok"

    def issue_pair(self, u: Ins, v: Ins, e: int, it: int) -> int:
        oc = pair_occupy(self, u, v, e)
        finish = e + oc
        for x in (u, v):
            for w, lat in x.wr:
                self.ready[w] = e + max(oc, lat)
            if x.row.extra.get("flags") in ("w", "rw"):
                self.flags_ready = finish
            if x.mne in ("push", "pop", "call", "ret"):
                self.ready["esp"] = finish
                self.esp_stackop = True
            elif any(w == "esp" for w, _ in x.wr):
                self.esp_stackop = False
            if x.mne in MMX:
                lat_ = x.row.extra.get("latency", 1)
                for wm in x.mm_wr:
                    self.mmready[wm] = e + lat_
                if x.row.extra.get("unit") == "mmmul":
                    self.mmmul_free = e + 1
                if x.mne == "emms":
                    self.mode = "mmx_clean"
                    self.fp_dirty = False
                elif self.mode != "mmx_clean":
                    self.mode = "mmx_dirty"
            self.count_mem(x)
        self.log.append(f"{e:6d} U  {u.raw}\n{e:6d} V  {v.raw}" + (f"  [x{oc}]" if oc > 1 else ""))
        return finish

    def run(self, iterations: int = 12):
        n = len(self.ins)
        deltas = []
        for it in range(iterations):
            start_of_iter = self.c
            i = 0
            while i < n:
                ins = self.ins[i]
                slot = self.avail[i] + it * self.period
                e = max(self.c, slot)
                if slot > self.c:
                    self.stall("decode", slot - self.c)
                if self.blocked_until > e:
                    self.stall("serial-block", self.blocked_until - e)
                    e = self.blocked_until
                if ins.mne in FPU and ins.mne != "fxch":
                    e2, _start = self.issue_fp(ins, e, it)
                    self.c = max(self.c, e2)
                    i += 1
                    continue
                if ins.mne == "fxch":
                    self.c = max(self.c, self.issue_fxch(ins, e, it, i))
                    i += 1
                    continue
                # AGI for anything address-bearing (int, MMX, lea) [MA 4.2]
                if (ins.mem_r or ins.mem_w or ins.mne == "lea") and self.agi_check(ins, e):
                    self.agi += 1
                    self.stall("agi")
                    e += 1
                # resolve U's own stalls BEFORE choosing a pair partner, so
                # the partner is evaluated at U's true issue cycle
                if ins.mne in MMX:
                    mmdep = self.mmdeps_at(ins, e)
                    if mmdep > e:
                        self.stall("mmx-dep", mmdep - e)
                        e = mmdep
                else:
                    dep = self.intdeps_at(ins, e)
                    if dep > e:
                        self.stall("dep", dep - e)
                        e = dep
                nxt = self.ins[i + 1] if i + 1 < n else None
                if nxt is not None and not (nxt.mne in FPU and nxt.mne != "fxch"):
                    slot_v = self.avail[i + 1] + it * self.period
                    ok, _why = self.pair_gate(ins, nxt, e, slot_v)
                    if ok:
                        self.c = max(self.c, self.issue_pair(ins, nxt, e, it))
                        i += 2
                        continue
                if ins.mne in MMX:
                    self.c = max(self.c, self.issue_mmx(ins, e, it))
                    i += 1
                    continue
                self.c = max(self.c, self.issue_int(ins, e, it))
                i += 1
            deltas.append(self.c - start_of_iter)
        steady = deltas[len(deltas) // 2:]
        return {"per_iter": steady, "all": deltas}


def pair_occupy(sim: "Sim", u: Ins, v: Ins, e: int) -> int:
    """Clocks the U/V pair occupies: [MA table 4.1] classes + imperfect cases."""
    def klass(x):
        if x.mne in MMX:
            return 1
        return 2 if x.row.extra.get("rm") else 3 if x.row.extra.get("rmw") else 1
    matrix = {1: {1: 1, 2: 2, 3: 3}, 2: {1: 2, 2: 2, 3: 3}, 3: {1: 3, 2: 4, 3: 5}}
    oc = matrix[klass(u)][klass(v)]
    um = list(u.mem_r) + list(u.mem_w)
    vm = list(v.mem_r) + list(v.mem_w)
    if um and vm:
        # compare the primary data access of each instruction
        conf = bank_conflict(um[0], vm[0], sim.ctx.deltas)
        if conf is None:
            sim.bank_unknown += 1
        elif conf:
            sim.bank_conflicts += 1
            oc = max(oc, 2)
    if sim.agi_check(v, e):
        oc += 1
        sim.imperfect += 1
        sim.stall("agi-in-pair")
    return oc


# ==========================================================================
# Analysis driver
# ==========================================================================

def analyze_slice(instrs: list[Ins], ctx: Ctx | None = None, iterations: int = 12) -> dict:
    sim = Sim(instrs, ctx or Ctx())
    res = sim.run(iterations=iterations)
    steady = res["per_iter"]
    cpi = steady[len(steady) // 2]
    iters = max(1, iterations)
    return {
        "cycles_per_iter": cpi,
        "spread": max(steady) - min(steady),
        "steady_series": steady,
        "stats": {
            "stalls": sim.stalls,
            "agi": sim.agi / iters,
            "bank_conflicts": sim.bank_conflicts / iters,
            "bank_unknown": sim.bank_unknown / iters,
            "imperfect_pairs": sim.imperfect / iters,
            "memops_per_iter": sim.memops / iters,
            "bytes_rw_per_iter": sim.bytes_rw / iters,
            "stores_per_iter": sim.stores / iters,
            "misalign_risk": sim.misalign_risk / iters,
            "fp_after_emms": sim.transitions["fp_after_emms"],
            "mmx_after_fp": sim.transitions["mmx_after_fp"],
            "fmul_bubbles": sim.fmul_bubbles,
        },
        "log": sim.log,
    }


# ==========================================================================
# Selftest — every expectation hand-derived against the cited rules.
# All bodies end in a backedge `jnz` (predicted taken, 1 clock, may pair).
# ==========================================================================

SELFTESTS = [
    # name, asm body (excluding backedge), expected steady-state clocks/iter, basis
    ("ex4.4 same-dword imperfect pair",
     "mov eax, [esi]\nmov ebx, [esi]\ninc ecx",
     3, "MA 4.4: bank-conflict pair 2 + inc/jnz pair 1"),
    ("ex4.7a AGI",
     "add ebx, 4\nmov eax, [ebx]",
     3, "MA 4.7a: add 1 + AGI 1 + mov/jnz pair 1 (jcc pairs with any V-pairable u)"),
    ("ex4.7b AGI avoided by pairing",
     "mov eax, [ebx+4]\nadd ebx, 4",
     2, "MA 4.7b: pair 1 + jnz(pairs w/add) 1"),
    ("ex4.3 rm-then-rmw",
     "add ebx, [mem2]\nadd [mem1], eax",
     4, "MA table 4.1: pair 2,3 -> 3 + jnz 1"),
    ("ex4.3 rmw-then-rm",
     "add [mem1], eax\nadd ebx, [mem2]",
     5, "MA table 4.1: pair 3,2 -> 4 + jnz 1"),
    ("ex4.10b fully split",
     "mov ecx, [mem1]\nmov edx, [mem2]\nadd ecx, eax\nadd edx, ebx\nmov [mem1], ecx\nmov [mem2], edx",
     4, "MA 4.10b: 3 perfect pairs + jnz 1"),
    ("ex4.15 four pipelined fadd",
     "fadd st1, st0\nfadd st2, st0\nfadd st3, st0\nfadd st4, st0",
     5, "MA 4.15: issue 1/clk, deps resolve across iterations; +jnz"),
    ("ex4.16a fmul bubbles",
     "fld qword [a1]\nfld qword [b1]\nfld qword [c1]\nfxch st2\n"
     "fmul qword [a2]\nfxch st1\nfmul qword [b2]\nfxch st2\n"
     "fmul qword [c2]\nfxch st1\nfstp qword [a3]\nfxch st1\n"
     "fstp qword [b3]\nfstp qword [c3]",
     16, "MA 4.16a prints 14 but its cycle assignment is self-consistent only "
        "under fmul-spacing=1 for 16a vs note (n) of [IT] (overlap=1 when the "
        "next op is also FMUL => spacing 2); we implement [IT] note (n): "
        "16 (+1 jnz). Relative claim retains: 16a >> 16b. DOCUMENTED DEVIATION"),
    ("ex4.16b scheduled fmul",
     "fld qword [a1]\nfmul qword [a2]\nfld qword [b1]\nfmul qword [b2]\n"
     "fld qword [c1]\nfmul qword [c2]\nfxch st2\nfstp qword [a3]\n"
     "fstp qword [b3]\nfstp qword [c3]",
     13, "MA 4.16b publishes 12 for the FP body; +1 backedge"),
    ("imul serializes integer pipes",
     "imul eax, ebx\nadd ecx, edx",
     10, "IT: imul r,r = 9 np; then add/jnz pair 1"),
    ("cmp/jcc flag-fusion pair",
     "dec ecx",
     1, "MA rule 5: dec (flags) pairs with jnz backedge => 1"),
    ("mmx mul 3clk result, 1/clk issue",
     "pmaddwd mm0, mm1\npmaddwd mm2, mm3\npaddd mm4, mm0",
     4, "IT: mul/mul no pair; issue 0,1; mm0 ready@3 -> paddd@3; jnz pairs => 4"),
    ("mmx mem op pairs with mmx (U-only)",
     "movq mm0, [esi]\npaddw mm2, mm3",
     2, "MA rule10: mem-MMX is U-only but PAIRS with MMX; pair 1 + jnz 1"),
    ("emms then back to mmx: no transition cost",
     "paddw mm0, mm1\nemms\npaddw mm2, mm3",
     3, "IT p.180: EMMS 1clk, FP<->MMX costs only on crossing w/o EMMS; 1+1+jnz-pair"),
    ("fp->mmx transition penalized",
     "fadd st1, st0\nmovq mm0, [esi]",
     41, "IT p.180: per-iter fp<->mmx ping-pong: 1+38+movq 1+jnz 1 = 41 steady"),
    ("cdq split comparison",
     "cdq",
     3, "IT: cdq 2 np + jnz 1"),
]

KERNEL_BASELINE = {}


def selftest(verbose=False) -> int:
    failures = 0
    for name, body, expect, basis in SELFTESTS:
        instrs = parse_asm(body + "\njnz loop_head\nloop_head:")
        try:
            res = analyze_slice(instrs)
        except (KeyError, ValueError) as err:
            print(f"SELFTEST-FAIL {name}: {err}")
            failures += 1
            continue
        got = res["cycles_per_iter"]
        ok = got == expect
        print(f"{'ok  ' if ok else 'FAIL'} {name}: got {got} want {expect}  [{basis}]")
        if not ok:
            failures += 1
            if verbose:
                for ln in res["log"]:
                    print("   ", ln)
    print(f"pm-mca selftest: {'FAIL (' + str(failures) + ')' if failures else 'PASS'}")
    return 1 if failures else 0


# ==========================================================================
# CLI
# ==========================================================================

# --------------------------------------------------------------------------
# Loop discovery: clean single-backedge inner loops over the whole binary
# --------------------------------------------------------------------------

def scan_loops(dump_text: str, name_filter: re.Pattern | None, limit: int):
    funcs = parse_objdump_symbols(dump_text)
    rows = []
    skipped = {"ctl": 0, "unknown": 0, "big": 0}
    for sym, instrs in funcs.items():
        if name_filter and not name_filter.search(sym):
            continue
        # index jcc-backedges
        for i, ins in enumerate(instrs):
            if not ins.mne.startswith(("jz", "jnz", "ja", "jb", "je", "jl", "jg",
                                       "jae", "jbe", "jge", "jle", "jna", "jnae",
                                       "jnb", "jnbe", "jnc", "jne", "jng", "jnge",
                                       "jnl", "jnle", "jo", "jp", "jpe", "jpo",
                                       "js", "jns", "jc", "loop")):
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
            inner_cti = [x for x in body[:-1] if x.mne in
                         ("jmp", "call", "ret", "loop", "int", "leave", "iret")]
            if inner_cti:
                skipped["ctl"] += 1
                continue
            try:
                res = analyze_slice(body, Ctx(), iterations=10)
            except (KeyError, ValueError):
                skipped["unknown"] += 1
                continue
            st = res["stats"]
            rows.append((sym, body[0].addr, body[-1].addr, len(body),
                         res["cycles_per_iter"], res["spread"],
                         round(st["memops_per_iter"], 1),
                         round(st["bytes_rw_per_iter"], 0),
                         st["stalls"] if st["stalls"] else {}))
    return rows, skipped


# --------------------------------------------------------------------------
# report: manifest-driven analysis of the whole build -> JSON + METRIC lines
# --------------------------------------------------------------------------

def db_hash() -> str:
    import hashlib
    here = Path(__file__).resolve().parent
    h = hashlib.sha256()
    for name in ("pm_mca.py", "pm_mca_data.py"):
        h.update((here / name).read_bytes())
    return f"{h.hexdigest()[:12]}-{VERSION}"


def resolve_loop_slice(funcs, loop: dict) -> list[Ins]:
    sym = loop["symbol"]
    if sym not in funcs:
        raise KeyError(f"symbol {sym!r} absent from this build")
    instrs = funcs[sym]
    rx = re.compile(loop["contains"])
    hits = [i for i, x in enumerate(instrs) if rx.search(x.raw)]
    if len(hits) != 1:
        raise KeyError(f"{loop['name']}: anchor matched {len(hits)}x in {sym} (need 1)")
    a = hits[0]
    best = None
    for i in range(a, min(len(instrs), a + 400)):
        ins = instrs[i]
        if not (ins.mne.startswith("j") and ins.mne != "jmp"):
            continue
        if not ins.ops or ins.ops[0].kind != "imm":
            continue
        mt = re.match(r"^(0x[0-9a-f]+|[0-9a-f]+)", ins.ops[0].text.strip())
        if not mt:
            continue
        t = int(mt.group(1), 16)
        if ins.addr is None or t > ins.addr or t > instrs[a].addr:
            continue
        start = next((j for j, x in enumerate(instrs) if x.addr == t), None)
        if start is None or start > a:
            continue
        best = (start, i)
        break
    if best is None:
        raise KeyError(f"{loop['name']}: no enclosing backward-jcc loop found")
    body = instrs[best[0]:best[1] + 1]
    if any(x.mne in ("jmp", "call", "ret", "loop", "int", "leave", "iret")
           for x in body[:-1]):
        raise ValueError(f"{loop['name']}: slice has inner control flow (v1 scope)")
    return body


def read_evidence(path: Path) -> dict:
    ev = {}
    for line in path.read_text().splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            ev[k.strip()] = v.strip()
    return ev


def cmd_report(args) -> int:
    def argval(flag, default=None):
        return args[args.index(flag) + 1] if flag in args else default
    dump_path = argval("--objdump") or argval("--dump")
    exe = argval("--exe")
    if dump_path is None:
        exep = Path(exe)
        cache = exep.parent.parent / "pmmca" / (exep.stem.lower() + ".dump")
        cache.parent.mkdir(parents=True, exist_ok=True)
        if not cache.exists() or cache.stat().st_mtime < exep.stat().st_mtime:
            objdump = find_djgpp_objdump()
            text_d = subprocess.run(objdump + ["-d", "-Mintel", "-j", ".text", str(exep)],
                                    check=True, capture_output=True, text=True).stdout
            cache.write_text(text_d)
        dump_path = str(cache)
    manifest = json.loads(Path(argval("--manifest", "scripts/pm_mca_manifest.json")).read_text())
    ev = read_evidence(Path(argval("--evidence"))) if argval("--evidence") else {}
    frames = int(ev.get("frames", "48") or 48)
    funcs = parse_objdump_symbols(Path(dump_path).read_text(errors="replace"))
    defaults = manifest.get("defaults", {})
    out = {"version": VERSION, "db": db_hash(), "frames": frames, "loops": {}}
    total = 0.0
    problems = []
    for loop in manifest["loops"]:
        name = loop["name"]
        try:
            body = resolve_loop_slice(funcs, loop)
            ctx = Ctx(align={**defaults.get("align", {}), **loop.get("align", {})},
                      deltas=loop.get("deltas", {}),
                      fd_precision=loop.get("fd_precision", defaults.get("fd_precision", 33)))
            res = analyze_slice(body, ctx)
        except (KeyError, ValueError) as err:
            problems.append(f"{name}: {err}")
            continue
        trips = float(ev.get(loop["trips_key"], 0) or 0) / max(1, frames)
        c = res["cycles_per_iter"]
        est = c * trips
        total += est
        st = res["stats"]
        out["loops"][name] = {
            "cpi": c, "trips": trips, "est_cycles": round(est, 1),
            "n_instr": len(body),
            "stalls": {k: round(v / 12, 2) for k, v in st["stalls"].items()},
            "bank_unknown": st["bank_unknown"], "mem_ops": st["memops_per_iter"],
            "bytes_rw": st["bytes_rw_per_iter"],
        }
    out["total_cycles"] = round(total, 0)
    out["est_us_pmmx233"] = round(total / 233.0, 0)  # cycles per frame / 233 MHz -> us
    out["problems"] = problems
    if argval("--json"):
        Path(argval("--json")).write_text(json.dumps(out, indent=1) + "\n")
    # METRIC lines for harness consumption
    print(f"METRIC pmca_total_cycles={int(out['total_cycles'])}", flush=True)
    print(f"METRIC pmca_est_us={int(out['est_us_pmmx233'])}", flush=True)
    print(f"METRIC pmca_loops_ok={len(out['loops'])}", flush=True)
    for name, d in out["loops"].items():
        print(f"METRIC pmca_{name}_cpi={int(d['cpi'])}", flush=True)
    # deterministic verdict vs baseline JSON
    base = argval("--baseline")
    if base and Path(base).exists():
        old = json.loads(Path(base).read_text())
        if old.get("db") != out["db"]:
            print("PMCA-VERDICT=REBASELINE (db/analyzer changed)")
            return 43
        o, n_ = old["total_cycles"], out["total_cycles"]
        delta = n_ - o
        print(f"PMCA-DELTA {delta:+.0f} cycles ({o} -> {n_})")
        for name in out["loops"]:
            if name in old.get("loops", {}):
                dd = out["loops"][name]["cpi"] - old["loops"][name]["cpi"]
                if dd:
                    print(f"PMCA-LOOP {name}: cpi {old['loops'][name]['cpi']} -> {out['loops'][name]['cpi']} ({dd:+d})")
        if delta > 0:
            print("PMCA-VERDICT=REGRESS")
            return 42
        print("PMCA-VERDICT=OK")
    for p in problems:
        print(f"PMCA-WARN {p}", flush=True)
    return 0


def main(argv: list[str]) -> int:
    if len(argv) < 2 or argv[1] not in ("analyze", "selftest", "scan", "report"):
        print(__doc__, file=sys.stderr)
        return 2
    if argv[1] == "selftest":
        return selftest(verbose="-v" in argv)
    args = argv[2:]
    if argv[1] == "report":
        return cmd_report(args)

    def argval(flag, default=None):
        return args[args.index(flag) + 1] if flag in args else default

    dump_path = argval("--objdump")
    if dump_path is None and argval("--exe"):
        exe = Path(argval("--exe"))
        cache = exe.parent.parent / "pmmca" / (exe.stem.lower() + ".dump")
        cache.parent.mkdir(parents=True, exist_ok=True)
        if not cache.exists() or cache.stat().st_mtime < exe.stat().st_mtime:
            objdump = find_djgpp_objdump()
            text = subprocess.run(
                objdump + ["-d", "-Mintel", "-j", ".text", str(exe)],
                check=True, capture_output=True, text=True).stdout
            cache.write_text(text)
        dump_path = str(cache)
    if dump_path is None:
        print("need --objdump FILE or --exe SILVER.EXE", file=sys.stderr)
        return 2
    text = Path(dump_path).read_text(errors="replace")

    if argv[1] == "scan":
        pat = re.compile(argval("--match", "."))
        rows, skipped = scan_loops(text, pat, int(argval("--limit", "40")))
        for row in rows:
            sym, a0, a1, n, cpi, spread, memops, bytesrw, stalls = row
            print(f"{sym:42s} {a0:08x}-{a1:08x} n={n:3d} cpi={cpi:4d} "
                  f"spread={spread} mem={memops:5} bytes={bytesrw:6} {stalls}")
        print(f"skipped: {skipped}", file=sys.stderr)
        return 0

    # analyze --objdump/--exe --symbol SYM [--start RE] [--end RE] [--json]
    sym = argval("--symbol")
    if not sym:
        print("analyze needs --symbol", file=sys.stderr)
        return 2
    funcs = parse_objdump_symbols(text)
    if sym not in funcs:
        print(f"symbol {sym!r} not found", file=sys.stderr)
        return 2
    instrs = funcs[sym]
    start_i, end_i = 0, len(instrs) - 1
    if "--start" in args:
        rx = re.compile(argval("--start"))
        hits = [i for i, x in enumerate(instrs) if rx.search(x.raw)]
        if len(hits) != 1:
            print(f"--start anchor matched {len(hits)} times (need exactly 1)", file=sys.stderr)
            return 2
        start_i = hits[0]
    if "--end" in args:
        rx = re.compile(argval("--end"))
        hits = [i for i, x in enumerate(instrs) if i >= start_i and rx.search(x.raw)]
        if not hits:
            print("--end anchor matched 0 times", file=sys.stderr)
            return 2
        end_i = hits[0]
    body = instrs[start_i:end_i + 1]
    try:
        res = analyze_slice(body, Ctx())
    except (KeyError, ValueError) as err:
        print(f"slice rejected: {err}", file=sys.stderr)
        return 2
    out = {"symbol": sym, "n_instr": len(body), **{k: v for k, v in res.items() if k != "log"}}
    print(json.dumps(out, indent=1))
    if "--log" in args:
        for ln in res["log"]:
            print(ln)
    return 0


def find_djgpp_objdump() -> list[str]:
    nixpkgs = os.environ.get("NIXPKGS", "github:NixOS/nixpkgs/f2b17eb1b90f6a359082035b4538d4c63017aec3")
    return ["nix", "shell", f"{nixpkgs}#djgpp_i586", "-c", "i586-pc-msdosdjgpp-objdump"]


if __name__ == "__main__":
    sys.exit(main(sys.argv))
