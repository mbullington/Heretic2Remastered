# Next experiment ideas (ranked)

Untried hypotheses only — prune when attempted. History, falsifications, and
learnings live in autoresearch.jsonl and git history (runs #1-#95).

STATE 2026-08-20 (SEG-7 combat workload; pmca_est_us=527, frame 184.2ms):
- Session #86-#95: frame 264.1 -> 184.2ms (-30.3%). Pools now: world 95.4 /
  entities 43.5 / blend 40.5 [SYNTHETIC-INFLATED: fxload damage-blend pulse
  per frame; gameplay priority = world > entities] / particles 3.5 / lights
  0.15 / present 0.31 / sky 0.17 (us).
- HARNESS FACT (runs #91-92): draw_face_spans is PERTURBATION-FRAGILE on this
  host (ARM DOSBox dynarec): value-neutral C additions (counters, bool decls,
  call sites) deterministically move gate pixels (129px class) + inflate
  world_us up to +24%. Known-safe edit classes there: (a) E17-class
  float-chain-identical rewrites, (b) pure-integer changes (E19c passed).
  Everything else = per-build gate lottery.
- pm-mca v1.5 (run #96): inner-jcc=nottaken flag + scripts/pm_mca_pathclass.py
  price dominant-path WINDOWS cut from the objdump per class, weighted by
  measured trips: the world ftb-pow2 lane now HAS a per-class PMMX price map
  (paint 129 clk/px AGI-dominated, prologue 408clk/block fp-stall dominated).
- LEDGER CORRECTION (2026-08-20): E50 flex signed-area backface cull (KEPT
  seg-5 @50ac58df: entities -9%, flex px 33,173->27,383, gate-PASS = tutorial2
  flex content single-sided in-view) was SILENTLY REMOVED by the SEG-6
  baseline rewrite df1a0562 (pickaxe -S area2: cull added 50ac58df, gone by
  df1a0562; NO falsification record, no gate vote). Current flex path has NO
  winding test anywhere (span_raster_flex_triangle/_opaque reject only
  fabsf(denom)<1e-4).
- 10x CAMPAIGN verdict (2026-08-20; user: blend/entities -> 1/10th each):
  blend REACHABLE keep-class (B1 exact-MMX + B2 present-fusion =>
  ~1.5-3.5ms); entities keep-class stack ~= 1.6-1.7x (43.5 -> ~25-30ms),
  +arbitration package ~= 4-5x (~8-11ms), true 4.35ms = turbo-class
  fidelity renegotiation. Combat anatomy: 2,078 tris/f walked (922 die on
  the in-rasterizer bbox AFTER projection, 260 clip), 23.2K flex px, 2,217
  lerps, 794 proj fills => setup-dominated small-tri regime (Abrash/Galvan
  class); gradient chains 74/68 cpi PMMX remain the biggest measured units.

## Runnable keep-class queue
- B1 MMX 4-wide exact blend (2026-08-20; biggest single lever in tree):
  every step bit-exact: (d>>8)&0xF8 -> PSRLW+PAND; n = s*aw + d*dw is a
  convex combination <= 63,240 => fits PMULLW low16, no 16-bit overflow;
  (n*0x8081)>>23 == PSRLW(PMULHUW(n,0x8081),7) by shift composition.
  Sweep-verify 65,536 dst x 256 alpha offline (#86 contract). ~27 MMX
  ops/4px => ~4-7 billed cyc/px vs ~63 now => blend 40.5 -> ~1.5-3.5ms.
  EMMS once per call (blend is a leaf; last op of the frame path => the
  ~50clk FP<->MMX transition, Fog 19.2, is negligible). Build/run proven:
  W6 MMX compiled and ran (run #38 failed PIXELS in the world z-loop, not
  the environment); blend has no z / no float = cleanest MMX fit in the
  renderer. Ruler: out-of-manifest, DOSBox-scored like keeps #86/#87/#88.
  Ref: Mesa mmx_blend.S (PMULLW/PMULHUW 4-wide blend era-proven on PMMX;
  theirs approximates /255, ours is exact).
- B2 present-fused blend (pass elimination): fold fullscreen fd->blend
  into E1's direct-FB present copy (read once, blend, write LFB): the
  standalone RMW pass and its 153KB/frame traffic vanish; cost = ALU in
  the copy loop. SEMANTICS GUARD: UI (SW_DrawChar/Pic/Fill, crosshair)
  draws AFTER the blend and must NOT be tinted => pending-tint + flush-
  on-UI-write fallback (one global flag in ~4 UI draw paths, NOT in
  PutPixel). Census F first. Stack B1's MMX kernel in the copy loop =>
  blend ~= +1-2ms over present alone. Sibling-mode bonus: INDEX8 320x200
  bakes tint into the 64KB present LUT rebuild (blend = free there).
- E50-RESTORE flex winding cull, census-first (queued 2026-08-20;
  banked entities -9% precedent, per LEDGER CORRECTION):
  (1) per-mesh-node winding census (front/back/degenerate x node flags);
      sprites + any census-proven two-sided nodes are carved OUT of the cull;
  (2) restore signed-area<=0 skip at the E50 site (post-proj-cache,
      pre-color-construction), env-gated for A/B + user-eyes.
  Flex path is NOT the perturbation-fragile function (E9/E55c/E58 landed
  fine there). E50 evidence was QUIET tutorial2; SEG-7 combat is flex-heavy
  (~640 tris/f) => bigger pool, but new content => census (1) IS the gate
  discipline, not optional. E50-era measured: entities -9%, flex px -17.5%.
- B3 BlendRect per-call channel maps (rmap[32]/gmap[64]/bmap[32], queued
  2026-08-20; scalar fallback/derisk for B1, ~2.5-3x): src color/alpha are constant per call => map dst 5/6/5 bits
  straight to out bits; 128 entries (~256B L1-resident) replace the current
  6 IMUL/px form (src-weight hoist + exact 0x8081 div @ ref_soft_framebuffer.c:341)
  with 3 loads. Bit-exact by construction (build maps FROM current semantics;
  sweep-verify all 65536 dst inputs for the call's params against the replaced
  path). Evidence: blend loop NOT in pmca manifest; inner loop is branchless
  (mag += setcc-class) so add a manifest entry + T0-price FIRST — run #5
  falsified big-LUT memory-for-ALU on this ruler class (128KB light LUT,
  +8.8ms); 256B resident is a different regime, but price it, don't trust
  DOSBox to show IMUL removal. Pool = blend 40.5ms (synthetic-inflated,
  see STATE).
- E-B white-light chain collapse per tri (E17-class bitwise predicate):
  corner r==g==b bitwise => 3 Gouraud chains -> 1 intensity chain: -2
  gradient chains/tri, -4 endpoint FMUL + -4 int-conversions + -2 deltas
  per block, -2 adds/px + shade-row sharing with E-A. Census E first
  (colored dlights may break whiteness on the planted monsters). Est
  -3..6ms.
- E-A exact universal flex shade LUT (#97 pattern carried to flex):
  mr=(tr*cr+128)>>8 with ~65KB tables (513 cr-rows x 32/64/32 channel
  cols); cr = the lerped Gouraud value itself => NO light key (kills
  #80's rebuild storm) and NO quantization (kills #80's fidelity fail;
  exact by construction, #97-class sweep-verify). Per-px: 3 IMUL + ~9 ops
  -> 3 loads; per-tri cr range tiny => hot rows L1-resident. Est -3..5ms.
- E-D clamp/colorkey elimination: per-SPAN s/t in-range proof (u = uz/wz
  monotonic on a span => block-endpoint checks replace 2 clamps/px) +
  per-skin has_zero_texel flag (drops the texel!=0 test when the skin
  has none). Bit-exact (branch-only). Est -1..2ms.
- E-B2 mesh-node screen-bbox pre-cull: 922/2,078 walked tris die on the
  in-rasterizer bbox test AFTER corner projection; project per-node bounds
  once per entity => skip walk + proj fills of doomed limbs. Bit-exact
  (skips outputless work). Est -1..2ms.
- E-E proj-fill remainder: verify project_flex shares ONE 1/w per vertex
  (else 2 FDIV -> 1: -39clk x 794/f) + SoA batch of the 2,217 lerps/f.
  Est ~1ms.
- E-F MMX flex px engine [STRETCH, do last]: 4-wide shade+store over
  z-pass runs (scalar gather + vector shade via the B1 identities; z
  divergence needs run masks; flex loop is NOT the fragile function).
  ~1.5-2x on the px component only.
- Census bundle B/C/D (2026-08-20; evidence only — gates the parked items
  below, no experiments until data lands):
  B flex projected-area histogram (<0.25/0.5/1/2/4/8/>=8 px) => sizes the
    LOD/impostor pools (arbitration) AND the microtri-setup-waste class;
  C flex overdraw = shadeable frags / unique final flex px => gates the
    flex visibility-buffer (deferred texturing; mechanism = cheap edge/Z
    integer work vs expensive Gouraud-projective px);
  D world texture/light face switches + visible-span counts/lengths per
    face, collected at EMIT SITES ONLY (counters inside draw_face_spans =
    fragility class) => ranks deferred material-order shading AND provides
    the faces/frame patch-amortization evidence for the asm era.
  E per-tri whiteness share: fraction of painted flex tris with corner
    r==g==b bitwise in combat (colored dlights may kill it) => gates E-B.
  F post-blend color writes/frame (UI/crosshair after fd->blend) + fxload
    pulse alpha==0 frame share => gates B2's fusion shape (and B4 minor).
  G combat-mode flex spans/blocks per frame (h2dos_main metrics print, in
    scope; T3 prints them, combat doesn't) => sizes E-C/E-F block tax.
(Everything else = the gate classes below.)

## Gated: needs ruler/harness engineering (PMMX-class pools both rulers miss)
Unlock: pm-mca v2 (inner-branch support) OR hand-factored clean-slice loop
constructs for the world span/block prologue. Otherwise these have NO
evidence path (DOSBox class already answered: x87 costs are invisible there):
- #13 world span-prologue reciprocal work [POOL PRICED #96: prologue+advance =
  437clk/block = 1.44Mcyc/frame ~5.8us @233; the 2 FDIVs are a MINORITY -
  fistp-QWORD ikey casts + IDIV + 4 fldcw dances share it] => #13 alone is
  the WRONG shape; the real item is prologue conversion-tax (#21a steps in).
- #21(a) known-mode cast elimination [VARIANT-PRICED #97: bareFISTP -92clk/blk
  (~-1.3us/f), +ikeyDWORD +0 (model-neutral), +recipLUT -142 (~-2us/f @233).
  ALL variants = DRIFT-class (RN vs chop for sign-general sites; ikey QWORD is
  defensive near INT_MAX) + draw_face-fragile => arbitration/deep-freeze.
- ~~#22 565 shade-row LUT~~ DONE (run #97): pm-mca predicted -9.3us@233;
  MEASURED -3,375us world on DOSBox TOO (-3.5%; hot rows + no init-check beat
  the run #5 cold-LUT class on BOTH rulers). Bit-exact, gates 0-diff.
- #19(a) light-cell const runs (8,738 crossings/frame census): kills ~6
  ops/painted-px in the world lane; confined to the FRAGILE function anyway
  => same manifest/lane gate.
- E-C flex block-prologue conversion tax: 2 FDIV + ~14 fistp-class
  conversions per block; no bit-exact C rewrite exists (float chain).
  Unlock = pm-mca manifest entry + pathclass map for the flex block loops
  (like #96's world price map), then drift-class variants => arbitration
  (same family as #21a/b). Census G sizes the pool first.

## Gated: bench-invisible (combat orbit forces real frames)
VERIFIED 2026-08-20 in code: SEG-7 orbit yaws 7.5deg/frame (h2dos_main.c:1105)
> the E61 scheduler's 3.0deg gate (ref_soft_raster.c:4384) => warp cadence is
ZERO in the combat bench; translation is likewise forced-real. ANY temporal
mechanism (warp polish, phase-2) prices at 0 until a calm-camera second scene
exists OR the user arbitrates building blind.
- CALM-RULER UNLOCK (queued 2026-08-20, user-approved): promote T3 (OLD static
  tutorial2 scene, scripts/run-dos-benchmark.py) from robustness check to
  gate-relevant calm telemetry for the temporal class; combat stays the scalar
  primary, T3 reports the calm vector. ONE scene only — a walk/look/idle suite
  quadruples the ~90s x 3 verify. Unblocks #16, translation-warp,
  styled-snapshot.
- #16 temporal polish bundle: radius-2 dilation (holes ~18/frame), snapshot
  zb-16, warp block 8-vs-16, flex-only refresh when dynamic faces dominate.
- Static-camera identity fast path (was -25..40ms on the old static bench).

## Awaiting USER arbitration (evidence banked; do not rebuild)
- #21(b) FPCW chop + 24-bit PC at renderer entry/exit (the actual Quake
  trick): FDIV 33->19 clk. ULP-drift class (E42/E43-lane). pm_mca_data
  already prices it; needs your sign-off on drift risk.
- Translation-capable temporal warp [E61 phase-2; rotation-only today]:
  (a) depth-dependent backward map via snapshot z-key, or (b) per-face plane
  homographies (needs face-ID sidecar). Pool = every straight-line walk
  (banked rotation warp: -25.2ms e9b207f). Workload-blocked (above).
- Styled-face material snapshot / relight-after-warp: snapshot is baked
  RGB565+Z so styled faces are excluded en bloc today; enabler VERIFIED:
  #17 proves light_s/t == s/t per non-warp face (no new gradient chains).
  Same workload blocker.
- Flex LOD (offline 50/25/12.5% progressive meshes): collides with off-limits
  "BSP/model loading semantics" => derived-data pipeline = your call. Animated
  impostor cache: banked churn lesson (#23/#59: every-frame lightstyles) +
  per-frame pose churn = tiny reuse window. BOTH gated on census B first.
- rgb-affine flex turbo variant A: -12.1ms flex, GATE RED 210px (fidelity
  ceiling class; your call per data). 10x-campaign card: combat's tiny-tri
  regime makes the per-block reciprocal staircase THE setup tax; Quake-
  class 8/16px subdivision endpoint-solve kills the block machinery
  (Abrash ch.70: the 7.5clk/px mapper made exactly this trade). Siblings
  in the same package: integer/Pineda edge-equation setup (replaces float
  gradients; px-moving) and flat-shade CLUT (#80-class).
  10x arithmetic: keep stack ~1.7x (43.5 -> ~25-30ms); +this package
  ~= 4-5x (~8-11ms); true 4.35ms = fidelity-contract renegotiation.
- True mipmapping (loader reads mip0 only): output-changing but
  period-correct (Q1/Q2 software did it); BOTH rulers cache-blind =>
  real-iron measurement only. Cheaper sibling of Talisman subtree-quad
  caching (same class, more bookkeeping).

## Parked for real PMMX (both rulers blind to the memory system)
Need real-iron bench or pm-mca phase 2 (trace+cache sim): cache-set phase
sweeps (depth base 0..4095 step 32 x pitch pad 320..352 x light-arena phase),
MTRR/USWC for the VESA LFB, DPMI page lock/pre-touch, 32x8 scratchpad
microtiles, 16-bit z (run #3 falsified under DOSBox; 300KB->150KB footprint
is real-iron-only), tile rendering (Unreal '98 8x8), #18's FDIV-carry in asm.
- Deferred material-order shading (2026-08-20): keep FTB ownership (pass 1 =
  visibility/ownership spans; pass 2 = shade grouped by face/texture) =>
  gate-safe in principle (B1 closure was ORDER changes under painter
  semantics; ownership unchanged here). Payoff = cache-hot texture/light
  constants + per-class kernels; both rulers cache-blind (pm-mca: "cpi = L1
  view") => real-iron / phase-2 class. Ranking evidence = census D.
- PMMX asm paint-kernel era (user direction 2026-08-20): targets =
  pathclass-priced compiler artifacts (paint_nomove 129clk/px AGI-dominated;
  prologue+advance 437clk/block). PLAN: (1) ONE static fixed-ABI asm kernel
  for ftb-pow2 paint_nomove, C float frontend untouched (E19c-safe edit
  class at function granularity; expect one gate-lottery landing attempt),
  C path env-retained as gate reference; (2) every asm variant
  pathclass-priced BEFORE DOSBox; (3) THEN per-face template patching
  (base/masks/strides/light => immediates; freed registers = the actual AGI
  fix) from a malloc'd call-target (no DPMI alias games); P55C SMC flush
  per patch => census D faces/frame must prove amortization. Scalar first:
  per-px MMX = separable priced experiment (#38/#39 parity-tax was
  DOSBox-ruler screening; paint_nomove is AGI-bound, not ALU-bound).

## Screened / falsified (do not re-pitch; details in jsonl)
- Quake lit-surface caches: runs #23 (+730%), #59/E60 (rebuild storm,
  ceiling +2.5ms perfected). H2 animates lightstyles EVERY frame.
- CLUT intensity-shade flex (run #80): 396-429px >2LSB @128lv (8.6-9.4x
  budget) + dlight-keyed rebuild storm (41->164-210ms). 32-level = gate-red
  by construction. Removed from queue 2026-08-20 per user.
- B1/painter-family reorderings: E44/E47 gate-red on coplanar ties;
  B1.1+B1.2 perf-regress; world span level = architectural optimum.
- HZB/subtree occlusion (4 forms incl. cover-tile kills): pools bounded by
  E59.1 census (2.0K leftover px) + #92 (963 nodes walked ~0.1ms).
- Post-transform world VERTEX cache: +4.8ms (10-15% dedup only); edge-cache
  sibling same class. E58 packets already hoisted the float work.
- Per-px MMX (modulate/blend/particles): runs #38/#39 parity tax. movq-copy
  lane (#10) trigger-gated on copy domination (present now 310us: NOT met).
- Sparse-Z tiles: conflicts with default-ON temporal (warp needs world zb).
- Eager per-entity projection (run #93): fills move counters, wash.
- Endpoint carry in C (run #90: +521us liveness; PMMX-asm class only).
- Deferred additive compositing: 0 additive px in combat (run #88 census).
- Entity-level occlusion/reject, client-effects governor, flex hierarchy
  composition: pools measured-absent (census era).
- No-clear ztrick: pool = clear_us ~0.8ms.
- 8-bit indexed renderer: structurally unfittable for H2 (2016 palettes;
  seg-4 close-out). 64KB 565->index LUT only as present-tax of the 320x200
  sibling mode (kept as env opt-in).
- Light multiply via big LUT (run #5: +8.8ms): memory ~ ALU on this ruler.
- Texel-row pointer (run #92 census: 29% crossing rate = DEAD).
- Coverage-before-prologue (17.6% full blocks = ~0.1ms pool: DEAD).
- Fixed-point edge DDAs (pool misidentified; coplanar crack class).
- Viewmodel foreground pre-cull (order flip + hole class).
- Full kinetic screen arrangement / frameless error heap (B1 ties + E61
  already ships the keep-class core), map-compiled visibility automaton.
- Rational integer DDA (quotient/remainder incremental perspective to delete
  blockwise FDIV machinery): ANALYTICALLY SCREENED 2026-08-20. Pathclass:
  prologue+advance = 437clk/block and the FDIVs are a MINORITY share
  (fistp-QWORD/IDIV/fldcw dances dominate) => at 128px blocks the FDIV share
  amortizes to <1clk/px while paint_nomove = 129clk/px: per-pixel
  quotient-correction costs more than the save, and data-dependent
  correction loops blow up exactly on steep-gradient faces (|dU|>W).
  Output-changing (arbitration-class) AND slower-in-expectation; do not
  re-pitch as speed (revisit only as a fidelity argument).
