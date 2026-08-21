# Segment 29 results

1. **Mesh-batched projection table — rejected.** Logical projection reuse was
   high (99,580 unique mesh vertices versus 483,615 flattened corner
   references), but direct cache indexing improved frame and flex-triangle time
   by only 30 us, within noise.
2. **Lazy command-corner cache — rejected.** Post-cull triangles retained
   34.81% corner reuse, but a per-entity command-ID cache regressed flex
   triangles by 180 us and frame time by 148 us.
3. **Group-local rolling strip/fan cache — rejected.** Restricting state to the
   current command group avoided the large random cache, yet regressed flex
   triangles by 224 us and frame time by 226 us.

Command and projection lookups are not the expensive part of the 17.863 ms
triangle pool. Do not revisit exact strip/fan corner caching without a design
that also removes substantial scan-conversion setup. A future segment should
measure and replace the edge/gradient/span setup representation itself.
