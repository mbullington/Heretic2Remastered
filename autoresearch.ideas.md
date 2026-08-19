# Next experiment ideas (ranked)

Untried hypotheses only — prune when attempted. History, falsifications, and
learnings live in autoresearch.jsonl; closed axes are enumerated there
(B1/painter family, lit-surface caches, HZB, quiescent-pool probes).
MMX/asm axis REOPENED 2026-08-18; pm-mca ruler + staged harness + combat
bench are BUILT (SEG-7 baseline #75: pmca_est_us=625, 145,709 cycles).
2026-08-20: external GPT code-review triaged against jsonl evidence - 2 new
atoms admitted (#12 blend specialization, #13 manifest-gated reciprocal),
7 re-pitches screened out with run citations (see dated section below).
2026-08-19 (user): lanes + temporal backlog PROMOTED into the queue (#14/#15/
#16) - combat bench is the workload contract, r_swtemporal defaults ON.
2026-08-19b: GPT round-2 ("what is mathematically redundant per frame/px")
triaged with code verification: #17 light-chain collapse (VERIFIED bit-exact
by aliasing), #18 block endpoint carry, #19 run specialization, #11 census
re-scoped, #20 codegen census, mips -> user arbitration, 6 more screened.

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

### 15. Light-disc stamps [bit-exact feasible; 13.7ms combat pool; sequence after #12]
Promoted from lanes 2026-08-19 per user (combat bench prices lights: 13.7ms).
SW_RasterLights @3715: sqrtf + per-px PutPixel inside a square-domain walk.
Precompute per-radius row extents (dx per row) + radial factor table keyed by
(radius, d2) populated with the IDENTICAL float expression (bit-exact by
construction) => row blit with z-test, no sqrt/reject/divide. Additive class
shares #12's exact-/255 machinery (dr + floor(sr*a/255) identity). Table
size: radii 2..64 x d2<=radius^2, computed once, cacheable.
Sibling approx variant is TURBO-CLASS ONLY: approx falloff (max + 3/8 mid +
3/16 min, or rsqrt family) changes px values everywhere => gate-red class,
needs user arbitration, NOT keep-fidelity like the stamp table.
NB: fog = documented non-goal (adds px cost, would red gates).

### 14. Deferred additive compositing — 8x8 tile accumulators [combat-priced fx overdraw]
Pool: particles 34.6ms + additive passes (Raven postmortem: ~10x overdraw as
their systemic unfixed cost). Accumulate additive fx into 8x8 tile per-channel
accumulators; one fb READ+ADD+WRITE per tile instead of one per sprite.
Legal: saturating nonneg addition commutes. In scope: blend path only; fx
simulation stays in src/. Sibling mechanisms in the same family (pick on
census, don't stack): particle stamps, sprite-atlas RLE spans (PS1
transparent-pixel surgery), pre-sampled particle atlases per discrete
radius/type (GPT round-2 concurs), E65/E66 re-probe (combat de-quiesces
their pools).
Est unknown until an overdraw census lands — gate build on a #11-style
counter run first; most complex mechanism in the queue.

### 17. World light-coordinate collapse (5 -> 3 perspective chains) [bit-exact by aliasing; est -1..3ms]
GPT round-2, VERIFIED IN CODE: draw_face sets vertex.light_s = base_s /
light_t = base_t for EVERY face class (ref_soft_raster.c:1107/1153); only
SURF_WARP makes s/t independent of base. For non-warp faces, light_s_over_z
attrs == s_over_z attrs at every vertex => SW_GRADIENT(lsz/ltz) computes the
identical floats as uz/vz => aliases are BIT-IDENTICAL (same ops, same
inputs). Then per block: ls0 == u0 so ils = iu + (1<<20) - (light_min_s<<16)
(one add vs FMUL+FIST); per px: kill the ils/ilt accumulators, lidx from
(iu+Cs)>>20 (iu+Cs add replaces ils+=step add = wash, but 2 regs + 2 step
inits + 2 block FISTs die). Face cost: -2 gradients (~20 float ops x 462
faces) + span-init evals. Flowing faces: light_s = s - scroll => recover as
u0 - scroll*iz0 (new float chain, ULP-drift class = E42/E43-lane, measure);
or keep flowing on the 5-chain fallback. Gate on !(SURF_WARP); warp keeps
the full path. Synergy: #19's run DDAs then derive light cells from (iu,it)
directly.

### 18. Block endpoint carry (iz1/u1/v1 -> next block's iz0/u0/v0) [bit-exact w/ guard; est -0.3..0.8ms]
GPT round-2, VERIFIED: block prologue re-derives iz0 = 1.0f/w and the four
start endpoints every block (raster.c:648-651) though all four were computed
as iz1/u1/v1b/(ls1/lt1) at the previous block's end. Carry them => FDIVs go
2B -> B+1 and -4 FMUL/block (~E51 halved FDIV count for -446us; this halves
the REMAINDER without touching block quantum). Bit-exact conditions: (a)
carry iz1 only when w_end > 1e-20f (current iz0 path is UNclamped vs iz1's
max() — divergence only in the degenerate near-plane case; guard recomputes);
(b) uz/vz/w advances must keep the same single-eval expression (uz + gux*block)
so carried products are the same floats. Carry FLOATS ONLY: carry the fixed
iu/it too and truncation drift accumulates = gate-red class. Note: shrinks
#13's pool ~2x => sequence this FIRST, then re-decide #13.

### 5. r_swscan2 row-parity variant [turbo-class, ON est -5..9 vs columns' -4.3]
PS2 field-render trick. Existing stripe infra (#71/#72) alternates 2px column
pairs: ON = -4.3. Row parity additionally kills span-row prologues (4.7K
span-rows x 0.76us ~ 3.6ms, E57-v2 census) while halving real-frame px the
same. Predicate-level change on existing machinery; snapshot already samples
both phases. Known anti-synergy: sky row-skip dies under stripes (#72
learning). User-grant class mirrors scan2 (stipple-adjacent turbo).
Stronger cousin from GPT round-2: checkerboard SHADE at full-res visibility
with face-ID-keyed reconstruction (holes fill only from same-face neighbors)
— kills silhouette ghosting but needs a full-face-ID buffer + reconstruction
pass traffic; vanilla row-parity is nearly free by comparison.

### 19. World span run specialization (light cells + texel rows + native-565 modulate) [bit-exact; census-gated; est -1..4ms]
GPT round-2 bundle "D", three coordinated per-px kills in the opaque lanes:
(a) LIGHT-CELL RUNS: per-px lidx = (ilt>>20)*stride + (ils>>20) + cmp is a
2-shift+IMUL+add+cmp chain EVERY px (78.8K/frame) even though light cells are
16x16 texels; precompute the next (iu,it)-cell boundary crossing (integer DDA
on the affine step) and run const-light inner loops - hoists the light unpack
too. Existing per-px lidx!=prev run-reuse is the weak form of this.
(b) TEXEL ROW POINTER: image->pixels[(ty)*width + tx] pays IMUL/px; retain
texture_row and recompute on integer-V crossing. (run #6's ctz-shift regress
falsified the shift form — CL-dependency — NOT the row-pointer form.)
(c) EXACT NATIVE-565 MODULATE: span_modulate3's 8-bit expand cancels
algebraically (verified: floor(floor(x/32)/8)=floor(x/256) incl. the
overbright clamps) => min(31,(r5*lr)>>8) form, -5..7 ops/px, repack
collapses to (r<<11)|(g<<5)|b. LUT sibling STAYS CLOSED (run #5: 128KB exact
LUT +8.8ms; memory != cheaper than ALU here).
Prereq census (folds into #11): light-cell + texel-row crossing frequencies
at 320x240 — at minification >=2px/texel, (b) pays; at near-1:1 faces it
adds a compare per px over the plain IMUL. (a) pays at any crossing rate
<= every-16px.

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

### 11. World census sweep [measurement-only; decides #19, coverage-first, style-LUTs, node-walk]
Decide-by-counter dimensions: (1) light-cell and texel-row crossing
frequencies per span (#19 gate); (2) fully-covered 128px-block fraction via
bench_world_zreject run histogram (decides GPT's coverage-before-prologue:
skip the whole block prologue FDIVs/FISTs when 4 cover words are saturated);
(3) build_face_light rebuild cells/frame under animated styles (decides
per-style 256-entry scale LUTs: raw*style_fixed -> indexed add; band-pack
makes per-CELL laziness awkward, count first); (4) BSP node-walk cost
(visframe subtree prune + axial-plane special-case potential: full dot per
node today, axial => 1 FSUB; GPT's PVS-row-cache part is DONE in user batch
f2edab6a).

### 20. DJGPP codegen census [measurement-only, cheap, T0-native tooling exists]
pm_mca.py analyze --objdump already walks every function: sweep hot symbols
for (a) call sites to ceilf/floorf/fix helpers in loops, (b) FDIV/FSQRT
counts per hot function, (c) FWAIT/control-word churn. GPT's "innocent C
hides x87 nonsense" hypothesis, priced statically in one script run; any hit
becomes its own experiment with the pmca delta printed natively.

### 13. World span-prologue FDIV -> seed LUT + 1 Newton [MANIFEST-GATED first]
GPT-prompted; survives only in this form. pm-mca-legal target (FDIV = 39cyc
non-pipelined): 2 reciprocals per 128px block x ~6K blocks/frame ~ 0.5Mcyc ~
2ms PMMX-class. But: world span loop is NOT in the manifest (3 loops only),
and the #8/rsqrt precedent was T0-discard on layout churn for exactly this
visibility gap. Precondition = manifest grows an opaque-span entry; then
1-2K seed LUT + r*(2-w*r) NR, error budget derived from texel-movement, not
IEEE. Do not run as T1-only on the DOSBox ruler (x87-tax refuted there).
Variant (b) [IEEE-exact alternative]: software-pipeline the TRUE 1/w FDIV
across the integer shading of the current block (Pentium FP/integer overlap;
pm-mca models it) — same manifest gate. #18 halves this pool first.

### 16. Temporal polish bundle [promoted 2026-08-19: temporal is default-ON; one session]
- radius-2 dilation ONLY for >1px residual reveal cracks (holes ~18/frame):
  quality, kills the visible artifact class
- snapshot zb-16 (halve snapshot store + parity traffic)
- warp block tune 8 vs 16
- flex-only face refresh when dynamic faces dominate (combat monsters qualify)
Bench-visibility caveat: warp-path items (zb-16, block tune, dilation) fire
only on fake frames; the H2DOS_SWEEP orbit may hold the motion gate at REAL
every frame — verify warp-cadence counters before/while tuning, or the bench
never prices these.

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
- True mipmapping for the world (GPT round-2): loader consumes mip0 only
  (width[0]/offsets[0]); nearest-level sampling by |du|/|dv| shrinks the
  distant-wall working set 4-16x. OUTPUT-CHANGING (gate-red class) but
  period-correct (Q1/Q2 software mipmapped by default; mip bias may look
  MORE right via shimmer suppression). Both rulers blind to the win
  (DOSBox: no cache model; pm-mca phase-1: L1-view) => priceable only on
  real iron or pm-mca phase-2. Needs: user sign-off on the visual change
  + a ruler that can see it. Do NOT run under DOSBox expecting movement.

## Screened out 2026-08-20 round 2 (GPT bundle II; evidence-pinned, do not re-pitch)
- PMULLW packed modulate over light-cell runs: 3rd re-pitch of the per-px
  MMX axis (runs #38/#39 parity tax). Axis stays closed; #10 movq copies
  remain the only open MMX lane.
- Sparse-Z / depth-needed tile mask (skip world zb writes where no later
  consumer overlaps): conflicts with default-ON temporal - the warp
  transports the WORLD-ONLY zb (batch f2edab6a qz-scaled z-key transport),
  so every world px has a consumer. Sky already tests covermask not z
  (S-buffer row free-ride): the other half of the pitch is done.
- Frame-stamped projected BSP edge cache: vertex-cache family, falsified
  at +4.8ms with ~10-15% dedup on H2 scenes; edge dedup adds clip-status
  caching but keeps the same stamp-check-vs-save economics.
- Viewmodel/foreground pre-coverage cull of world: requires render-order
  flip or a conservative rect (=> holes at silhouettes); post-FTB census
  has accepted overdraw at 2.0K px/frame - the pool this attacks is
  measured-absent.
- Per-scanline fixed-point edge DDAs (kill ceilf): pool misidentified -
  ceilf runs per-edge-onset, not per scanline (x advances by slope add);
  moving edge rounding shifts span boundaries => coplanar shared-edge
  crack class (B1-family gate risk), no measured dividend.
- Subtree occlusion via fully-covered 8x8 covermask tiles: 4th HZB form;
  the E59.1 leftover-overdraw census (2.0K px) still bounds the geometry
  pool. Keep-class ceiling ~1.5ms, machinery-heavy.

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
