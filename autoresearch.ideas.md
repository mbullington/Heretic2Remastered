# Next experiment ideas (ranked)

Untried hypotheses only — prune when attempted. History, falsifications, and
learnings live in autoresearch.jsonl; closed axes are enumerated there
(B1/painter family, lit-surface caches, HZB, quiescent-pool probes).
MMX/asm axis REOPENED 2026-08-18; pm-mca ruler + staged harness + combat
bench are BUILT (SEG-7 baseline #75: pmca_est_us=625, 145,709 cycles).
2026-08-20: external GPT code-review triaged against jsonl evidence - 2 new
atoms admitted (#12 blend specialization, #13 manifest-gated reciprocal),
7 re-pitches screened out with run citations (see dated section below).

SEG-7 pricing frame (supersedes the static-tutorial one):
- Bench = tutorial2 combat (gorgon+rat_giant+angermonsters, orbiting camera,
  per-frame fxload of 64 typed particles + 2 dlights + blend pulse).
  frame 261.7 / world 101.1 / entities 40.8 / particles 34.6 / blend 70.3 /
  lights 13.8 / present 9.3 / sky 0.2 (µs, flat ruler).
- Primary ruler = pmca_est_us: deterministic PMMX-233 cycle estimate over
  manifest loops (3 now; grow the manifest as analysis pays).
- pmca baseline already says: flex gradient loops are FILD->FMUL serialized
  (89/79 clk/iter, 34/30 fp-operand stalls). That is the best-evidenced
  instruction-window target on record.
- blend_us=70ms is a continuous-damage worst case: blend-path ideas are
  combat-conditioning tests, not average-play gains.

## Queued (ranked by expected total_us impact)

### 4. Flex intensity-shade CLUT — PARKED turbo artifact (run #80)
FULL implementation in-tree, env-gated OFF (H2DOS_SW_CLUT=levels). Fidelity:
fixture sweep + production 396-429px >2LSB @128lv (8.6-9.4x budget, deltas
<=25) = not keep-class. Perf: (light x skin) key churn under animated dlights
=> rebuild storm (41->164-210ms); 1/2 light quantum insufficient. Premise
holds for STATIC-light content; revisit as user-arbitrated turbo toggle.
Off-cost: +0.8ms entities (block-specialized pixel loops; was +3.3ms), then
absorbed by #7.
PS1 CLUT trick; fits E55c exactly: per-vertex vcolor = base_light * intensity
is already hoisted, so lerping (r/z,g/z,b/z) == base_rgb * lerp(I/z) —
mathematically identical. Replace 3 rgb-over-z chains with ONE
intensity-over-z interpolant; per-px = shade[level][skin8[idx]]; kills 3
gradient sets, 6 per-block conversions, and 3x per-px clamp/IMUL/repack.
UV stays perspective-correct (unlike banked affine variant A). LUT 64KB at
128 levels; rebuild keyed on (quantized base light + style signature) ~0.3ms/
frame at animated styles. Needs flex skin index8 retained alongside 565.
Run AFTER #2 (same loops; sequence matters for attribution) and census #3.
Fidelity plan: sweep levels 16/32/64/128 on the synthetic flex fixture that
killed E55 variant A. 16 = gate-red by construction; 128 -> error ~1/888,
borderline-inside budget. Find the knee, then arbitrate keep vs turbo-bank.

### 12. SW_BlendRect full-screen specialization [bit-exact, est -20..40ms COMBAT-class]
GPT code-review re-derivation, verified in-tree: SW_BlendRect inlines the full
SW_PutPixel body per px (267-instr fn; objdump: env-init call, tagxy/zcollide
debug compares, float depth-key classify, z-test, 6 IMUL unpack + mul-hi /255).
Combat bench prices it at 70.3ms = 27% of frame (continuous-damage worst case,
NOT average play). Dedicated loop: row-ptr walk, hoisted env-init/clip, keep
bench_mag_putpixel counter, exact /255: (n*0x8081)>>23 == floor(n/255) for ALL
reachable numerators n<=63240 (565 channel masks cap sr,dr at 0xF8/0xFC).
Additive class (dlights) must NOT use the direct constant (n reaches 126,480 >
2^16 => off-by-one at 255-multiples): use the exact identity dr + (sr*a)/255
instead. RENDER gate includes the pulse (fxload injects it per frame) => keep
class requires true bit-exactness, not "close". Not in pm-mca manifest =>
T1/T2 DOSBox trend path; ruler credits it (instruction-count mechanism).

### 5. r_swscan2 row-parity variant [turbo-class, ON est -5..9 vs columns' -4.3]
PS2 field-render trick. Existing stripe infra (#71/#72) alternates 2px column
pairs: ON = -4.3. Row parity additionally kills span-row prologues (4.7K
span-rows x 0.76us ~ 3.6ms, E57-v2 census) while halving real-frame px the
same. Predicate-level change on existing machinery; snapshot already samples
both phases. Known anti-synergy: sky row-skip dies under stripes (#72
learning). User-grant class mirrors scan2 (stipple-adjacent turbo).

### 6. Eager per-entity projection pass [sub-ULP position, est -1..3ms + MMX-batch enabler]
From census #78: proj fills = 794/frame @4.7us = 3.7ms/frame, all scattered
lazily through the tri loop with per-corner seq-check branching. Project
render_num_xyz in ONE straight-line pass per entity (fills already exist as
flex_proj_lookup): kills the scatter/branch overhead, converts lazy-cache
checks into plain array reads, and produces the exact loop shape PMMX batch
projection needs later. Sub-ULP position argument (same math, new order);
expect <1px. Overlaps #7's win: it subsumes the scatter/branch part of #7.

### 8. normalize_checked rsqrt variant — DISCARDED by T0 vote (run #81)
Q_rsqrt+2NR correct in spirit (70+33 clk -> ~30) but the win site is not a
manifest loop; T0 voted REGRESS on +2cpi layout churn in the flex pods.
Ruler discipline says: do not retry until normalize sites are manifest-visible
or a churn threshold exists. The abstain-churn path (added after) reclassifies
such votes correctly now.
normalize_checked pays serialized FSQRT (70 cyc) + FDIV (33 cyc), modeled in
pm_mca_data.py; it runs ~2x per flex joint per entity (flex_joint_rotation
dir/up) plus sprite bases. Call sites in-scope, visual-only => Q_rsqrt + 1
Newton is a legal variant (joint-basis ULP moves vertices ~1e-7 relative;
sub-ULP position argument per the fusion note, expect <1px flip). Collision
/ physics sqrt sites stay exact. Natural asm-lane T0 citizen once LUT refs
exist in the manifest for those loops.

### 9. Proj-cache copy elimination (pointer API) [bit-exact, trivial, est -0.5..1.5]
From census #78 (80% hit rate): flex_proj_lookup returns *out = *cache =
~45B memcpy x 3,970 lookups/frame ~= 160KB/frame of pure copy for hits that
are read-only until the corner is consumed. Return a pointer to the slot
(or index) instead. Zero math change; mechanically small. Ship first as a
warm-up for the lane; #6/#7 change the surrounding shape anyway.

### 10. MMX movq bulk-copy probe [bit-exact, trigger-gated, est -1..3ms]
Axis boundary: PER-PX MMX math stays closed (run #38 parity tax; #39).
movq COPY loops differ: fewer instructions, pure-copy bit-exact, pm-mca can
now price them precisely (movq U-only-pair rules + store-early rule are
modeled). Trigger: once #1 (dosmemput present) lands and copies dominate.
Form: __m64 builtins with -mmmx inside one copy function (no raw asm blocks
= no scheduler fences), single emms at function exit, alignment at alloc.
Do not reopen the per-px modulate axis here.

### 11. World texel+light run census [measurement-only; decides a -1..3ms idea]

### 13. World span-prologue FDIV -> seed LUT + 1 Newton [MANIFEST-GATED first]
GPT-prompted; survives only in this form. pm-mca-legal target (FDIV = 39cyc
non-pipelined): 2 reciprocals per 128px block x ~6K blocks/frame ~ 0.5Mcyc ~
2ms PMMX-class. But: world span loop is NOT in the manifest (3 loops only),
and the #8/rsqrt precedent was T0-discard on layout churn for exactly this
visibility gap. Precondition = manifest grows an opaque-span entry; then
1-2K seed LUT + r*(2-w*r) NR, error budget derived from texel-movement, not
IEEE. Do not run as T1-only on the DOSBox ruler (x87-tax refuted there).

## Workload-gated lanes (combat bench now prices them)
- Deferred additive compositing: accumulate additive fx into 8x8 tile
  per-channel accumulators; one fb READ+ADD+WRITE per tile instead of one
  per sprite. Legal because saturating nonneg addition commutes. In scope
  (blend path only; fx simulation stays in src/). Raven's postmortem names
  ~10x overdraw as the cost they never fixed systemically => combat prices it.
- light-disc stamps (SW_RasterLights @3715: sqrtf + per-px PutPixel inside a
  square-domain walk; 13.7ms pool): precompute per-radius row extents (dx per
  row) + radial alpha keyed by d2 (or a pre-lit disc sprite) => row blit with
  z-test, no sqrt/reject/divide. Shares #12's exact-/255 multiply machinery for
  its additive class (dr + floor(sr*a/255) identity) => sequence after #12.
  Particle stamps, sprite-atlas RLE spans (PS1 transparent-pixel surgery),
  E65/E66 re-probe.
- Approx-distance attenuation (max + 3/8 mid + 3/16 min, or rsqrt family)
  for dlight falloff: pm-mca prices the FSQRT/FDIV it replaces; combat bench
  prices the pool (13.7ms).
- Fog: documented non-goal (adds px cost, would red gates).

## Temporal polish backlog (small; bundle as one turbo-polish session)
- radius-2 dilation ONLY for >1px residual reveal cracks (holes ~18/frame)
- snapshot zb-16 (halve snapshot + parity traffic)
- warp block tune 8 vs 16
- flex-only face refresh when dynamic faces dominate

## Awaiting user arbitration (evidence banked; do not rebuild)
- rgb-affine flex turbo variant A: -12.1ms flex (frame 157.1 vs 160.7), GATE
  RED 210px synthetic-fixture. CLUT (#4) is its higher-fidelity sibling and
  likely supersedes it at 64/128 levels.
- Static-camera identity fast path for temporal [was -25..40ms on static
  tutorial bench]: the combat bench ORBITS the camera every frame
  (H2DOS_SWEEP), so the calm band it attacks is invisible to the current
  bench. Real gameplay still has calm stand-and-look phases; needs either a
  calm-camera second scene or user arbitration to build anyway. Ranked out
  of the active queue 2026-08-19.

## Screened out 2026-08-20 (GPT external code-review re-pitches; evidence-pinned)
- Quake lit-surface cache (3rd re-pitch): runs #23 (+730% world) + #59/E60
  (gate red + regressed; ceiling +2.5ms perfected). H2 animates lightstyles
  EVERY frame => no amortization; 565 pow2-mask modulate3 ~= composite cost.
- BSP node frustum cull / subtree HZB: HZB closed with B1 (E47 gate signature,
  E59b pool ~1.5ms). Node-cull pool bounded by census: 462 faces/frame, FTB
  candidate==inside, E58 banked ALL per-face static work at -1.8ms total.
- World vertex transform cache: measured +4.8ms REGRESSION (~10-15% dedup:
  3.3K unique verts / 3.7K transforms); E58 packets already hoisted the float
  projection math. "BSP faces share vertices" is structurally true, materially
  false in H2 scenes.
- Flex intensity CLUT as the DEFAULT path: run #80 parked-turbo evidence
  (@128lv = 8.6-9.4x gate budget; GPT's 32-level form = 16-class = gate-red by
  construction; dlight rebuild storm). The base-light x intensity factorization
  is already banked bit-exactly (E55c vcolor hoist).
- Sky-into-holes / stop-z-scan: DONE better (E57-v2 oracle -> S-buffer row
  free-ride; combat sky_us=0.17ms).
- SW_INV_WIDTH 64 vs span-step 128: TRUE pre-batch (runtime FDIV fallback per
  block), already fixed by user batch #84 (SW_INV_WIDTH 64->128, in flight) -
  GPT independently found the same seam. Not an experiment.
- Per-px MMX (blend/modulate/particles/clears): axis STAYS closed (#38/#39
  parity tax). Open lane = #10 movq copies only.

## Screened out 2026-08-19 (evidence-pinned, do not re-pitch)
- Flex hierarchy transform composition: redundancy real, pool dead
  (flex_prologue 467us/frame covers ALL posing; rotate slice is a fraction).
- FMNI_NO_DRAW / demand-deform vertex_valid[] caching: same 467us prologue.
- Client-effects fill-rate governor + PVS-driven effect-sim skip: right
  diagnosis, wrong scope (fx sim in src/ + dynamic quality = gate-red).
- Entity-level occlusion/HZB reject, particle coarse-Z reject: pool absent
  in census (5% pre-FTB z-rejects, overdraw 1.25x, all 99 flex on-screen).
- Dynamic-light dirty-tile caches: E60 lineage ceiling +2.5ms perfected.
- Flex cluster/meshlet culling: E50 two-sided-content trap for the
  normal-cone variant; pose-memo form = bench overfit by construction.

## Parked for real PMMX (pm-mca by design cannot see these either)
Need a real-iron bench program (or the full trace+cache sim, phase 2 of the
ruler): cache-set phase sweeps (depth base 0..4095 step 32 x pitch pad
320..352 x light-arena phase), MTRR/USWC for the VESA LFB, DPMI page
lock/pre-touch, 32x8 scratchpad microtiles. GPT-prompted 16-bit z belongs here
too: run #3 falsified it under DOSBox (memory WIDTH free under the emulator;
+1.9% on insn-count ruler) and seg-6's 31-bit linear keys make the tie budget
delicate, but the 300KB->150KB depth footprint is a real-iron-only effect both
rulers are blind to. Strongest parked axis: tile
rendering — Unreal '98 shipped 8x8 tile CPU rasterization as its core cache
strategy; extend to snapshot store + temporal warp traversal. Build trigger
for the full sim: analyzer says pairing-optimum but DOSBox trend stops
moving = residual lives in the memory system.
