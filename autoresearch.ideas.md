# Segment 17 untried hypotheses

1. **Explicit-assembly BlendRect replacement** — the old intrinsic B1 kernel
   is retired after two user-visible catastrophic-corruption runs. Revisit only
   with a standalone full-buffer oracle and an assembly routine that proves ABI,
   register, MMX/x87-state, alignment, and tail behavior before integration.
2. **Assembly mixed-coverage world kernel** — extend the fixed-ABI kernel across
   partially covered words; current C fallback retains the per-pixel branch/
   counter tax there. Pool is bounded by 6.5K z-reject pixels/frame.
3. **World kernel context/register diet** — split light-cell runs in C or return
   a compact state so the assembly hot path keeps interpolation and pointers in
   registers instead of the 104-byte context. After stable-material patching,
   pm-mca still reports 48 AGI stalls and 80 cpi on the no-reload path.
4. **Flex PMMX run kernel** — four-wide exact RGB565 shade/store across
   contiguous z-pass runs, with scalar gather/depth arbitration. Entity pool is
   29 ms.
5. **Static-address blend assembly, not SMC** — the fullscreen blend maps have
   fixed storage addresses and 76,800-pixel amortization; use absolute operands
   in a safe scalar assembly loop rather than runtime code writes. Revisit only
   with the standalone full-buffer oracle required by item 1.
6. **Per-particle modulation maps, not SMC** — the 3.48 ms particle pool runs
   float `modulate()` over ~9,200 textured candidates/frame. Color-keyed channel
   maps or cached per-color texel tables attack the actual work; patching a few
   immediates would not remove the float conversions.
