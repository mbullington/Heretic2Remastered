# Segment 15 untried hypotheses

1. **Explicit-assembly BlendRect replacement** — the old intrinsic B1 kernel
   is retired after two user-visible catastrophic-corruption runs. Revisit only
   with a standalone full-buffer oracle and an assembly routine that proves ABI,
   register, MMX/x87-state, alignment, and tail behavior before integration.
2. **Assembly mixed-coverage world kernel** — extend the fixed-ABI kernel across
   partially covered words; current C fallback retains the per-pixel branch/
   counter tax there. Pool is bounded by 6.5K z-reject pixels/frame.
3. **World kernel context/register diet** — split light-cell runs in C or return
   a compact state so the assembly hot path keeps interpolation and pointers in
   registers instead of the 104-byte context. pm-mca still reports 48 AGI stalls
   and 85 cpi on the no-reload path.
4. **Flex PMMX run kernel** — four-wide exact RGB565 shade/store across
   contiguous z-pass runs, with scalar gather/depth arbitration. Entity pool is
   29 ms.
5. **Per-face patched constants** — only after an emit-site face/switch census
   proves patch amortization; specialize the world kernel's bases/masks/strides
   to free registers. No writable-code dependency without that evidence.
