# Untried hypotheses (remove when attempted)

## Big structural swing (researched from q2DOS + Hammer of Thyrion, both GPL2 — see NOTICES.md)
Both DOS ports ship Quake-family surface caches: composite texture*lightmap per
face per frame (r_surf.c D_CacheSurface/R_BuildLightMap) so the span inner loop
becomes a pure indexed fetch (~8 ops/px vs our ~30). World = our whale (108ms).
PLAN (two slices, both gate-arbitrated):
1. Surface cache for world faces (mip0 first): per visible face, smax x tmax
   composite at 32bpp: span_modulate3(texel, light) built ONCE per texel. The
   (stex,ttex) -> (ls,lt) map is exactly AFFINE per face (both are affine maps
   of the face plane), steps derivable per face; light boundaries quantize a
   hair differently vs the current screen-projected path -> ERROR BUDGET RISK
   bounded to lumel edges; gate (46px) arbitrates, expected mostly-exact.
   Inner loop becomes pbase[(it>>16)*cw + (iu>>16)] + zb test/write.
   Est: world 110 -> 55-70ms. Cache pool: faces keyed by (face,generation),
   evict LRU, big faces included (H2 faces are small: median block 16x16).
2. Painter's-order world (drop per-px zb compare on opaque world faces; keep zb
   WRITES for sky/entity gating + alpha/trans faces keep compare). BSP walk is
   already back-to-front from the camera side; verify span overlap behavior.
   Est: -3-5ms on top.
Both slices preserve 32bpp RGB pipeline (cache composites in linear RGB space,
which 8-bit palette Quake ports could not do - ours is actually CLEANER in 32bpp).
Also noted from q2dos: -O2 enough, -fno-strict-aliasing, -fomit-frame-pointer
(JJGPP bool: check our flags match), present path = straight memcpy to LFB
(validates the layout-flip direction: finish the flip via per-subsystem PPM
isolation, then direct-render).
## World pass (world_us ~128 ms — biggest target)
- SPAN-SORTED front-to-back world rendering (Abrash-style span clipping per
  scanline from front-to-back BSP order): each world pixel shaded once, no
  per-pixel z compare for accepted spans; keep z writes for entity testing.
  Expected: remove ~10-15 cy/px + all 1.42× overdraw.
- Lightmap fetch hoisting: light cells constant over ≤16-texel runs; fetch
  per-run instead of per-pixel (1 mul + clamps saved per px).
- Int16 depth buffer with fixed-point iz stepping: halves z traffic; watch
  precision at far range (needs care: quantize at 2^15 w/ per-block reseed).
- Split U/T perspective: compute `(iu/iz)` via reciprocal LUT of iz at 12-bit
  precision instead of FPU div per block-END (only 2 divs per block remain,
  per *span* + per-tri → likely minor now).
- Draw sky-covered spans without storing z (sky is always furthest).

## Sky (sky_us ~36–56 ms, rotating → cache never hits)
- Tile-based swirl: render the warp into 16×16 tiles and memcpy identical
  tiles; the swirl field is smooth — quantize dir.z/dir.x to tile grid.
- Bypass the cache entirely on rotate!=0 and write directly with the
  incremental dir stepping loop (save the double-buffer copy + key checks).

## Entities (entities_us ~38 ms)
- Per-entity affine approx for small screenspace tris: skip per-block div
  (done: 1 div/span). Next: batch transform via per-entity basis reuse in
  the *facing* prologue (world_to_view dot3 → shave 3 mul chains using
  pre-folded entity basis) — mostly registers though.
- Back-face cull flex triangles BEFORE per-tri setup via screen-space
  signed area test (projected cache gives it nearly free).
- Clip the GLCMD fan walk: bound per-fan vertex count tests, avoid memcpy
  float reads by aliasing (undefined-behavior-safe unions if C allows).

## Present (present_us ~23 ms)
- Had a custom RGBA→RGB24 repack (bitfield + 32-bit stores) that measured
  SLOWER than SDL_ConvertPixels (28.7 vs 22.7 ms) — SDL's converter is
  already hot.

## Clear (clear_us ~6 ms)
- Sky already fills the whole viewport; skip the color clear when
  `SW_ClearDepth`-only is possible; and clear depth with single-width
  sweep at 8 bytes/iteration (rep stos pattern).

## Cross-cutting
- 32-bit inner-loop batching: process 2 texels/iteration (load 2×32-bit,
  combine stores) to amortize loop overhead.
