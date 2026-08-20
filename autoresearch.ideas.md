# Segment 18 untried representation hypotheses

1. **Whole-model surfel renderer** — replace opaque triangle rasterization with
   one projected sample per visible `(vertex, skin-uv)` surfel. The triangle
   command stream is read only to establish visibility and projected footprint;
   no per-triangle gradients, scan edges, spans, or perspective blocks remain.
   Draw depth-tested diamond footprints with per-vertex texture/light samples.
   Keep exact triangle fallbacks for clipped, translucent, skeletal, special
   mesh-color, and near/large models. This is the first order-of-magnitude-work
   prototype, not another widened microtriangle mask.
2. **Compact object-space 2.5D impostors** — if surfels cannot meet silhouette
   quality, precompute sparse span/depth/UV-normal packets for finite animation
   frames and quantized view directions at registration. Runtime work becomes
   one packet stream/model instead of hundreds of triangles. Quantify frame
   count, view-bin memory, animation interpolation, and lighting ownership
   before implementation; use exact fallback above a projected-size threshold.
3. **Whole P55C flex assembly only if representations fail** — assembly can
   schedule x87 gradients and integer edges but does not eliminate the 500
   triangle / 1,900 span workload. It is no longer the primary route to 2x.
