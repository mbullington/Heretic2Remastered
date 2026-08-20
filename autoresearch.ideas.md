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

## Runnable keep-class queue
- BlendRect per-call channel maps (rmap[32]/gmap[64]/bmap[32], queued
  2026-08-20): src color/alpha are constant per call => map dst 5/6/5 bits
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
(Everything else = the gate classes below.)

## Gated: needs ruler/harness engineering (PMMX-class pools both rulers miss)
Unlock: pm-mca v2 (inner-branch support) OR hand-factored clean-slice loop
constructs for the world span/block prologue. Otherwise these have NO
evidence path (DOSBox class already answered: x87 costs are invisible there):
- #13 world span-prologue reciprocal work [POOL PRICED #96: prologue+advance =
  437clk/block = 1.44Mcyc/frame ~5.8us @233; the 2 FDIVs are a MINORITY -
  fistp-QWORD ikey casts + IDIV + 4 fldcw dances share it] => #13 alone is
  the WRONG shape; the real item is prologue conversion-tax (#21a steps in).
- #21(a) known-mode cast elimination (double-bias / bare fistp) for the
  draw_face prologue [#96 PRICES the pool: ~5.8us/frame @233; variant slices
  go through pm_mca_pathclass.py before any C edit].
- #22 565 shade-row LUT [PRICED #96 per pathclass: 129->101 clk/px =
  -2.17Mcyc/frame ~-9.3us @233, AGI 24->60 but IMUL serialization was worse.
  CONFIRMED PMMX-class winner; DOSBox wash-or-worse expected (run #5 class),
  so decision = build-for-PMMX (needs real-iron or accept pm-mca verdict).]
- #19(a) light-cell const runs (8,738 crossings/frame census): kills ~6
  ops/painted-px in the world lane; confined to the FRAGILE function anyway
  => same manifest/lane gate.
- #22's sibling requirement: light-cell runs first (they provide the hot-row
  residency the LUT needs).

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
- rgb-affine flex turbo variant A: -12.1ms flex, GATE RED 210px (fidelity
  ceiling class; your call per data).
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
