# Segment 28 untried hypotheses

1. **MMX register-resident interpolation pairs** — hold `(iu,it)`, `(ils,ilt)`,
   and their steps in MMX registers for an accepted run; extract with `movd`
   and shifted temporaries, advance with `paddd`, and spill only at return.
2. **MMX spill extraction** — if shifts/extracts dominate, store each packed
   pair to an aligned run-local scratch for integer address-generation loads;
   retain packed `paddd` updates and one final context spill.
3. **Scalar two-pixel state batching** — amortize global interpolation state
   updates over two pixels without MMX, accepting stack pressure only if the
   measured `emms`/extraction cost closes both packed designs.
4. **Next-segment fallback: flex strip/setup batching** — if all packed world
   state designs fail, reuse projected strip vertices and batch tiny-triangle
   setup exactly; do not revive real mesh LOD or disconnected surfels.
