# Segment 25 untried full-resolution hypotheses

1. **Block-endpoint projective warp** — compute exact source coordinates and
   reciprocal depth only at each 8-pixel block boundary, then walk fixed-point
   `(s,t)` and linear `qz` increments inside the block. The current kernel's
   comment promises this but its implementation still divides per pixel.
   Activate the redesigned kernel for the frozen 7.5-degree orbit and retain
   full-resolution color/depth.
2. **Owned dynamic-surface refresh** — snapshot or enumerate flowing, warped,
   animated, and nonzero-lightstyle world packets during the real frame. On a
   reused frame, redraw only those owned packets plus static faces intersecting
   actual edge-hole tiles. Do not traverse and conservatively overlap-test the
   complete BSP world a second time.
3. **Source-forward depth splats** — transform full-resolution snapshot samples
   into the new camera with source-depth reconstruction, z-test destination
   splats, and mark only uncovered pixels for repair. Compare this distinct
   translation-capable representation if backward mapping remains dominated by
   repair or cannot support camera translation.
4. **Stable blend transfer-function cache** — the deterministic combat overlay
   spends 12.494 ms applying a stable RGB565 transfer. Lazily cache complete
   65,536-entry transfer tables by quantized blend key with a small deterministic
   LRU, then reduce the hot loop to one indexed load and store. Pursue only after
   the temporal world path saves real time; guard general blend colors/alphas and
   account for table-build amortization.
