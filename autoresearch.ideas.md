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
2026-08-20c (user-approved): external research-direction pitch (boundary-
proportional/"kinetic" rendering family) triaged against in-tree E61 evidence
- 2 temporal-phase-2 entries admitted to arbitration below (translation warp,
styled-face material snapshot), 3 screened at triage: full kinetic screen
arrangement (B1/E44/E47 painter-tie + crack class; residual-hole pool
census-tiny ~18px/frame @879e568), frameless error heap (keep-class core
ALREADY SHIPPED as E61's motion-gated scheduler + hole-tile error regions),
map-compiled visibility automaton (PVS row cache DONE f2edab6a, vertex
cache +4.8ms falsified, emitted programs vs P55C 16KB I-cache). Flags the
SEG-7 workload blocker verified in code (static-identity entry below).
2026-08-20d (user-approved): external "1996 x87 lore" mail triaged against
LIVE objdump + pm_mca_data evidence during triage - 2 admitted (#21
conversion-tax [census EXECUTED at triage: 1,444 fnstcw / 4,169 fldcw /
1,905 fistp; FDIV PC=single phase], #22 shade-row LUT), #13(b) refined to
cross-span form (avg span 28.8px), #20 census partially executed, 2 parked
(indexed M8 world textures, constant-z mapper), 1 screened (no-clear
ztrick).

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

### ~~12. SW_BlendRect full-screen specialization~~ DONE (run #86): blend 70.3->40.5ms (-42%), frame -29.8ms.
### 12. SW_BlendRect archive note
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

### ~~15. Light-disc stamps~~ DONE (run #87): lights 13,752->148us (-99%). GPT's row-extent + radial-factor form, bit-exact verified.
### 15. Light-disc stamps archive note
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

### ~~14. Deferred additive compositing~~ RESOLVED-BY-CENSUS (runs #88-89): additive px = 0 in combat => tile accumulators have NO pool. Redirected to PARTICLE STAMPS (census 1.33M tex + 417K point px): particles 34.6ms->3.5ms (-90%). See #14 note.
### 14. Particle stamps (from deferred-additive census) [bit-exact, est -25..31ms combat]
DONE (run #89): census-first per the old plan KILLED the tile mechanism
(additive px/frame = 0 in combat: Raven's overdraw problem isn't H2-soft's).
Real pool = textured-quad px (1.33M/run) + point-disc px (417K/run), both
paying PutPixel + float-lerp atlas sampling per px. Stamps: (type x radius)
sampled-texel grids + int-predicate disc extents, PutPixel-class blend
families from #12/#15, debug fallback, arithmetic counting incl. OOB px
(evidence parity per-binary; tex totals follow the combat rng trajectory
~0.35% across builds - E62 rand-fork precedent; pointwise stamp==live
verified 0/4,640 mismatches). particles 34,588->3,480us. Textured-quad path
still pays per-px modulate() float chains (E55/E56 color-class: untouchable)
- residual 3.5ms is mostly that + z-guards.

### 21. x87 conversion-tax elimination [CENSUS DONE 2026-08-20d; est 5-7ms PMMX-class, DOSBox-BLIND]
Mail pitch (Quake Sys_SetFPCW/Sys_LowFPPrecision lineage), census executed
against SILVER.EXE: 1,444 fnstcw / 4,169 fldcw / 1,905 fistp static sites.
Hot paths CONFIRMED: _draw_face 26 fnstcw (~10 truncating casts per 128px
block prologue: iu/it/ils/ilt + 4 steps + ikey/ikey_end; plus per-row ceilf
casts), _SW_RasterFlexEntity 10 (= run #16's "12 conversions/block"),
_dispatch_flex_screen_triangle 11, SW_PutPixel 3 (folds into #12's fix).
pm-mca already prices every piece: fldcw=8NP, fistp=6NP, fnstcw=2NP =>
~22 serialized clk per C cast (gcc hoists the fnstcw save; the chop/restore
fldcw pair per cast remains). World alone: 6.6K blocks/frame (12.28M walked
px / 64) x ~10 casts ~= 1.5M clk ~= 6ms @PMMX-233 ~= 4x the ENTIRE #13 FDIV
pool. DOSBox cannot see this (run #16 double-bias sw_ftoi = discard-as-noise
on the blind ruler, NEVER gate-falsified; revival justified, not re-pitch);
pm-mca can once the span loop gets a manifest entry (same gate as #13).
Phase (a) BIT-EXACT: known-mode conversions in span/flex prologues -
double-bias magic-add (RN-semantics, CW-independent; revival of run #16) or
asm-lane bare fistp. Phase (b) SEPARATE GATE-RISK RUN: chop + 24-bit-PC
FPCW set once at renderer entry, restored at exit (the actual Quake trick):
FDIV 33->19 clk (pm_mca_data.py already cites the 19/33/39 24/53/64-bit PC
table; DJGPP CW = 53-bit). ULP-drift class = E42/E43-lane arbitration.
Subtlety the mail missed: gcc emits its dances regardless of ambient CW, so
(b) does NOT remove the cast tax - (a) does; (b)'s own object is FDIV/FSQRT
latency. Ambient chop changes no compiled-C cast semantics (dances set chop
explicitly; libm frndint sites manage their own CW). Ruler note: phase (a)
is DOSBox-invisible by construction => pm-mca manifest entry is the 
evidence path; do not T1/T2 it expecting trend movement.

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

### 5. r_swscan2 row-parity variant — DISCARDED by forensic bisect (run #91)
PS2 field-render trick; FULLY BUILT (r_swscan2=2: row gates in
spans/sky/clear, default-off) then gate-red EFFECTS 129px + world +14ms on
the OFF path, reproducibly. Bisect: sw_scan2_row fns-only, sky row-gates,
row clear, span-counter-move = all GREEN; sw_scan2_row() CALL or even DECL
inside draw_face_spans = RED, and counters+env prove the gate never fired
in-game. draw_face_spans is PERTURBATION-FRAGILE on this host (ARM DOSBox
dynarec billing-granularity + lowered-FPU class; E18's +521us may share it).
Any C-level addition there = gate/billing dice roll; E17-class
float-chain-identical rewrites are the only known-safe edits. Revival path
= asm-lane or manifest-era span-engine rebuild, NOT C.
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
collapses to (r<<11)|(g<<5)|b. LUT sibling: monolithic 128KB texel-indexed
form STAYS CLOSED (run #5: +8.8ms on DOSBox's free-memory/cheap-IMUL
ruler); the 128B-hot-row variant REOPENED as #22 under pm-mca's
IMUL=9-11clk-NP pricing.
CENSUS #11 DONE (run #92): (a) 8,738 light-crossings/frame ~ run 9.8px
> 16px threshold WOULD pay structurally, but draw_face_spans edit-fragility
(runs #91-92) => PARKED for asm-lane/manifest era; (b) 24,390 texel-row
crossings/frame = 29% of painted px => DEAD (recompute beats saved IMUL).
(c) stands as the only live piece; #22 inherits the manifest gate.

### 22. 565 shade-row LUT [bit-exact by construction; sequence AFTER #19; est -2..5ms PMMX-class; shares #13's manifest gate]
Mail pitch; NOT closed by run #5: #5's 128KB monolith was falsified on
DOSBox's free-memory/cheap-IMUL ruler ("3 extra byte loads + index math +
init-check per px"). This form: mul5[1024][32] + mul6[1024][64] = 96KB
committed but 128 BYTES hot per light-cell run, row pointers hoisted at run
start, tables built eagerly once (no per-px init-check). pm-mca flips the
sign: 3x IMUL (9-11clk NP EACH) per painted px -> 3 hot-row loads (1clk +
AGI care) with identical indexes ((texel>>11), (texel>>5)&63, texel&31).
Bit-exact: rows built with #19(c)'s verified min(31,(r5*lr)>>8) expression.
Deps: #19(a) light-cell runs (per-px row-pointer reload negates it), and
the opaque-span manifest entry for honest pricing.

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

### ~~11. World census sweep~~ DONE (run #92; data banked in #11-note)
RESULTS (per frame, 144f combat run): light-cell crossings 8,738 (avg run
~9.8 painted px); texel-row crossings 24,390 (29%: #19(b) row-ptr DEAD);
fully-covered 128px blocks 582/3,302 = 17.6% (coverage-before-prologue pool
~0.1ms: DEAD); BSP nodes 963/walk (~15 instr each: visframe pool ~0.1ms:
DEAD). style-LUT question: see bench-world-lightbuild-px in evidence. The
census placement itself re-confirmed run #91: draw_face_spans additions =
gate lottery (129px signature).

### 20. DJGPP codegen census [PARTIALLY EXECUTED 2026-08-20d; remainder cheap]
(c) FWAIT/control-word churn: DONE at mail triage - 1,444 fnstcw / 4,169
fldcw / 1,905 fistp, hot fns enumerated, graduated into experiment #21.
Remaining: (a) ceilf/floorf/fix-helper call sites per hot symbol
(draw_face's 26 fnstcw INCLUDE the per-row ceilf casts - they share #21's
fix machinery), (b) FDIV/FSQRT counts per hot function (prices #21 phase
b's pool). One-script sweep via pm_mca.py analyze --objdump; any new hit
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
Form note 2026-08-20d: avg span = 28.8px (12.28M walked px / 425.8K spans /
64 frames) => most spans are ONE 128px block, so the mail's Quake picture
("issue block-N+2 FDIV, shade N+1") has no room INTRA-span. Working shape =
CROSS-SPAN: issue the NEXT span's iz0/iz1 FDIVs while integer-shading the
current span's block (~29px of inner-loop covers the 33-clk fdiv i_ov=32
window easily IF the schedule holds; C unlikely to keep producer/consumer
distance => asm-lane kernel, pm-mca priceable).

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
- Translation-capable temporal warp [E61 lineage + per-plane homography;
  2026-08-20 research pitch, triaged: the pitch's #1 mechanism]: E61's
  backward warp is ROTATION-ONLY by construction (ref_soft_raster.c:4384
  moved_sq > 1e-6 => real frame; comment :4370 "any translation needs
  depth-dependent mapping"), so ALL walking gameplay falls out of reuse.
  Mechanism options: (a) depth-dependent backward map via snapshot z-key
  (per-px ray*z transform; stage-2's forward splat was the slow first
  draft, 151-157 vs 136.5 @879e568), (b) per-face plane homographies (H2
  world = known planes; needs face-ID sidecar in the snapshot, +2B/px
  traffic; pitch's exact-projective form). Disocclusions land in the
  EXISTING hole-tile repair pass; expected artifact class = disocclusion
  z-tie/1px cracks (translation map is many-to-one, unlike the rotation
  function => likely turbo-class unless ties hold). Pool = every
  straight-line walk; sibling rotation warp banked -25.2ms (e9b207f).
  Gated on the workload blocker (static-identity entry below): the combat
  bench has zero translation by construction.
- Styled-face material snapshot / relight-after-warp [census-gated;
  2026-08-20 research pitch #2, triaged]: the snapshot is BAKED RGB565+Z
  (sw_snap_color/sw_snap_zb), so nonzero-lightstyle faces are excluded from
  reuse en bloc (temporal_dynamic, ref_soft_raster.c:1012-1017: re-rendered
  every fake frame; "they are few" is unpriced). Mechanism: transport
  material IDENTITY for styled faces, reshade per fake frame from current
  style scalars. Enabler VERIFIED in-tree: #17 proves light_s/t == s/t for
  every non-warp face (bit-exact aliasing) => reshade needs no new gradient
  chains, only light lookup + modulate per px. Survives the 3x-falsified
  lit-surface axis (#23/#59/E60) by construction: amortizes GEOMETRY
  (span-walk/prologue), pays shading PER FRAME - no amortized shading to
  invalidate. Win = styled faces' fake-frame span-walk/prologue only;
  census prereq folds into #11.3 (count styled px FRACTION, not just
  rebuild cells). Sidecar traffic (styled-px tag + light-cell idx) is
  real-iron-visible, DOSBox/pmca-metric-blind. Gated on the same workload
  blocker.
- rgb-affine flex turbo variant A: -12.1ms flex (frame 157.1 vs 160.7), GATE
  RED 210px synthetic-fixture. CLUT (#4) is its higher-fidelity sibling and
  likely supersedes it at 64/128 levels.
- Static-camera identity fast path for temporal [was -25..40ms on static
  tutorial bench]: the combat bench ORBITS the camera every frame
  (H2DOS_SWEEP), so the calm band it attacks is invisible to the current
  bench. Real gameplay still has calm stand-and-look phases; needs either a
  calm-camera second scene or user arbitration to build anyway. Ranked out
  of the active queue 2026-08-19.
  WORKLOAD BLOCKER (verified in code 2026-08-20; gates ALL temporal
  phase-2 incl. #16 polish and the two entries above): SEG-7 orbit =
  7.5deg/frame yaw (h2dos_main.c:1105, frame%48 * 360/48) + stationary
  player, and the E61 motion gate (ref_soft_raster.c:4384: >3deg rotation
  OR >0.001u translation => real) therefore forces EVERY frame REAL -
  warp cadence is exactly ZERO in the combat bench. Numbers agree: SEG-7
  world 101.1 ~= seg-6 no-warp 99.1 vs warp-active 78.5 (e9b207f). Every
  temporal-reuse mechanism prices at 0 on both rulers until a
  calm+translation second scene lands or the user arbitrates building
  blind. #16's warp-cadence check is confirmed by construction, not
  counters.
- True mipmapping for the world (GPT round-2): loader consumes mip0 only
  (width[0]/offsets[0]); nearest-level sampling by |du|/|dv| shrinks the
  distant-wall working set 4-16x. OUTPUT-CHANGING (gate-red class) but
  period-correct (Q1/Q2 software mipmapped by default; mip bias may look
  MORE right via shimmer suppression). Both rulers blind to the win
  (DOSBox: no cache model; pm-mca phase-1: L1-view) => priceable only on
  real iron or pm-mca phase-2. Needs: user sign-off on the visual change
  + a ruler that can see it. Do NOT run under DOSBox expecting movement.
  Heavyweight sibling from the 2026-08-20 research pitch: Talisman-style
  hierarchical image caching (Shade '96: BSP-subtree images reused as
  warped quads) - same gate-red resample class, same ruler blindness,
  plus bookkeeping; census prereq = per-face distance x screen-area
  distribution. Mips remain the cheaper first move if arbitrated.

## Screened out 2026-08-20d (90s-lore mail; evidence-pinned, do not re-pitch)
- No-clear Z via GL-Quake ztrick (alternate key ranges + compare direction
  per frame): pool = the entire clear_us ~= 1.2ms (~0.6% of frame,
  ref_soft_framebuffer.c:247); requires a polarity variant of EVERY
  z-compare/write site (world, flex, sky, SW_PutPixel/blend stamps, lights)
  + canonicalization at the temporal snapshot boundary (default-ON warp
  transports zb); seg-6's 31-bit linear keys already carry a delicate tie
  budget. Worst machinery-to-payoff ratio in the mail.

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
2026-08-20d (90s-lore mail): indexed M8 world textures - keep wall-kind m8
as INDICES + sample via per-image pal565 (src8/pal565 retention EXISTS,
skin-only at ref_soft_image.c:146; extension mechanical). Bit-exact by
construction (pal565[i] built with the identical sw_pack_texel call as the
per-px expansion); win = halved texture working set = cache-only => both
rulers blind. Synergy: composes with #22's shade rows. Also the moonshot:
constant-z / orientation-adaptive mapper (Ken Silverman school) - pick span
direction per surface by |dw/dx| vs |dw/dy| so perspective varies least
along runs => whole-run affine or huge correction quanta; needs a second
complete span-engine family (sky/flex/temporal interactions) + a real-iron
ruler. Revisit only if the world lane hits an FDIV/prologue floor.