- Move `bench_*` counter stores out of all inner loops into locals.
- Prefetch next span's texture row? (DOSBox does not model cache; skip.)
- Translation lookaside: dot3 world_to_view per projected vertex for world
  faces shares the view basis — already done per frame; minor.

## Learnings (seg 4 continued)
- DOSBox flat-op model: light LUT (+8.8ms world) and pow2 tw_shift (+4.2ms) both REGRESSED vs IMUL chains; hand present shuffle regressed vs SDL_ConvertPixels (+5.5ms). Micro-ALU/memory swaps are empirically hostile here — test, never assume.
- Anatomy 2026-08-17: entity 36.9ms = flex 20.8ms (bench-model-flex=256/32≈8/frame?! verify per-frame scaling) + sprites ~0.014ms + ~16ms gap = inline/brush entity faces flowing through draw_face (attributed to world buckets). Flex pixel work is tiny (~1038 px/frame, 244 lerps/frame, prologue 186us) => ~20ms unaccounted inside SW_RasterFlexEntity = per-modela/tri setup (clip/project/skin fetch). Next: sub-stage timers inside SW_RasterFlexEntity (pose/clip/project/raster).
## Queued mechanisms (unchanged priority)
1. flex sub-stage timers -> then cut the dominant stage
2. sky tile tagging (38.2ms wall; layered transform sky)
3. coarse 8x8-tile z-reject to kill walked-but-z-rejected world px (overdraw)
4. world hand asm inner loop (92cy/px observed vs ~30 model) - last resort

## Entity anatomy (2026-08-17, complete)
entities 36.3ms = flex ~20.5ms + brush 14.4ms + sprites ~0
- flex: 320 tris/frame, 45% bbox-culled (now hoisted pre-fill), ALL fast-path; per-tri setup = float gradient tax (x87 emulation ~10-30cy/op). per-tri ~60us for ~6px.
- brush: 175 draw_face calls/frame ~85us each (vs 18us for world faces). World-entity-cull kept (sphere, ~10% whole-model culls only). Per-face cost places in draw_face vertex loop + clip_near + light dims, ALL float.
## CONFIRMED MODEL (2026-08-17): DOSBox = flat instruction count.
x87 emulation is host-side cheap (int-key z conversion of the world inner loop changed NOTHING - kept as infrastructure). Wins come from REMOVING INSTRUCTIONS PER PX or SKIPPING PX/RUNS WHOLESALE:
- sky-after-world z-gate: sky 38.2->16.8ms (-56%), frame -11% [KEPT, gates PASS]
- int-key depth buffer (bits of non-negative floats = ordered uint32): NEUTRAL perf, kept as infra.
## DONE / ruled out since
- whole-model sphere cull for brush entities: kept (-0.4ms, only ~10% models offscreen)
- flex early bbox reject hoisted: kept (~0.1ms)
## Ranked next levers
1. FLEX tiny-tri fast path: 320 tris/frame avg 3px/1.8 spans/1.1 blocks but ~25us per span-scale; bypass span engine for bbox<=8x8 with per-px barycentric stepping replicating EXACT conventions (ceil(x-0.5) coverage, block-linear uv seeds, ikey z). Est -10 to -14ms.
2. BRUSH per-face cone cull during vertex loop in draw_face (bail before min/max light scan + clip_near). Est 3-6ms of 14.4ms.
3. WORLD inner loop 118ms @ 92-120cy/px: split z-test run from texel run per 16px block (z-mask first, contiguous texel runs) to drop per-px branch+5-inc overhead on z-passed runs; or 2-row pairing. Instruction-count audited, not FPU.
4. present 24.2ms: SDL converter on 320x200 windowed XRGB8888 pitch 1280; rows 200-239 black. Copy only 200 rows? surface is 240 but window shows 200? Measure partial convert [y0..200).


## #32 (PLANNED, next big swing): painter-order opaque world, drop per-px z
Goal: world_us 97,113 -> ~60-75ms. Mechanism: recursive BSP walk near->far => opaque world
spans stop testing/writing per-px zb for themselves (front-to-back painter = z results);
zb WRITE must remain (entities/flex gate on world zb). Steps:
1. Replace SW_RasterWorld leaf-list iteration with node recursion: side=PlaneSide(vieworg);
   walk(near child), draw node faces, walk(far child)? ORDER for front-to-back write:
   visit near subtree FIRST, then node faces, then far subtree (contained content painter-safe).
   Keep vis cluster marking (visible_faces[]) but walk nodes instead of flat face scan.
