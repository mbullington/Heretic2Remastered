# Segment 29 untried hypotheses

1. **Mesh-batched projection table** — after exact mesh bbox acceptance, project
   each unique mesh vertex once and let triangles index the entity-sequenced
   cache directly instead of calling/checking the lookup three times each.
2. **Original glcmd strip/fan walker** — decode each command vertex once, retain
   strip predecessors or fan anchor, and emit triangles in the exact flattened
   parity/order while reusing projected and UV-bearing command records.
3. **Path-local command-corner cache** — if direct glcmd traversal loses, cache
   affine/perspective corner products by command occurrence and path class while
   retaining flattened triangle dispatch and all clipped fallbacks.
