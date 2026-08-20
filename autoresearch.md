# Autoresearch: exact-output PMMX world-renderer performance

## Segment 13: fixed-ABI world paint kernel

This is a new, non-comparable segment based on validated source commit
`8e2d8e3d`. The preceding temporal-quality campaign is closed at 1.515381%
bad pixels with exact per-pixel rotational mapping retained.

## Objective and Workload
Minimize Pentium-MMX-233 cost of the DOS RGB565 software renderer while
preserving exact output. The frozen workload is the deterministic tutorial2
combat scene: planted gorgon + rat_giant, `angermonsters`, a stationary player,
7.5-degree camera orbit, and fixed renderer-boundary effects. Temporal reuse is
present in the product but inactive in this workload because 7.5 degrees
exceeds its 3-degree scheduler gate, so every measured combat frame is fresh.

The dominant pool at the source baseline is the opaque front-to-back,
power-of-two world span lane. Its measured class frequencies are frozen from
the deterministic 144-frame combat evidence. `scripts/pm_mca_pathclass.py`
prices exact instruction windows with the P55C scheduler and those frequencies.

## Metric Contract
- **Primary**: `pmca_world_cycles` (cycles/frame, lower is better) — weighted
  P55C cycles for the dominant opaque FTB power-of-two world paint lane. The
  candidate replaces only the uncovered paint-loop fragments; covered runs and
  block setup retain identical frozen C prices. Call/dispatch cost is outside
  the static slice and must be paid honestly in the DOSBox guardrail.
- **Hard correctness guardrails**: combat `status=PASS`; RENDER.PPM and
  EFFECTS.PPM satisfy the existing <=0.06% pixels-over-2-LSB gates.
- **Performance guardrails**: combat `frame_us` and `world_us` must not regress
  outside their established deterministic/noise class. PMMX savings may be
  kept when the static ruler improves and DOSBox is neutral or better.
- **Secondary telemetry**: `pmca_est_us`, manifest loop CPIs, `frame_us`,
  `world_us`, `entities_us`, `blend_us`, particles/lights/present/clear, and
  world/flex pixel counters.

Before baseline, ruler sensitivity must separate the real compiler paint path
from at least one contrasting realistic instruction schedule. After baseline,
class frequencies, instruction extraction, workload, and metrics are frozen.

## How to Run
`./autoresearch.sh` incrementally builds SILVER.EXE, runs pm-mca plus the
world path-class ruler, then executes the deterministic combat workload and
prints `METRIC` lines. `autoresearch.checks.sh` regenerates and gates renderer
proof images after every passing run.

## Files in Scope
- `dos/ref_soft/ref_soft_raster.c` — dominant world paint path and fixed-ABI
  inline assembly kernel
- `dos/ref_soft/ref_soft_local.h`, `ref_soft_public.h` — renderer-local ABI and
  telemetry only when required
- `scripts/pm_mca.py`, `scripts/pm_mca_data.py`,
  `scripts/pm_mca_pathclass.py`, `scripts/pm_mca_manifest.json` — P55C ruler
- `autoresearch.sh`, `autoresearch.checks.sh` — frozen harness and hard gates
- `autoresearch.md`, `autoresearch.ideas.md` — segment contract and untried ideas

## Off Limits
- Gameplay, BSP/model loading semantics, networking, sound, PAKs, vendors, and
  toolchains
- Pixel-gate loosening, workload/camera changes, counter-frequency changes, or
  benchmark-specific output shortcuts
- Changing the fallback/fan world paths or relying on Pentium-II-only ISA
- Temporal cadence changes as a way to improve this fresh-frame workload

## Hard Constraints
- C99/DJGPP build with `-Wall -Werror`
- P55C/Pentium-MMX-compatible instructions only
- No UB, races, self-modifying-code dependency, or benchmark detection
- Keep a C reference/fallback path for A/B and unusual/debug cases
- Exact RGB565/depth/coverage semantics on the gated workloads

## Stop Conditions
Stop at `pmca_world_cycles <= 50%` of the segment baseline, when combined
combat frame time reaches <=70 ms, after 3 consecutive discarded kernel
mechanisms with no distinct scheduling strategy left, on user interruption,
or at a tooling/measurement dead end.
