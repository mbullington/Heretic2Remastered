# Autoresearch: DOS combat renderer

## Active Segment 17: stable-key generated/patched kernels

Source baseline is commit `4b677d7b`: default cached 5x5 flex masks plus the
retained world and BlendRect wins. The frozen combat frame is 115.971 ms, with
world 70.245 ms and entities 28.471 ms.

### Objective and Frozen Workload

Implement and measure, in order, (1) a world paint kernel specialized for
stable material constants, then (2) a whole-flex kernel specialized for stable
skin constants. The workload remains the Segment-16 tutorial2 combat sweep:
planted gorgon + rat_giant, `angermonsters`, stationary player, deterministic
effects, and a fresh 7.5-degree camera orbit every measured frame.

### Metric Contract

- **Primary**: `frame_us` (microseconds/frame, lower is better).
- **Secondary decision metrics**: `world_us` for the material kernel,
  `entities_us` and flex telemetry for the skin kernel, plus all existing pool
  and P55C static metrics.
- **Hard functional guards**: combat `status=PASS`; P55C/Pentium-MMX-compatible
  instructions only; no gameplay, workload, resolution, temporal, or benchmark
  shortcuts; exact/debug and clipped/translucent/unusual fallbacks remain.
- **Output guards**: preserve the Segment-16 EFFECTS exact budget and its
  RENDER/FLEXOLD/FLEXMID/FLEXNEW/FLEXSKEL/COMBAT quality limits. World material
  specialization must be pixel/depth exact. Flex specialization may retain the
  already accepted default 5x5 mask behavior but may not widen its quality
  budget.
- **Code generation guard**: patch only stable material/skin addresses and
  dimensions, never per-span interpolation, framebuffer positions, light-cell
  rows, or workload-derived values. Generated or patched code must use a fixed
  documented ABI and deterministic patch-site table.

The ruler separates realistic cases: the retained world assembly previously
moved world 92.0 -> 70.2 ms, while the flex path remains about 20 ms of the
28.5 ms entity pool. The Segment-16 census proves amortization (2,630 world
pixels/material key and 238 flex source triangles/skin key). The PMCA baseline
is rebased to `4b677d7b` because the retained alternate flex path changed an old
snippet that does not model its eliminated work. Harness, workload, captures,
metrics, and gates freeze after the new baseline.

### Stop Conditions

Run the world material mechanism through full DOSBox and quality checks before
starting flex. Then run the whole-flex per-skin mechanism. Stop after both are
kept or rejected, after two implementation-level failures of one mechanism,
on user interruption, or at a tooling/measurement dead end.

## Segment 16: default-quality entity architecture

Source baseline is commit `9fcb0901`: exact flex rasterization plus the retained
world and BlendRect wins. The user authorized deliberate flex-model pixel
changes when they do not look bad, and requires an accepted fast path to be
enabled by default rather than opt-in. World, effects, UI, gameplay, and model
semantics remain unchanged.

### Objective and Frozen Workload

Reduce the frozen combat workload's complete entity pool from about 28.8 ms to
14.4 ms (2x), with 9.6 ms as the 3x stretch target. The workload remains
tutorial2 with planted gorgon + rat_giant, `angermonsters`, stationary player,
7.5-degree camera orbit, and deterministic renderer-boundary effects.

### Metric Contract

- **Primary**: `entities_us` (microseconds/frame, lower is better).
- **Secondary**: `frame_us`, `flex_prologue_us`, flex triangle/span/block/pixel
  counters, world/blend/particles pools, and P55C static telemetry.
- **Hard functional guards**: combat `status=PASS`; P55C/Pentium-MMX-compatible
  instructions; no gameplay, workload, resolution, temporal, or benchmark
  shortcuts; exact C fallback retained for clipped/translucent/unusual paths.
- **Unaffected-output guard**: EFFECTS.PPM stays under the historical 0.06% of
  pixels over 2 LSB.
- **Flex quality guards**: RENDER, FLEXOLD, FLEXMID, FLEXNEW, FLEXSKEL, and the
  frozen combat capture each stay at or below 2.0% whole-frame pixels over
  2 LSB and at or above 32 dB RGB PSNR versus exact Segment-16 captures.
  Accepted changes must also pass direct visual inspection for silhouette
  holes, depth leaks, texture swimming/boiling, and objectionable lighting
  banding. These numerical limits are rejection backstops, not substitutes for
  visual inspection.
- An accepted renderer is default-on. An environment/cvar escape hatch may
  preserve the exact path for diagnosis, but is not the shipping default.

The ruler separates realistic cases: the exact source reports about 28.8 ms
entities / 20.4 ms flex, while the earlier RGB-affine flex experiment moved the
entity metric by 12.1 ms and visibly changed 210 gate pixels. The six-image
quality suite records that contrast while covering frame interpolation,
skeletal deformation, general rendering, and the actual combat view. Harness,
workload, captures, metrics, and quality limits freeze after the baseline.

### Segment-16 Scope and Stop Conditions

First census visible triangle max-edge and bounding-box classes, then compare
two distinct tiny-triangle architectures: Quake-style recursive subdivision
and precomputed microtriangle coverage masks. Stop after both are measured and
visually compared, at `entities_us <= 14,400`, after both mechanisms fail the
quality/performance contract, on user interruption, or at a tooling dead end.

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
