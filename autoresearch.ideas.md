# Segment 26 untried exact-transfer hypotheses

1. **Complete transfer-function LRU** — lazily build exact 65,536-entry RGB565
   destination→output tables keyed by `(source565, alpha8)`. A 16-slot bounded
   cache covers the deterministic 16-step damage pulse after warm-up. The hot
   loop becomes one indexed load and one store; allocation failure retains the
   existing exact channel-map loop. Measure the 2 MiB cache's P55C locality and
   include cold-build cost in the frozen run.
2. **Page-local two-level transfer cache** — if a 128 KiB table misses L1 too
   often, cache exact 256-entry low-byte pages under the stable high-byte/source/
   alpha key, or derive a decomposition whose active pages remain near the
   framebuffer's color distribution. It must remain exact for all 65,536 input
   colors and avoid per-pixel branches that cost more than the current 4 KiB
   RG map.
3. **Verified P55C assembly lane** — only if data-shape approaches fail, write a
   fixed-ABI 4-pixel packed kernel using P55C/MMX instructions and an exhaustive
   startup oracle against scalar output. The retired intrinsic kernel's green
   corruption means no unchecked reuse of that implementation.