2. In span engines: opaque world span inner loop: drop `if (ikey >= zb_row[idx])` test AND
   keep unconditional zb write. Alpha/warp spans keep z-test+conditional path (they must
   still lose to nearer opaque spans; with painter order nearer spans already written zb).
3. Gates judge coplanar tie semantics: equal ikey: classic BSP coplanar-valid content -> no diff on bench.
Risks: dup faces (SURF_UNDULATE displaced), overlap ordering across leaves (BSP guarantees).
Expected: -13..20ms. Also enables (later) zb-write elision for fully-opaque-covered rows.

## Other queued swings (post #32)
- Flex gradients/prologue: per-tri FPU ~60 ops + span setup ~500; batch fixpoint preconvert.
- Sky zero-run rows (est -2..3ms).
- 8-bit indexed pipeline + LUT modulation (needs gate-contract renegotiation; est -40ms world; DO NOT attempt without user sign-off).

## BANGER STACK (exa-mined 2026-08-14, replaces "dark magic" wishlist). Sources:
- Abrash Black Book ch66/67/70 (phatcode.net/res/224): Quake span-sorting internals.
- Sanglard Q2 software review (fabiensanglard.net/quake2/quake2_software_renderer.php):
  colormap.pcx = 256x320 LUT: 64 shading gradient rows + 16x16 translucency-blend squares.
  Q2 sw lightmaps are MONOCHROME; H2 lightmaps are RGB (delta = colored-light fidelity).

