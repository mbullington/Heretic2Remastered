# Untried hypotheses (remove when attempted)

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
## Ranked next levers
1. FLEX: convert span_raster_flex_triangle edge-stepping + span-start eval to fixed point (gradients stay float once/tri). Est 5-8ms.
2. BRUSH: per-face cheap cone cull DURING vertex loop in draw_face (bail before s/t mins + clip_near); entity faces skip min/max light scan when culled. Est 3-6ms.
3. WORLD inner loop (THE whale, 111ms spans for 108K px @ ~92cy): hand-schedule the opaque pow2 block loop in C first (split z-test loop from texel loop: test wiz-run z-pass mask over the block, then unconditional texel run for pass-runs) to cut z-compare FPU ops (wiz float compare per px!). wiz is FLOAT: the per-px depth compare is x87 FCOM... convert depth compare keys to int (beware negative wiz values ordering = int compare with sign flip... use bias+unsigned trick exact for floats? sign-magnitude: f>=0 -> key = bits; f<0 -> key = ~bits. all floats comp exact via int). Est 10-25ms if FPU tax real.
4. sky 38.2ms: flat-tile tagging via 8x8 tile attribute map written by clear, whole-tile fill where tags say uniform.
5. present 20-24ms: already XRGB8888; SDL converter wins. Try SDL_BlitSurface vs manual? or render sw_color directly into surface (locks LFB); blocked by SW depth/color as static buffers - END maybe skip.
## Ruled out (regressed on DOSBox flat-op model)
light LUT, tw_shift, hand present shuffle, int16 depth, ABGR mode
