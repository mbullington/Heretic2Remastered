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
