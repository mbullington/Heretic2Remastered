# Segment 31 untried hypotheses

1. **Cross-map path-cost census.** Measure opaque-mask, opaque-affine,
   perspective/CLUT, and near-clipped setup separately on `sstown`, `hive1`, and
   `tutorial2`; select mechanisms only when the same path dominates broadly.
2. **Incremental integer edge setup.** Replace repeated floating scanline edge
   reconstruction for tiny opaque-affine triangles with exact fixed edge
   start/step records while retaining the current interpolation and pixel loop.
3. **Model command preprocessing by path facts.** Precompute only immutable
   winding/UV/skin facts at load time, not runtime corners or projection caches;
   Segment 29 proved runtime command caching loses.

# Segment 30 results

Four-wide light-cell groups, shade-row hoisting, uncovered-word coalescing,
dense mask points, and diagnostic subtraction were retained. Eight-wide groups,
patched texture shifts, and local counter aggregation were rejected. Combat
improved from 99.473 to 87.149 ms without renderer quality changes.

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
