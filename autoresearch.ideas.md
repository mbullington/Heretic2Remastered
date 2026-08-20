# Segment 21 untried interactive-rendering hypotheses

1. **Assembly-backed two-phase world interleave** — the existing `r_swscan2`
   mode paints alternating two-column pairs, but it disables the fast world
   assembly lane and falls back to scalar per-pixel C. Add a fixed-ABI kernel
   that advances interpolation across skipped pairs while painting only the
   active pair. Preserve stale color/depth coherently for the opposite phase.
   This should turn a previously measured 4.3 ms mode into a meaningful world
   reduction without changing entities, HUD, or final 320x240 output.
2. **World-only half-resolution target** — render BSP world and sky at 160x120,
   upscale color and inverse depth to 320x240, then draw entities, particles,
   weapon, blend, and UI at native resolution. This removes roughly three
   quarters of world pixel work while keeping interaction-critical silhouettes
   and overlays sharp. Start with nearest/depth replication; add a cheap edge
   reconstruction only if the declared visual guard needs it.
3. **Fixed-point scanline temporal warp, only if reuse still has a purpose** —
   the current temporal path is slower because it performs one floating divide
   per destination pixel and then invokes a repair world pass. A replacement
   must use incremental fixed-point source coordinates with no per-pixel divide
   and a bounded repair budget. Do not tune the current full-resolution warp;
   leave it opt-in and prefer direct low-sample rendering if that is faster.
4. **World-only 2x2 variable-rate tiles** — if uniform half-resolution harms
   nearby walls, classify projected world packets into full-rate near/edge
   tiles and quarter-rate far/interior tiles before rasterization. The
   classification must be camera/general and its cost must be measured against
   simply rendering the 160x120 world.
