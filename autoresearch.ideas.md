# Segment 18 untried hypotheses

1. **Complete default affine opaque-flex polyset** — remove the existing
   perspective block machinery for the default opaque path. Compute screen-space
   s/t/R/G/B and depth gradients once per triangle, convert once per scanline,
   and run one fixed-point span. The historical all-affine prototype saved about
   12 ms and changed only 211 gate pixels; the current 2%/32 dB quality contract
   explicitly admits this class if motion captures remain unobjectionable.
2. **Exact z-tested brush assembly sibling** — brush entities spend 5.09 ms in
   the generic C world span path because they cannot use the front-to-back
   coverage-mask kernel. Reuse the fixed world ABI and material patches with a
   per-pixel depth compare, retaining C for alpha/non-pow2/debug/stripe paths.
3. **Early visible-triangle command streams** — avoid three projection-cache
   lookups and signed-area work for the roughly 620 bbox/backface candidates
   rejected before 510 dispatched triangles/frame. Prefer conservative
   model-frame/view-bin visible streams or a cheaper exact pre-pass; reject if
   conservative expansion or selection overhead consumes the pool.
4. **Unclassified entity/sprite pool** — entities exceed timed flex+brush+model
   sprites by about 3.36 ms. Add only coarse outer classification if the first
   three mechanisms leave the 2x target unmet, then specialize the measured
   dynamic/variable/line sprite winner with a direct depth-tested row/stamp path.
5. **Whole P55C flex assembly** — if affine C remains setup-bound, move triangle
   gradient, 12-case edge selection, fixed edge walking, and span draw into one
   fixed-ABI assembly routine. Schedule integer edge work under x87 divides and
   patch only stable skin constants. This is a fallback after the simpler
   architecture quantifies the remaining pool, not a prerequisite.
