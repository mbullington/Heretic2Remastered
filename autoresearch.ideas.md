# Next experiment ideas (ranked)

Untried hypotheses only — prune when attempted. History, falsifications, and
learnings live in autoresearch.jsonl; closed axes are enumerated there
(B1/painter family, lit-surface caches, HZB, quiescent-pool probes).
MMX/asm axis REOPENED 2026-08-18; pm-mca ruler + staged harness + combat
bench are BUILT (SEG-7 baseline #75: pmca_est_us=625, 145,709 cycles).

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

### ~~1. Direct-framebuffer present path~~ DONE (run #76): present 9,289->311us (-97%). pm-mca correctly silent (out-of-coverage by design: DOS video-mode selection, not renderer loops). Legacy learning: banked path accepts tearing, no page flip.

### ~~2. Flex gradient byte->float LUT~~ DONE (run #77): pmca_est_us 625->522 (-16.5%); gradient 89->74 clk, variant (swaprefs, same disease fixed) 79->66 clk; prologue trend 616->546us; bit-exact gates green. Secondary learning banked: LUT-index loads trade FILD serialization for index-AGI when movzx is adjacent (variant B pre-pass showed it); deferring index computation is the follow-on if these loops stay hot.
Baseline evidence: flex_vertex_gradient (89 clk/iter, 491 trips/tutorial,
985/combat) + flex_variant_b (79 clk) spend 34-38% of cycles on FILD chains:
xor eax,eax / mov al,[byte] / mov [tmp],eax / xor eax,eax / fild [tmp].
Byte->float is EXACT in fp32, so LUT[256] of floats is bit-identical:
movzx eax, byte[...] / fld [lut+eax*4] replaces the 6-instr store->FILD
serialization with a 1-cycle load + no temp memory round-trip. Covers both
vertex-gradient variants (same disease, same surgery). Bit-exact by
construction (integer 0..255 exactly representable); pm-mca re-prices the
loops directly, so T0 measures this one natively.
Risks: 1KB LUT init cost; make sure LUT symbol is 4-aligned (bank/AGI
declarations in the manifest entry); watch for dpending rematerialization.

### 3. Flex census remainder (proj fills + rotate_flex_clusters) [measurement-only]
#3 of the old queue is PARTIALLY ANSWERED by baseline #75 (gradient chains:
89/79 clk, fp-operand dominated — see #2). Still open: proj-cache corner
copies + span prologues + pose/proj redundancy splits. ~15 lines of counters:
flex_proj_lookup fills/frame + fill cost, rotate_flex_clusters calls vs
unique verts ratio (whole posing prologue capped at 467us/frame — only act
if a slice proves > ~300us of it).

### 4. Flex intensity-shade CLUT [turbo-class, est -4..8ms; keep possible at 64/128 levels]
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

### 5. r_swscan2 row-parity variant [turbo-class, ON est -5..9 vs columns' -4.3]
PS2 field-render trick. Existing stripe infra (#71/#72) alternates 2px column
pairs: ON = -4.3. Row parity additionally kills span-row prologues (4.7K
span-rows x 0.76us ~ 3.6ms, E57-v2 census) while halving real-frame px the
same. Predicate-level change on existing machinery; snapshot already samples
both phases. Known anti-synergy: sky row-skip dies under stripes (#72
learning). User-grant class mirrors scan2 (stipple-adjacent turbo).

### 6. normalize_checked rsqrt variant [sub-ULP; ruler now prices it directly]
normalize_checked pays serialized FSQRT (70 cyc) + FDIV (33 cyc), modeled in
pm_mca_data.py; it runs ~2x per flex joint per entity (flex_joint_rotation
dir/up) plus sprite bases. Call sites in-scope, visual-only => Q_rsqrt + 1
Newton is a legal variant (joint-basis ULP moves vertices ~1e-7 relative;
sub-ULP position argument per the fusion note, expect <1px flip). Collision
/ physics sqrt sites stay exact. Natural asm-lane T0 citizen once LUT refs
exist in the manifest for those loops.

### 7. MMX movq bulk-copy probe [bit-exact, trigger-gated, est -1..3ms]
Axis boundary: PER-PX MMX math stays closed (run #38 parity tax; #39).
movq COPY loops differ: fewer instructions, pure-copy bit-exact, pm-mca can
now price them precisely (movq U-only-pair rules + store-early rule are
modeled). Trigger: once #1 (dosmemput present) lands and copies dominate.
Form: __m64 builtins with -mmmx inside one copy function (no raw asm blocks
= no scheduler fences), single emms at function exit, alignment at alloc.
Do not reopen the per-px modulate axis here.

### 8. World texel+light run census [measurement-only; decides a -1..3ms idea]
30-line histogram in the opaque pow2 lane: exact (texel-addr, lidx, z-state)
run lengths. If mean run >= ~2.5px, bulk-fill same-colour runs (skip texel
fetch + lidx + modulate on continuing px; store + zb stay per-px). W1 says
uniform runs may be rarer than intuition, and the post-temporal px pool is
thin — census first, build only if the histogram is favorable.

## Workload-gated lanes (combat bench now prices them)
- Deferred additive compositing: accumulate additive fx into 8x8 tile
  per-channel accumulators; one fb READ+ADD+WRITE per tile instead of one
  per sprite. Legal because saturating nonneg addition commutes. In scope
  (blend path only; fx simulation stays in src/). Raven's postmortem names
  ~10x overdraw as the cost they never fixed systemically => combat prices it.
- light-disc stamps, particle stamps, sprite-atlas RLE spans (PS1
  transparent-pixel surgery), E65/E66 re-probe.
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
lock/pre-touch, 32x8 scratchpad microtiles. Strongest parked axis: tile
rendering — Unreal '98 shipped 8x8 tile CPU rasterization as its core cache
strategy; extend to snapshot store + temporal warp traversal. Build trigger
for the full sim: analyzer says pairing-optimum but DOSBox trend stops
moving = residual lives in the memory system.
