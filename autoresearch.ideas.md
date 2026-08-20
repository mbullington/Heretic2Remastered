# Segment 13 untried hypotheses

Only exact-output PMMX world-kernel hypotheses remain here. Temporal-quality
history is closed in autoresearch.jsonl; earlier renderer falsifications remain
in git and prior segment records.

1. **Fixed-ABI scalar assembly paint kernel** — extract the common opaque FTB,
   power-of-two, uncovered/no-light-cell-change pixel path behind strict
   preconditions. Keep the C path for mixed coverage, alpha-test, stripe/debug,
   and light-cell changes. Hand-schedule address generation to remove the
   compiler path's dominant AGI stalls; preserve shade-row LUT, coverage,
   depth, counters, and RGB565 output exactly.
2. **Assembly mixed-coverage kernel** — after (1), add bit-test/z-reject handling
   to cover mixed words without returning to C per pixel. Only attempt if the
   first kernel proves call/dispatch overhead is amortized.
3. **Patched per-face constants** — specialize a copied static kernel template
   with texture/light bases, masks, strides, and shade-row pointers to free
   registers and remove loads. Requires an emit-site face/switch census and a
   demonstrated patch/amortization budget; no writable-code implementation
   before that evidence.
4. **Flex PMMX run kernel** — four-wide RGB565 shading/store across contiguous
   z-pass runs. This attacks the 28 ms entity pool after the world kernel and
   retains scalar gather/depth arbitration for exactness.