### B1: Quake-native span-sorting world (edge list, zero overdraw, NO zb per px)  [3-4x on world]
Walk BSP front-to-back, assign node seq numbers; polygons -> clipped edges -> ONE global
x-bucketed edge list scanned top-to-bottom once (AEL); emit x-sorted spans into per-surface
span lists; spans cover screen EXACTLY once; hidden faces emit no spans. Kills: per-px
zb load/cmp/store on world (3-4 of 10 ops), span-engine z-write, sky z-walk (sky px =
background edge-list sentinel, filled from exact coverage, 76.8K->~35K walked px).
Supersedes #32 painter-order (same direction, native mechanism instead of a write-painter).
world 97 -> ~30-40ms, sky 16.8 -> ~4-6ms, zb traffic for world gone (clear keeps zb for entities only => clear ~0.8ms).
NOTE: surface-cache ALREADY falsified here (#23) -- do not re-attempt lightmap bake caching;
span-sorting is orthogonal (it works on screen px, no surface texel caches).

### B2: 8-bit indexed framebuffer + colormap.pcx-style LUT (256-color pipeline, needs sign-off)  [2x world, 3x present]
texel 8-bit + shade byte -> lut[shade<<8 | texel] -> write byte: ~6-8 ops/px (vs 28 RGB).
Present: 76.8KB -> SDL palette surface (SDL3 pal8->XRGB) = quarter the bytes.
Colored light: RGB lightmaps must quantize to shade-index; torches/blue light lose hue.
GATE IMPACT: pixel class changes (only 256 colors) => baseline refresh + visual review REQUIRED before keep rule. DO NOT RUN without user sign-off on the fidelity trade.
Transflex/sfx blending via blend squares of same palette.

### B3: entity/flex gradient batching (prologue+gradients amortized, fixedpoint)  [1.35x entities]
~44ms entities+flex: per-tri gradient FPU + per-entity transform. Batch: transform ONCE per
entity to fixedpoint vertex set, gradients by INT arithmetic on 16.16 (drops FPU per tri).
Big local multiplier but smaller absolute pool (est -8..12ms).

### Rejected by math (do NOT propose): MMX (DOSBox flat parity; P90 has no MMX), damage rects
(view moves every frame), resolution tricks (output-exact gates), surface cache (falsified #23).
STACK OUTCOME ESTIMATE: B1+B3 without sign-off: total ~100-115ms. B1+B2+B3 with sign-off: ~45-60ms -> PASSES the 85ms / 30fps stop condition. (~2.6-3.4x from 155ms; 10x not reachable at 320x240 with per-px color work -- state honestly.)

## B2-REVISED (exa dig #2): 565 not 8-bit. Green light rationale informed by sources:
- Sanglard confirms Q2 sw used max(R,G,B) 6-bit shade => monochrome-shading cheat.
- Allegro exlights + hicolor era practice => the TRUE pair to the palette trick:
  **RGB565 16-bit internal pipeline**: colored light KEPT (no monochrome cheat!), texels
  16-bit, modulation via three precomputed channel-scale LUTs (r:32, g:64, b:32 entries ->
  lut[(texel&mask)+offset]) = ~7-8 ops/px vs today's ~12-15 in modulate3; framebuffer+sky
  cache+width-banked bytes all HALVE; present blit halves (307KB->153KB; VESA 16-bit mode
  320x240 = LFB 0x150-class instead of 32-bit). Bilinear/perspective spans otherwise identical.
  Fidelity: RGB565 = era-authentic hicolor (Descent II / Q2 had 15-bit paths); no monochrome
  fake => arguably CLOSER to GL desktop than the 8-bit option. Gate baselines regenerate once.
  Optional hi-fidelity verify on desktop.
- 8-bit palette tier remains on the table as B2b AFTER 565 if more needed (Q2 colormap.pcx
  mechanism: 64-gradient rows + 16x16 blend squares; lut[shade<<8|texel]).

## FAST-RSQRT/SH audit (user asked "like the quake sqrt function / spherical harmonics"):
- 0x5f3759df fast-invers sqrt: our divides/sqrts already LUT'd (SW_INV_WIDTH) or amortized
  per-block/entity => total exploitable surface ~0.2ms. NOT a banger. reject with math.
- Spherical harmonics: SH = runtime probe irradiance compression; H2 lights are BAKED lightmaps
  sampled in texture space - nothing for SH to compress. The shade-space equivalent (64-level
  colormap rows) IS the H2-correct analog and is captured by B2/B2b. reject with mechanism.

## EXECUTION ORDER (locked): B1 edge-list span-sort world (no sign-off needed) -> B3 flex/
entity gradient fixedpoint batching -> B2(565) with one-time gate refresh, then B2b optional.

## B1 IMPLEMENTATION STAGES (context-safe checkpoints):
- B1.1 SW_RasterWorld: replace leaf-list iteration (visible_faces[] flat scan) with recursive
  node walk: side=PlaneSide(vieworg,node->plane); walk(child[side]) FIRST (near), then node's
  own faces, then child[side^1]. Keep PVS cluster marking EXACTLY as-is (mark leaves first from
  current code, then recurse). Still fully z-buffered; gates must be 0-diff (proves painter
  ordering correctness for bench content). Site: SW_RasterWorld ~line 1199; draw_face entry 759.
- B1.2 opaque world spans: drop per-px `ikey >= zb_row` test AND zb store (alpha/warp unaffected);
  spans drawn front-to-back paint once. World px loop loses 3-4 ops.
- B1.3 edge-list exact coverage + sky from coverage-sentinel; zb writes replaced by span-RUN zb
  fill for entity gating; sky walks only background px.
- Flex entities keep own zb path against world zb (unchanged semantics).
- Counters: keep walked_px/inside_px; post-B1.2 expect walked==inside world px = unique coverage
  approx (overdraw -> ~1.0), sky walked px collapses to hole count.

## B3-forensics (measured 2026-08-17, branch dc863377, bench BENCH1)

Flex entity pipeline decomposition via ablation probes (direct bench runs, evidence-verified):
- bench-avg-us-flex-tris = per-frame sum. Baseline 20,278µs.
- Probe A (early-return span_raster_flex_triangle): 17,309µs → ALL flex span raster incl gradients/rows/px = ~3.0ms only.
- Probe B (skip colors/uv/corners+dispatch): 16,744µs → colors/uv/corner-build ≈ 3.5ms.
- Probe C (B + proj lookups stubbed): 16,744 identical (dead-store elimination made C≈B) → cache-hit lookups ~free; misses 244/frame tiny.
- Remainder ~16.7ms = tri-loop SKELETON+segment fixed costs: 325 tri-iters + 166 glcmd blocks + 8 mesh iters per frame → ~51µs per tri-iter vs ~300-450 static instrs (x87-dense: flds/fmul/fxch/faddp/fmuls/fmulp dominated, plus many fnstsw) → effective factor ~3 vs ALU-flat parity. Likely x87 stall factor in DOSBox (unverified; parity probes were ALU-only).
- Struct sizes: SW_RasterFlexEntity = 1,326 static instrs; biggest loop ~895 bytes x87-heavy.
- Conclusion: flex-tiny-tri pig = per-iteration FPU plumbing, NOT span engine, NOT gradients (3ms), NOT cache. B3 rewrite shape: batch per glcmd-strip outer-product: precompute per-strip corner tables so tri loop degenerates to index arithmetic; or precompute per-vertex s_over_z etc ONCE (per entity) instead of per tri (colors/intensity per vertex current per tri!). colors[][][] rebuild per TRI is suspect #1 (3 corners × 4 channels × conditionals per tri vs per-vertex-once).

## B-series swing economics (all tree @ dc863377 = 155,622µs)
- B3 skeleton-diet target: 20.3 → ~9ms = −11ms (batch + hoist per-vertex tables, fewer x87 ops in loop)
- B2 RGB565: present 11.5→~5.5ms (153KB stores) + world/entity modulate diet −10..20ms est
- B1.3 full edge-list: world 97→45-55ms IF zero-overdraw holds (must beat +2.3ms walk overhead; compare-drop alone falsified: +4.1ms regression logged as #31)
- Stop line 85,000 reachable iff all three land: 155.6 − 11 − ~25 − ~45 ≈ 74µs

## Open schism discovered
- bench-avg-us-flex-tris denominator = frames ✓ verified (h2dos_main.c:847).
- flex-setup/flex-dispatch timers exist but UNWIRED (0 values) — wire when B3 starts.

## Probe D (2026-08-17, reverted): bbox reject-chain removal
- Segment unchanged (20,553 vs 20,278 baseline): the 5-clause float trivial-reject is free.
- x87=3x global hypothesis REJECTED: world path does ~46K span-setups × ~40 float ops per frame, 97ms total — x87 cannot be uniformly 3x or world alone breaks budget.
- REMAINING unexplained: ~3.4x localized phantom in flex tri-loop (~1,500 dynamic-instr-equiv/iter vs ~300-450 static). Localized suspects: fnstsw×12/body chains, x87-serialized dependencies, x87fxch thrash, -O3 spills. NOT loop count, NOT rejects, NOT lookups, NOT colors.
- NEXT-BUILD (B3): convert tri loop to (a) per-strip hoisted uv+color+proj tables (entity_seq-keyed color cache), (b) float-free control flow in loop body (no chained FPU compares), (c) pointer-walk instead of command_vertices[3] indexing. Also wire flex-setup/flex-dispatch timers (currently 0/dead) to finally bracket the phantom.
- Session state: tree @ dc863377, all probes reverted, plan_tracker tasks current.

## B2 RGB565 — implementation site map (post-#32, working from 77ce9692)
Goal: internal framebuffer + textures become 16-bit RGB565; present 307KB→153KB (also candidate VESA 320x240x16 mode); per-px modulate ~7-9 ops (2 packed multiplies) vs current 3 extracts+3 muls+3 min+pack.

Sites (verified by grep census):
- ref_soft_framebuffer.c: SW_RGBA macro def (:18 inline), SW_Clear (:51), pal→sw_color palette table (:121 XRGB8888), SW_Present (:144 — LFB copy loop, 4B/px; switch to 2B/px 153KB).
- ref_soft_raster.c touchpoints (~17 grep hits, but many in comments): line 138 lightmap_color pack (returns uint32 light sample; becomes 565 with 256=1.0 style scale packed differently), 604 span_modulate3 (world opaque px), 639 dst fetch + 644 translucent pack (world alpha path), 1164/1184 sky writes (two loops), flex write paths (~2040-2065: SW_RGBA packs ×3, modulate mr/mg/mb), brush path, sprite path, blend path.
- ref_soft_image.c: 7 SW_RGBA callers (M8/M32 decode, missing-texture magenta, resize) — decode target type change.
- Blend/mix helpers: SW_BlendRGBA? grep before build.
Design decisions:
- Keep zb SEPARATE uint32 array (unchanged) — do not try packing depth into 565 (keys need full precision).
- Style scaling: lightmap samples currently 256=1.0 per channel; 565 bilinear: keep 32-bit staging for light sample then quantize→565 (r5g6b5 with 256→~31/63 scale via (v>>3)/(v>>2)); 0-diff impossible → REQUIRES one-time baseline refresh + visual review (user pre-approved).
- Modulate: tex565 * light565: split channels: r=(tr*lr)>>5... with light in [0..512] (2x overbright) → r6=(tr*lr+16)>>5... keep single-precision-ish, verify vs GL tolerance in new baselines.
- DOSBox present: if SDL3/VESA mode stays 32-bit XRGB, present converts 565→8888 (lossless expand) OR switch mode; measure both (conversion = 76.8K px × ~6 ops ≈ 15ms — may kill it; if so, keep internal 565 only for MODULATE win? NO — modulate win exists only if textures 565. Present conversion cost likely swamps → prefer VESA 16-bit mode in h2dos_platform; check current mode set in h2dos_ui.c/platform).
Order: (1) mode/present experiments first (cheap, isolates present win), (2) texture/fb format sweep, (3) baselines regen + visual sign-off, (4) log as one keep with refresh noted.

## 2026-08-17 LATE: WORLD DECOMPOSITION + UNIT-CORRECTION (post-#34 565-keep, total 140,937)
- BENCH UNITS: bench-frames=32. walked_world_px=2.94M is a TOTAL => 91.9K px/frame painted
  (~1.2x screen coverage; overdraw ~nil confirmed). Sky px ~46K/frame at ~10 ops/px = 16.8ms.
- WORLD 95,055us/frame = vis-walk 1.8ms (216 leaves, 494 marks, 401 draw_face visits)
  + face front-matter 6.4ms (16us/face: clip+project+gradients+lightbuild) + ROW/PX CORE 86.9ms.
  => px-loop runs ~25 ops/px. check-dos-bench BENCH-EVIDENCE kk per-frame avgs for us-values.
- METHOD: uclock() inside draw_face = PIT port reads = 25x dilation (frame 125ms->3.15s,
  32-frame cap). NEVER time inside hot fns; use counters + cvar ablations:
  sw_probe_skip_world_draw (world->1,792), sw_probe_skip_face_spans, sw_probe_skip_span_rows
  (world->8,238). NOTE: probes were reverted with experiment #35; add similar ones as needed.
- NOSPANS (triangle fallback) world=625ms => span engine is ~6.5x faster than raster_triangle.
- #35 W1 (block-uniform-light via monotone endpoint proof) DISCARDED: per-px lidx already
  cached; uniform blocks rarer than modeled (+1.6ms).

## REVISED QUEUE (by measured pool):
- W2: evict zb cmp+store for OPAQUE world px (-3 ops/px ~= -10ms). Blocker: current leaf
  visit order is arbitrary => without z-test overlap correctness breaks. Requires either
  front-to-back ordering (#31 showed its recursive ordering costs +8.5ms w/ span sort;
  ordering ALONE may be cheap if marks stay as-is and only draw order changes) OR per-block
  zb-bypass only when face known non-overlapping (rare). MEDIUM RISK.
- W3: sky per-px direction linearize: per-face-row s,t endpoints + linear inner step
  (basically 2 FADD/px instead of 5; folds basis math into row setup). est -3..5ms. LOW RISK.
- W4: present verification: 565 mode engagement + SDL same-format fast path (present bimodal
  13.1-14.5 - may drop ~4ms with rep-movsd confirmed). TRIVIAL.
- WORLD px-loop remaining: modulate3 (3 mul), texel row indexing, 5 fixed adds + ikey.
  25 -> ~20 theoretical max -20ms without semantic change. Consider: single accumulator
  packing (iu/it via combined stride), texel-row cache (t>>16&mask)*width hoisted per row-run.
- B2b (8-bit colormap LUT, sign-off needed) would be -30ms-class on world; deferred.
- STOP at 85ms looks hard without 8-bit; honest report cadence preserved.

## 2026-08-17 FINAL closes this segment (#35,#36 falsified; #37 kept -> total 139,863)
- #37 KEEP (ff3c5010): W5 direct-to-surface rendering. Window surface proven RGB565
  tight-pitch system RAM (W4 probe) -> sw_color aliases SDL surface pixels, present drops
  SDL_ConvertPixels. present 14.5->13.4ms. total 140,937 -> **139,863** (-19.8% vs 174,297).
- W4 learning banked: SDL_UpdateWindowSurface blit = 14ms fixed, ~same at 8888 for 2x bytes
  => VRAM store-bound ~11MB/s guest scale in vendored SDL DOS driver (not blitter logic).
  Halving bytes again needs 8bpp => ties into B2b (sign-off gated).
- #36 DISCARD: W3 sky 8px-tile coverage skip. sky -365us only, world marking +1.6ms,
  plus 57px gate fail (partial-span hole class). Falsifies "covered-sky walk is expensive".
- #35 DISCARD: W1 block-uniform light. per-px lidx already cached.
- PLATEAU STATEMENT: world px-core 86.9ms @ ~25 ops/px over 91.9K px is the floor under
  flat DOSBox (~27 instr/us) without changing per-px semantics. Modulate3 (3 muls) + zbuf
  cmp/store + index + 5 fixed adds + texel load + fb store ~= 25. Three independent
  micro-hoist angles falsified (B3b flex diet flat, W1, W3).
## Remaining levers (ranked):
  1) B2b 8-bit indexed + colormap LUT: world px ~10 ops + present blit bytes halved ->
     projected -35..45ms total, needs USER palette-fidelity sign-off.
  2) W2 zb eviction: saves ~1.7ms on this bench (overdraw ~1.2x), needs ordering; low value.
  3) Sky per-px trim: cache_row store per sky px (~46K); classification exactness gap.

