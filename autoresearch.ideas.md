# Segment 27 untried representation hypotheses

1. **Material-local exact shade rows** — retain original byte texels and each
   M8 wall texture's own 256-entry RGB565 palette. Cache the exact 256-result
   function for `(image, packed light)` and select its row when the world kernel
   reloads a light cell. First prove M8 accepted-pixel coverage and key reuse.
2. **Compact exact world-depth runs** — opaque static world already resolves
   visibility front-to-back. Record each accepted interval as
   `(row, x0, x1, key0, key_step)`, verify every reconstructed key against the
   current z-buffer, then let later primitives query runs instead of paying a
   32-bit world depth write at every pixel.
3. **Combined color-only static-world kernel** — if both representations win,
   specialize the existing fixed ABI world painter for byte texels, one shade
   row lookup, RGB565 color stores, coverage, and depth-run emission. Keep the
   current exact RGB565/full-depth sibling for every unsupported path.
