# Next experiment ideas (ranked)

Untried hypotheses only — prune when attempted. History, falsifications, and
learnings live in autoresearch.jsonl; closed axes are enumerated there
(B1/painter family, lit-surface caches, HZB, quiescent-pool probes;
MMX/asm REOPENED 2026-08-18 under the pm-mca ruler proposal below).

Current best: total 145,221 (run #73, 4c79cc3): world 78.7 / entities 39.9
(flex-tris 33.4) / sky 8.9 / present 14.8 / clear 1.2.
Two pricings that shape this list:
- Temporal is default-ON: world px are ~1/4 of pre-E61 volume (33K/frame
  walked). Per-px world ideas bank ~1/4 of their old pool. Present and
  entities are paid EVERY frame — they are the best remaining lanes.
- Fidelity ceiling: any perturbation of a float interpolation chain dies at
  the 2LSB gate by construction (565->888 quantum = 8/255; E55/E56 red at
  210px = 4.6x budget). Assume turbo-class unless the chain is untouched.

## 0. Sidecar combat bench [build task; FIRST — prices everything downstream]
Runs before the queue below because its prices RESHAPE it: every remaining
agenda item either assumes camera calm (#1 identity restore, #5 scan2
follow-ups) or is unpriceable on tutorial2 spawn (the entire workload-
gated list), and E50's removal proved spawn-view validation is a real
correctness trap. Build a second deterministic scene as a pricing
instrument:
- Scripted, renderer-only (gameplay is unverified, src/ untouchable):
  placed monsters on animation timelines, scripted spell/projectile spawns
  carrying dlights, particle bursts; RNG already seeded (run #55 infra).
- Camera path with real translation+rotation (stresses temporal + the #1
  identity-restore camera-calm assumption).
- Map with corridors/pillars so entity occlusion actually exists.
- Gates: verify bit-stability (walked-px jitter precedent) before trusting
  PPM diffs; tutorial2 stays seg-8 primary — combat is a census sidecar;
  promote only if its pools prove bigger than tutorial2's remaining queue.
Downstream rule: no cap-3 relaxation, no workload-gated build, and no
"camera-calm sizing is final" claims (#1/#5) until this bench's first
numbers land.

## Harness/ruler: pm-mca static P55C loop analyzer [INFRA; ranks ahead of all queued lanes]

Ruler change, not an experiment. DOSBox cycles=fixed is a flat instruction
budget — blind to U/V pairing, AGI, load-use, FDIV serialization, 16B
fetch-window decode bandwidth, MMX shared mul/shifter, bank conflicts. With
hand-asm hot loops back in scope, the current ruler would score exactly the
work it cannot see. Build the analyzer FIRST; queued loop-level items get
repriced under it. ~80% of the full trace-driven sim's benefit for the asm
program at days-scale cost (full sim = weeks).

Design (trace-free; no emulator in the loop):
- Input: objdump -d -Mintel slice between marker symbols (ground truth incl
  gcc codegen seam) or .s directly for pure-asm loops.
- Instruction DB: PMMX integer+x87+MMX subset actually used (~150 rows):
  issue rules, latency/throughput, resource claims. Source: Agner PMMX
  tables cross-checked vs Intel appendix.
- Engine: cycle-stepped U/V issue; 16B-aligned fetch window, <=2 decode/cyc;
  full pairing ruleset; dep tracking (AGI, load-use, FDIV ~19cyc
  nonpipelined, FXCH rules); steady state = loop body unrolled x8, backedge
  BTB-predicted-taken.
- Data-dependent per-px branches (z-test etc) = declared frequencies from
  bench counters; report weighted cycles + spread. Never guess miss rates:
  flag misalign/line-cross/D-bank conflicts under DECLARED alignments only.
- Output: per-cycle U/V pairing view (the asm authoring tool) +
  mechanism-named JSON ASI (fetch_bound/agi/fdiv/bank/u_pressure). Win
  claims must name the mechanism term that moved (Goodhart tripwire).
- Metric when promoted (REQUIRES NEW SEGMENT): Sum(cycles/iter x trips/frame)
  with trip counts pinned from existing bench counters -> est PMMX-233
  core-us; un-analyzed residual tracked separately off the DOSBox trend.
  DOSBox PASS + pixel gates stay hard checks, untouched.

Validation gate before promotion: reproduce Agner's published PMMX pairing
examples/cycle counts + ~10-15 hand-computable kernels, stated error budget.
If it can't nail those it never becomes a ruler. Real-iron spot check
(P55CAL.EXE crowdsourcing) stays opportunistic, off critical path.

Honest coverage split: entities/flex (52us/tri, ~3ms px => instruction-window
bound), span engines, present traffic (sequential copy, addresses statically
known) = VISIBLE. Parked-list runtime memory effects (cache phase/pitch/arena
phase/microtiles/USWC-LFB) stay BLIND — pm-mca does not unpark them.

Extension, not competitor: the DB + pairing engine are phase 1 of the full
trace+cache sim, not throwaway. Trigger to build the rest: (a) parked
memory-lane ideas rank high enough to actually run, or (b) loops read at
pairing optimum but DOSBox trend stops moving = residual lives in the memory
system, which the static tool provably cannot see. Census-before-kill
discipline stands: analyzer scores cycles/iter; only counter-pinned trip
counts aim it at loops that dominate.

## Harness: staged rollout pipeline T0-T3 [BUILT 2026-08-19: SEG-7 live, baseline #75 pmca_est_us=625]

Shortest truthful path from candidate to verdict; cheap deterministic filters
kill early, expensive stages run rarely.

- T0 analyzer pre-filter (<1s, runs FIRST in autoresearch.sh, before any
  DOSBox run): verdict on Sum(cycles/iter x trips) over analyzed loops vs
  stored segment baseline. Deterministic => any regression is real. Scenario
  spread (declared branch freqs): if [best..worst] straddles baseline,
  ESCALATE to T1 instead of discarding. Exit-fast with distinct status +
  METRIC line so jsonl stays honest; bad asm ideas die in build-time + <1s.
- COVERAGE CHECK in front of T0: loop manifest (checked in) = symbols +
  trip-count counter mapping + declared branch freqs + alignment assumptions.
  Diff touching anything outside the manifest => T0 abstains, route straight
  to T1+T2. Never silently pass unanalyzed code.
- T1 correctness gate (1x DOSBox, ~30s): PASS + pixel gates are deterministic
  booleans; medians never needed. The ONLY mandatory DOSBox run for
  in-coverage asm rollouts => ~30s vs ~90s today.
- T2 3x-median trend run: ONLY (a) out-of-coverage changes (data layout,
  un-analyzed code), or (b) every Nth keep for trend-continuity sanity.
- T3 combat-scene holdout [DOES NOT EXIST YET — build task]: NOT per-rollout
  (hands back the saved time); run at keep-checkpoint cadence (every N keeps
  / user call). Scores robustness, not rank: regression > threshold =>
  review flag, not auto-discard. Primary metric stays the tutorial scene;
  making the combat scene SCORED = material workload change = new segment
  per contract.

Build task DONE 2026-08-19 (combat scene IS the SEG-7 production bench):
tutorial2 + planted gorgon+rat_giant (repositioned dos-side; engine spawn
cmd misplaces) + angermonsters + sweep + renderer-boundary fxload (64 typed
particles + 2 dlights + blend pulse/frame). Pools: particles 34ms / lights
14ms / blend 70ms / entities 41ms; counters + CLIENT.PPM bit-identical
across reruns. KNOWN GAP: angered natives never damage the player (probe:
only 1 SVF_MONSTER at 0,0,0; pathing to stationary player broken; damage
blend comes from fxload, not hits). FM loader rejects several original
monster models (imp etc.) — realdata model-coverage gap worth its own item.
T3 slot now = OLD static tutorial2 scene (scripts/run-dos-benchmark.py).

## Queued (ranked by expected total_us impact)

### 1. Static-camera identity fast path for temporal [bit-exact, est -25..40ms]
Run-64 ASI: warp kernel costs ~57ms per FAKE frame (warp + snapshot +
zb-zero) even when the camera barely moves; bench evidence says the calm band
is the common case (walked px 1.05M/32 frames, ~18 residual holes/frame).
But an identity (or subpixel) transform needs no warp: restore the world
snapshot (color+depth, post-sky) with two flat copies (~460KB ~ 2-3ms on the
ruler), then let the EXISTING selective-refresh pass handle dynamic faces.
World avg: (104 real + 3x57 fake)/4 ~ 79 -> (104 + 3x6)/4 ~ 30.
Bit-exact by construction: identity backward-map = same bytes, repair pass
and its tile culling unchanged, diff should stay 0 (identity-warp precedent
in run #66). Win attaches to camera calm (a real gameplay state: standing,
examining, menus-in-world), not to bench specifics.
Risks: sw_world_repair_pass flag discipline — the empty-mask bug ran a full
2nd world pass (193ms first attempt); keep cap-3 policy intact.
Follow-up for user arbitration: with bit-exact identity restore there is no
approximation drift, so cap-3's reason weakens for the identity case —
relaxing it (invalidate on motion signature / PVS-leaf change / dynamic-set
change) pushes sustained-calm world avg toward ~10-15ms.

### 2. Direct-framebuffer present path [bit-exact, est -8..12ms]
Run-36 decomposition: present's 14ms is the vendored driver's NORMAL path =
per-px SDL_BlitSurface ("not byte-bound": same 14ms at 8888 for 2x bytes) =
pure instruction waste on the flat ruler. The vendored SDL3 DOS driver ships
the fix: SDL_DOS_ALLOW_DIRECT_FRAMEBUFFER => one dosmemput sys-RAM->VRAM,
palette programmed only on change, no blit compositing. sw_color already
aliases the window surface (W5, run #37), so the blit is the entire remaining
present cost.
Do: set the hint before SDL_GetWindowSurface in the H2_DOS glimp block
(callsite-only; vendor code untouched).
Risks: banked path accepts tearing / no page flip (Quake-period-authentic);
multibank at 153.6KB@320x240; present_us is bimodal -> mode-matched medians.
Fold in: 320x200 INDEX8 sibling (present 9.0ms incl LUT -> likely ~4-5ms at
64KB copy) — the run #73 queued user quick-win; measure both in one session.

### 3. Flex "52us/tri phantom" census [measurement-only; decides the 33.4ms kill]
Flex = largest non-temporal-reducible cost: 33.4ms over 640 tris/frame = ~52
us/tri, while per-px work is only ~3ms of it. Runs #69/#33 banked "phantom
persists in proj-cache corner copies + per-tri gradient chains + span
prologues" but the split was never measured. ~30 lines of counters before
any further flex work; the attribution selects the kill:
- gradient chains dominate -> CLUT (#4) as designed
- scan-conversion rows dominate -> Pineda microtile resurfaces WITH a real
  pool number (was deferred on a ~3ms estimate)
- pose/proj cross-frame redundancy dominates -> DO NOT build the pose memo:
  md2 lerp changes pose every frame in real gameplay; bench-hit-rate would
  be an overfit. Record and pick the second-best axis instead.
Folded in from the 2026-08-19 hindsight review (same ~15-line counter pass):
- flex_proj_lookup cache fills/frame + fill cost: prices entity->view fusion
  (compose View x Entity once per entity; ~12 mul + 9 add vs current ~21 +
  19 per unique vertex, projection rows pre-scaled). Reassociates POSITION
  chains only: sub-ULP => expected <1px flips, NOT the E55/E56 color-chain
  death. Build only if census shows fills are a real slice.
- rotate_flex_clusters calls vs unique verts ratio (loader copies indices
  into ancestor lists, so expect 2-3x). Ceiling pre-pinned: the WHOLE posing
  prologue is 467us/frame, so this only matters if the rotate slice alone
  is >~300us.

### 4. Flex intensity-shade CLUT [turbo-class, est -4..8ms; keep possible at 64/128 levels]
PS1 CLUT trick; fits E55c exactly: per-vertex vcolor = base_light * intensity
is already hoisted, so lerping (r/z,g/z,b/z) == base_rgb * lerp(I/z) —
mathematically identical. Replace 3 rgb-over-z chains with ONE
intensity-over-z interpolant; per-px = shade[level][skin8[idx]]; kills 3
gradient sets, 6 per-block conversions, and 3x per-px clamp/IMUL/repack.
UV stays perspective-correct (unlike banked affine variant A). LUT 64KB at
128 levels; rebuild keyed on (quantized base light + style signature) ~0.3ms/
frame at animated styles. Needs flex skin index8 retained alongside 565.
Run AFTER census #3 confirms gradients are the pool.
Fidelity plan: sweep levels 16/32/64/128 on the synthetic flex fixture that
killed E55 variant A. 16 = gate-red by construction; 128 -> error ~1/888,
borderline-inside budget. Find the knee, then arbitrate keep vs turbo-bank.

### 5. r_swscan2 row-parity variant [turbo-class, ON est -5..9 vs columns' -4.3]
PS2 field-render trick. Existing stripe infra (#71/#72) alternates 2px column
pairs: ON = -4.3. Row parity additionally kills span-row prologues (4.7K
span-rows x 0.76us ~ 3.6ms, E57-v2 census) while halving real-frame px the
same. Predicate-level change on existing machinery; snapshot already samples
both phases. Known anti-synergy: sky row-skip dies under stripes (#72
learning). User-grant class mirrors scan2 (stipple-adjacent turbo).

### 6. World texel+light run census [measurement-only; decides a -1..3ms idea]
30-line histogram in the opaque pow2 lane: exact (texel-addr, lidx, z-state)
run lengths. If mean run >= ~2.5px, bulk-fill same-colour runs (skip texel
fetch + lidx + modulate on continuing px; store + zb stay per-px). W1 says
uniform runs may be rarer than intuition, and the post-temporal px pool is
thin — census first, build only if the histogram is favorable.

### 7. MMX movq bulk-copy probe [bit-exact, trigger-gated, est -1..3ms]
Axis boundary: PER-PX MMX math stays closed (run #38: flat-parity ruler turns
pair bookkeeping + asm-block fences into a +6.4ms tax; #39 killed the scalar
twin). movq COPY loops are different: they halve instruction count on the
flat ruler and are pure-copy bit-exact. Trigger condition: only priced once
#1 (identity restore = 2x460KB flat copies/fake frame) and/or #2 (direct
dosmemput present) land and copies dominate those costs. Form: __m64
builtins with -mmmx inside one copy function (no raw asm blocks = no
scheduler fences), single emms at function exit, alignment handled at
alloc. Do not reopen the per-px modulate axis here.

## Temporal polish backlog (small; bundle as one turbo-polish session)
- radius-2 dilation ONLY for >1px residual reveal cracks (holes ~18/frame now)
- snapshot zb-16 (halve snapshot + parity traffic)
- warp block tune 8 vs 16
- flex-only face refresh when dynamic faces dominate

## Awaiting user arbitration (evidence banked; do not rebuild)
- rgb-affine flex turbo variant A: -12.1ms flex (frame 157.1 vs 160.7), GATE
  RED 210px synthetic-fixture. CLUT (#4) is its higher-fidelity sibling and
  likely supersedes it at 64/128 levels.

## Screened out 2026-08-19 (H2 hindsight review; evidence-pinned, do not re-pitch)

- Flex hierarchy transform composition: the redundancy is REAL (verified:
  ref_soft_flex.c copies each vertex into ancestor cluster lists;
  rotate_flex_clusters re-transforms shared vertices once per ancestor; ref
  points walk the chain per ancestor) but the pool is measured dead —
  flex_prologue_us = 467us/frame (runs #64-73) covers ALL posing work for
  99 entities = 0.32% of total; the rotate slice is a fraction of that.
  3x on it = noise. DOS port already hoists per-entity trig into the proj
  cache. Counts-only confirmation folds into census #3 above.
- FMNI_NO_DRAW / demand-deform vertex_valid[] caching: attacks the same
  467us prologue; tutorial2 census has all 99 flex models on-screen, and
  per-vertex validity branches tax the hot tri path. Meshlet-cull variant
  stays workload-gated above.
- Client-effects fill-rate governor + PVS-driven effect-sim skip: correct
  diagnosis (Raven's postmortem: overdraw fixed effect-by-effect, never
  systemically), wrong scope — fx simulation lives in src/ (off-limits) and
  dynamic quality scaling is gate-red by construction. Revisit only as a
  user-arbitrated FEATURE, not a perf experiment.

## Workload-gated — UNBLOCKED 2026-08-19 by the combat bench
lights 3us->13.7ms, blend 3us->70.3ms, sprites/particles 0->34.6ms in the new
scene. Candidates: light-disc stamps, particle stamps, E65/E66 re-probe,
sprite-atlas RLE spans (PS1 transparent-pixel surgery). NOTE: blend_us=70ms
is a continuous-damage worst case; treat blend-path optimizations as
combat-conditioning tests, not average-play gains.
Unreal-98 cross-check (2026-08-18): its celebrated CPU lanes (colored dlights,
occlusion culling) are exactly these workload-gated lanes — no new items.
Fog: playerstate.fog_density arrives from src/, DOS ref_soft never consumes
it; implementing volumetrics ADDS px cost and would red seg-6 gates =>
documented non-goal for the perf campaign. From the Q2-lineage technique review (2026-08-18 msg):
- Entity-level occlusion/HZB reject, particle coarse-Z reject: tutorial2
  census says the pool does not exist here (pre-FTB z-rejects 5% of world
  px; overdraw 1.25x; all 99 flex models on-screen). Needs an
  occlusion-heavy scene to even price.
- Dynamic-light dirty-tile caches: E60 lit-surface lineage (ceiling +2.5ms
  even perfected; warp-class faces gate-red). Same combat-workload gate.
- Flex cluster/meshlet culling: normal-cone backface variant carries the
  E50 trap (seg-6 removed the winding cull: two-sided content vanished);
  frustum-cluster variant only pays partially-off-screen. Never in
  pose-memo form (md2 lerp runs every frame in real gameplay = bench
  overfit).
- Deferred additive compositing: accumulate additive fx into 8x8 tile
  per-channel accumulators; one fb READ+ADD+WRITE per tile instead of one
  per sprite. Legal because saturating nonneg addition commutes. In scope
  (blend path only; fx simulation stays in src/). Raven's postmortem names
  ~10x overdraw as the cost they never fixed systemically => the combat
  bench should price this lane directly.
- Approx-distance attenuation (max + 3/8 mid + 3/16 min, or rsqrt family)
  for dlight falloff: pm-mca prices the FSQRT/FDIV it replaces; the pool
  itself needs the combat bench (tutorial lights = 3us).

## Graduates when pm-mca is promoted (flat-ruler-invisible, analyzer-priced)

First T0-pre-filter candidate family: sqrt. normalize_checked pays serialized
FSQRT (70cyc) + FDIV (33cyc), both already modeled in pm_mca_data.py; it runs
~2x per flex joint per entity (flex_joint_rotation dir/up) plus sprite bases.
Call sites are in-scope and visual-only => Q_rsqrt + 1 Newton is a legal
variant (fidelity: joint-basis ULP moves every vertex ~1e-7 relative; same
sub-ULP position argument as the fusion note above, expect <1px). Also the
natural validation-kernel family for the analyzer's promotion gate:
hand-computable FDIV/FSQRT serialization + FXCH/pairing rules. Collision /
physics sqrt sites (none in ref_soft hot paths) stay exact.

## Parked for real PMMX (no evidence channel in the DOSBox flat ruler)
Unmeasurable, not falsified; need a real-iron bench program like texture
swizzle before them: cache-set phase sweeps (depth base 0..4095 step 32 x
pitch pad 320..352 x light-arena phase — 128-offset grid search was the msg-3
proposal), MTRR/USWC for the VESA LFB (run-38 ASI: SetMtrr docs already
banked), DPMI page lock/pre-touch (spike work, not median), 32x8 scratchpad
microtiles. Strongest parked axis: tile rendering — Unreal '98 shipped 8x8
tile CPU rasterization as its core cache strategy (Sweeney's retrospective
still endorses tiles for software rendering), i.e. third-party production
evidence the win is real and the current ruler simply can't see it. Extend
to tile domains beyond spans: snapshot store + temporal warp traversal.