## 2026-08-17 CARMACK-HAT CRACKPOT LIST (segment 3 closed: #38 MMX falsified; harness now cputype=pentium_mmx)
NEW RULER: pentium_mmx models real pipeline/stalls; #37 code re-measured frame 125,293 -> 137,816,
world 95,056 -> 97,522, present 13,496, clear 1,184. SEGMENT 4 MUST re-init_experiment + re-baseline
before next run_experiment (material contract change). px census: ~25 ops/px = zb(3-4) + texel idx(6)
+ texel load(1) + lidx(5) + modulate3≈10 + pack(3 folded) + fb store(1) + zb store(1) + 5 accum adds.
FALSIFIED AXES (do not retry): light LUT (+8.8ms), tw_shift pow2 (+4.2ms), block-uniform light (#35),
sky tile-masks (#36), pair-bookkeeping + asm MMX (#38: +6.4ms; harness models MMX at flat parity, no
IPC gain, compiler can't schedule across asm), surface cache (#23), view damage rects (camera moves).

### ACROSS PIXELS (save per-px work):
- P2 **scalar pair-pack dword store** [SMALL/FIRST]: in-scalar-loop pair: share lidx when equal,
  one u32 fb store per 2px. No asm/barrier. est -3..5ms new ruler. Opener experiment for segment 4.
- P1 **B1 REDUX: Q2-style edge-list span-sorting world** [BIG, ~2-3 days]: ref impl public:
  id-Software/Quake-2 ref_soft/r_edge.c+r_scan.c ("low depth complexity 1-3"; AEL scanned once per
  row; spans emitted per surface in BSP-walk order). Kills zb load+cmp+store on ~92K world px
  (-4..6 ops/px), sky zb walk entirely (16.8 -> ~5ms sentinel fill), depth-complexity exact 1.
  Est on NEW ruler: total ~151 -> ~115ms. Gate risk: painter-vs-z only for exact coplanar ties
  (2LSB budget absorbs). Prior #31 ordering attempt conflated machinery; edge list REPLACES it.
- P3 **Hecker exact integer edge walking** (Gdmtex3): fractional-edge DDA with positive-mod error
  terms; removes float from scan side-walk; correctness-crisp. est -1..2ms. LOW.
- P5 span-level single-light-cell fast path: W1 mechanism at coarser grain; likely same fate. SKIP.
- P6 **Descent-II scan doubling** (TMAP_P2 source: "scan doubling inner loop"): 2 rows per u,v walk.
  -45% world class BUT output change -> opt-in performance mode switch (user gate). PARK.
### ACROSS FRAMES (save per-frame work):
- F1 **sky-cache hit accounting + adaptive write**: bench camera moves every frame => cache stores
  (~46K stores, ~1.5ms) may be pure waste here, replay never hit. Measure hit counter; if 0, write
  cache only when camera basis == previous frame (cheap compare), keeping static-camera replay win.
- F2 viewleaf memo (1.8ms pool) / F3 face lightbuild (trivial): SKIP, bounded pools.
- F4 surface cache: FALSIFIED #23. F5 world/present decoupled rates: interactive change. PARK.
### REAL-HW (affects dad's PMMX-233 bench only, docs):
- Socket-7 BIOS "linear framebuffer write-combining/USWC" toggle: mirror of VOGONS quake 15->37fps
  anecdote; PMMX CPU lacks MTRRs (FastVid/SetMtrr are PPro+), so this is a CHIPSET BIOS item.
  Present path (13.5ms emulated) should collapse ~4x on the real box with WC on.
- Pairing (U/V) on real PMMX effectively halves the span loop CPI; nothing to code there, just note.

## 2026-08-17 USER-PROVIDED CRIME LIST — ENGINEERING TRIAGE vs MEASURED DATA
Ruler for all estimates: segment-4 pentium_mmx measurements (frame 137,816 / world 97,522 / present 13,496).
Census reminder: ~25ops/px world core {zb 3-4, texel idx 6, texel load 1, lidx 5, modulate3&pack ~8, stores 2, accum adds 5}.

1. 8-bit w/ 64-state CHROMATIC light vocab (8 brightness x 8 hue families, 16KB lut[shade<<8|texel],
   quantize at surface build): ACCEPTED AS THE B2b DESIGN FIX — solves the monochrome-cheat
   objection (Q2 used max(R,G,B)); quantize lidx cell -> 6-bit state at lightcache build, not per px.
   STILL SIGN-OFF-GATED (palette line classes: fonts/HUD/sprites must be palette-consistent).
2. Mode 13h 320x200: CONFLATE-with-caution. (a) world px volume -17% (-14ms class) YES;
   (b) "58% fewer present bytes" UNPROVEN HERE: 8888 (307KB) == 565 (153KB) == ~14ms measured
   => present path per-PIXEL or fixed-overhead bound, not byte-bound; 320x200 drops px 17% and may
   drop only ~2ms present. Probe INDEX8 present directly before believing the byte math.
   Output-class change -> sign-off (vertical FOV squash at equal FOV; menus re-letterbox).
3. Entity-tile-gated z writes (8x8 bbox mask): SOUND, composable with everything: world z STORE pool
   measured ~1-2 ops/px over 91.9K px => ~3-5ms total; tiles can cut ~60-80% => -2..4ms. Zero
   fidelity risk (exact z where written; entities only z-test their own tiles). MEDIUM. 1 day.
4. Half-res z: approximation, subset of #3 with correctness bleed at edges; prefer #3 first. PARK.
5. Temporal z: flicker risk on a moving camera; park as measurement-of-max-win only. SKIP for now.
6. Surface cache ("lighting in cache, never in raster"): FALSIFIED LOCALLY (#23) with our pow2
   texel path + torch-flicker dynamics; Q1's shape != ours. Do NOT re-open except maybe inside the
   indexed pipeline where cached stores halve (8-bit cached surfaces). Note dependency.
7. Alternating relights (Abrash): only meaningful if #6 exists. PARK with it.
8. Screen-door stipple for translucents: era-authentic (RoTT/Doom), exact win = kill dst-read+blend
   on affected px; MUST measure pool first (world-trans + flex alpha px on bench; entities=13.4ms
   quiescent total though). Output-class change -> sign-off. Cheap to prototype after pool probe.
9. SW_SPAN_STEP 16->32: block setup amortized => est -1ms; cheap w/ 2LSB watch. LOW value, do
   opportunistically with pair-pack.
10. Affine-model mapping/LOD: entities quiescent (bench holds them nearly still); semantic change;
    park until entities become the pool.
11. Interlace/checkerboard: emergency lever; doc only. NO.
12. No-clears: fb clear already tiny (1.18ms incl zb); B1 edge-list would make fb-clear removable
    (exact cover). Attach to P1 scope note.
13. Kernel specialization: partially present (pow2/opaque split); no branch-predict cost in harness;
    skip further splits. Self-patching SMT: no.

QUEUE (segment 4; single-experiment discipline):
 E39 P2 scalar pair-pack dword store [calibrates new ruler, -3..5ms]
 E40 F1 sky-cache hit counter + adaptive writes [-1.5ms, free]
 E41 z-tile entity mask [-2..4ms, zero-risk]
 E42 SW_SPAN_STEP 16->32 [~ -1ms, watch 2LSB]
 P1  B1 edge-list world = main event after queue [-18..25 world, -12 sky, kills clear]
 B2b indexed+chromatic lut [SIGN-OFF] [-35..45 projected]; mode13h 320x200 [SIGN-OFF] folds into it
 stipple [SIGN-OFF, after pool probe]
