# Autoresearch: 2x exact-output DOS combat renderer

## Segment 15: total-frame big swings

Source baseline is commit `4ad7a131`: validated exact temporal mapping plus the
fixed-ABI scalar PMMX world paint kernel. The kernel reduced combat world time
92.0 -> 71.8 ms and total frame time 166.3 -> 146.5 ms with all gates passing.

## Objective and Workload
Reach a 2x speedup over the validated post-temporal 166.3 ms combat frame while
preserving exact renderer output. The frozen workload is tutorial2 with planted
gorgon + rat_giant, `angermonsters`, stationary player, 7.5-degree camera orbit,
and deterministic renderer-boundary effects. Temporal reuse is inactive because
the orbit exceeds its 3-degree scheduler gate; every measured frame is fresh.

## Metric Contract
- **Primary**: `frame_us` (microseconds/frame, lower is better) from the frozen
  deterministic combat workload.
- **Hard correctness guardrails**: combat `status=PASS`; RENDER.PPM and
  EFFECTS.PPM satisfy the existing <=0.06% pixels-over-2-LSB gates.
- **Architecture guardrail**: P55C/Pentium-MMX-compatible instructions only;
  no Pentium-II ISA, UB, benchmark detection, or workload/gate changes.
- **Secondary guardrails**: no unexplained regression in world/entity pools or
  in `pmca_world_cycles`; exact C fallbacks remain for debug/unusual paths.
- **Secondary telemetry**: world/entities/blend/particles/lights/present/clear,
  pm-mca manifest CPIs, weighted world cycles, and pixel/work counters.

Ruler sensitivity is established by the source pools: blend specialization
previously measured 40.5 -> 9.2 ms on this exact workload class, and the new
world assembly kernel measured 92.0 -> 71.8 ms. Harness, workload, metrics, and
pixel gates freeze after the baseline.

## How to Run
`./autoresearch.sh` builds SILVER.EXE, prices PMMX loops and the world path,
then runs the frozen combat workload. `autoresearch.checks.sh` regenerates and
gates renderer proofs after each passing run.

## Files in Scope
- `dos/ref_soft/ref_soft_raster.c` — world/flex kernels and dispatch
- `dos/ref_soft/ref_soft_framebuffer.c` — exact blend/present kernels
- renderer-local headers for fixed ABI/telemetry only
- `scripts/pm_mca*.py`, manifest — static P55C telemetry only
- autoresearch contract, harness, checks, and ideas files

## Off Limits
Gameplay, BSP/model-loading semantics, networking, sound, PAKs, vendor data,
toolchains, workload/camera changes, gate loosening, resolution/format changes,
and temporal-cadence shortcuts.

## Hard Constraints
C99/DJGPP `-Wall -Werror`; P55C-compatible instructions; exact RGB565/depth/
coverage semantics on gated workloads; no races, UB, self-modifying-code
requirement, or benchmark-specific shortcuts.

## Stop Conditions
Stop at `frame_us <= 83,150` (2x vs the 166.3 ms validated source), after 3
consecutive discarded big-swing mechanisms with no distinct architecture left,
on user interruption, or at a tooling/measurement dead end.
