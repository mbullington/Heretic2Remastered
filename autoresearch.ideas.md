# Untried representation hypotheses after player correctness

1. **Whole-model surfel renderer** — replace opaque triangle rasterization with
   one projected sample per visible `(vertex, skin-uv)` surfel. The triangle
   command stream is read only to establish visibility and projected footprint;
   no per-triangle gradients, scan edges, spans, or perspective blocks remain.
   Draw depth-tested diamond footprints with per-vertex texture/light samples.
   Keep exact triangle fallbacks for clipped, translucent, skeletal, special
   mesh-color, and near/large models. This is the first order-of-magnitude-work
   prototype, not another widened microtriangle mask.
2. **Registration-time sparse impostor packets** — the runtime-built dense
   48x48 cache was tried and rejected after 146 misses versus 127 hits made
   entities 5.1ms slower. A distinct retry must precompute sparse depth/UV
   packets during registration and prove a stable memory bound before coding;
   it must not rebuild dense images on runtime misses.
3. **Whole P55C flex assembly only if representations fail** — assembly can
   schedule x87 gradients and integer edges but does not eliminate the 500
   triangle / 1,900 span workload. It is no longer the primary route to 2x.
