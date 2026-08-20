# Temporal reprojection experiment ideas

Untried hypotheses only. Remove each item when attempted or invalidated.

1. **Exact per-pixel rotational back-map.** The current 8-pixel endpoint solve
   linearly interpolates a projective source coordinate. Compute the divide per
   destination pixel to remove blockwise texture/edge wobble. Highest-fidelity
   prototype; expected cost increase but still bounded by the 125% guardrail.
2. **Whole-tile invalidation before repair.** A block is marked when any source
   sample is out of bounds or sky, but valid warped pixels/depth remain inside
   that 8x8 repair tile and can reject fresh geometry. Clear marked tiles first
   and expand by one tile so repair replaces, rather than composites with,
   unreliable boundary samples.
3. **Depth-discontinuity rejection.** Detect large source-depth jumps inside a
   warp block and send that tile to fresh repair. This targets foreground-edge
   ghosts without refreshing texture interiors.
4. **Bilinear color with conservative nearest depth.** Filter color only for
   samples whose 2x2 source depth neighborhood is continuous; retain nearest
   color at depth edges. This may reduce subpixel shimmer without bleeding
   foreground/background colors.
5. **One-warp anchor refresh.** Refresh the anchor after each warp rather than
   keeping it for three, but only if cadence can remain 13/32 through a
   snapshot-after-warp design; never feed warped pixels back without a fresh
   world repair proof.
