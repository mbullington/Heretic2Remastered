# Autoresearch: DOS combat renderer

## Active Segment 19: retail player frame-order correctness

Commit `a53e866e` made the retail Corvus visible and replaced the incompatible
185-frame watcher model with the 1,433-frame `models/player/elf/tris.fm` asset.
Interactive testing then proved that frame count alone was insufficient: game
code emits the 1,423 logical indices in `m_player.h`, while the retail FM stores
the same normal animations in another order. Logical frame 1 is `180turn1`,
but retail physical frame 1 is `4swim1`; `180turn1` is physical frame 908.

### Objective and frozen workload

Build and validate a retail-asset adapter that maps logical player frame names
to physical FM frame indices. Apply it consistently to current, old, swap,
skeleton-placement, and reference-placement indexing. The workload remains the
corrected-Corvus combat sweep, but its player pixels intentionally change to
the semantically correct animation. This starts a new non-comparable segment.

### Metric contract

- **Primary correctness metric**: all automated capture gates pass with Corvus
  using name-mapped retail frames; performance telemetry remains observational
  until the corrected capture becomes the new baseline.
- **Hard constraints**: no frozen-pose workaround, animation-state exception,
  game protocol change, workload shortcut, temporal shortcut, or PAK mutation;
  unsupported retail animations must fall back visibly and deterministically
  instead of indexing an unrelated pose.
- **Secondary guardrails**: P55C-compatible build, no missing player model,
  current/old/swap/skeleton/reference indices all share one mapping, generated
  frame-name data matches `m_player.h`, and entity/frame timings do not show an
  unexplained regression.
- **Interactive acceptance**: after automated gates, test idle, running,
  turning, weapon use, damage, and death in DOSBox Staging. Do not call player
  animation correct until this manual sequence passes.

The ruler separates the bug directly: only 1 of the first 1,423 logical indices
already names the same physical frame, while 1,351 names can be matched exactly
elsewhere in the retail model. The old direct indexing therefore cannot be
semantically correct even though a single screenshot is non-T-posed.

### Stop conditions

Stop after a clean automated baseline plus interactive animation acceptance,
on a reproducible model-format incompatibility, or on user interruption. Do
not resume representation optimization until Corvus animation correctness is
established.

## Segment 18: 2x entity architecture without mesh LOD

Source baseline is commit `47004d9e`: retained world material patching, inline
skin sampling, and default 5x5 microtriangle masks. The frozen combat frame is
114.988 ms, with entities 28.232 ms, flex 19.783 ms, and brush entities 5.092
ms.

### Objective and Frozen Workload

Reduce the complete entity surface to `entities_us <= 14,116` (2x versus the
Segment-18 source) without real mesh LOD. Attack the measured setup-dominated
architecture in this order: (1) a complete default affine opaque-flex polyset
path, (2) an exact z-tested brush span assembly sibling, (3) early visible
triangle command streams, and (4) remaining sprite/entity classes if needed.
The workload remains the frozen Segment-16/17 tutorial2 combat sweep: planted
gorgon + rat_giant, `angermonsters`, stationary player, deterministic effects,
and a fresh 7.5-degree camera orbit every measured frame.

### Metric Contract

- **Primary**: `entities_us` (microseconds/frame, lower is better).
- **Secondary decision metrics**: `frame_us`, `ent_flex_us`, `flex_tris_us`,
  `ent_brush_us`, `ent_sprite_us`, flex triangle/span/block/pixel counters, all
  other existing pools, and P55C static telemetry.
- **Hard functional guards**: combat `status=PASS`; P55C/Pentium-MMX-compatible
  instructions only; no gameplay, workload, resolution, temporal, benchmark,
  model-count, or cadence shortcuts; clipped/translucent/debug/unusual exact
  fallbacks remain.
- **Unaffected-output guard**: EFFECTS remains under its historical 0.06% of
  pixels over 2 LSB; RENDER and non-flex UI/world captures remain exact unless
  their differences are solely caused by accepted flex-model pixels.
- **Flex quality guards**: FLEXOLD/FLEXMID/FLEXNEW/FLEXSKEL/COMBAT each remain
  at or below 2.0% whole-frame pixels over 2 LSB and at or above 32 dB RGB
  PSNR versus exact Segment-16 captures. Inspect accepted paths for silhouette
  holes, depth leaks, texture boiling, and objectionable lighting banding.
- Accepted quality paths are default-on. Exact fallbacks may remain for near
  clipping, translucency, unusual materials, and diagnosis.
- Real mesh LOD is explicitly out of scope for this segment.

The ruler separates realistic cases: the same frozen workload reports 28.232
ms entities with normal winding culling, while disabling that production cull
previously raised entities by about 14.3 ms. The current 18.990 ms flex
triangle pool processes only about 5,765 attempted pixels/frame through 1,937
spans and 1,473 blocks, proving setup rather than fill is the target. Harness,
workload, captures, and quality limits freeze after the new baseline; the
additional emitted metrics expose already-existing counters only.

### Stop Conditions

Stop at `entities_us <= 14,116`, after the listed exact/affine mechanisms and
two genuinely lower-work representations are measured, after three consecutive
discarded distinct representation-level mechanisms, on user interruption, or
at a tooling/measurement dead end. Following the user's explicit pivot, stop
building local sprite/span variants: the remaining flex path must process an
order of magnitude fewer raster primitives. Real mesh LOD remains out of scope;
surfel and 2.5D impostor representations are in scope.

## Segment 17: stable-key generated/patched kernels

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
