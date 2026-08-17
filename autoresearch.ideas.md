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

