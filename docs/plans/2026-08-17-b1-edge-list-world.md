# P1 / B1: Quake-style edge-list span-sorting world (segment 4 main event)

Status: DESIGN (approved as next structural experiment). Supersedes the "ordering bolt-on" framing
that failed as #31 — the edge list *replaces* ordering machinery; nothing is bolted on.

## Why this is the main event (measured, segment-4 pentium_mmx ruler)

- World px loop: ~25 ops/px over ~92.9K walked px/frame (2,969,413 total / 32 bench frames).
- zb load+cmp+store ≈ 4-6 of those ops ⇒ edge-list removal ≈ **−10..15ms world**.
- Sky's per-px `zb_row[x]==0` gate disappears (sky becomes the background sentinel fill):
  current sky_us=13,260 (step-32) ⇒ est **~4..6ms**.
- Framebuffer clear of world rows becomes unnecessary (exact cover) ⇒ share of clear_us=1,182.
  (zb still needed ONLY where entities must occlude: that's the *z-tile* follow-up, scoped below.)
- Total estimate on kept stack (141,807): **−16..25ms → ~115..125ms**.

## References (read first)

- id-Software/Quake-2 `ref_soft/r_edge.c`, `r_scan.c` — the canonical implementation
  (local cache: `~/.cache/checkouts/github.com/maraakate/q2dos/ref_soft/r_scan.c`).
  Comment that justifies the whole design: "low depth complexity — 1 to 3 or so".
- Abrash Black Book ch66-67 (hidden-surface removal, sorted spans), ch70 (Quake architecture).

## Current architecture (what exists today, refs in dos/ref_soft/ref_soft_raster.c)

- `SW_RasterWorld(fd)`: BSP/leaf walk → `draw_face()` per visible face → `draw_face_spans(...)`
  emits horizontal spans per row; per-px loop does zb compare/store, texel fetch, light modulate.
- Perspective subdivision already in place (`SW_SPAN_STEP 32`, kept E42; step-64 = gated-out).
- Sky: after world, writes where zb==0; plus working replay cache (12/20 hit/miss on bench).
- Entities read zb for occlusion (flex/brush/sprite paths).

## Design: five deltas

1. **Face collection, front-to-back.**
   Replace leaf-list iteration with recursive BSP traversal emitting faces in strict
   front-to-back BSP order into a per-frame face list (cap ~4K faces; overflow → fall back to
   today's path for the overflow tail and keep zb for that tail).

2. **Edge extraction per face.**
   For each emitted face, project its polygon edges to screen space; insert into a global edge
   pool, bucketed by first touched row (array of row heads). Edge = {x, xstep, leading_height,
   surface_id}. Subpixel: keep Abrash/Q2 leading-step form (0.5px bias consistent with current
   scan convention — VERIFY against existing `scan` setup before locking).

3. **AEL scan (one pass per row).**
   Global new-edge buckets feed an active-edge list; per row: insert, sort by x (insertion —
   nearly sorted), emit span boundaries, remove dead edges. Complexity /"depth 1-3"/ holds for
   BSP world content.

4. **Span emission per surface, no zb.**
   Per row, group AEL boundaries by surface (active surface stack à la Q2 `spanforplane`/
   `surfaces`); for each surface's span segment call the EXISTING `draw_face_spans` core with the
   zb test/store compiled out (new span-core variant: `zbwrite=0`). Painter correctness:
   BSP front-to-back + span splitting at edge crossings; coplanar tie pixels = only risk class,
   expected inside the 0.06% >2LSB budget (same texels, same light; winner differs only on exact
   seam equality). If fails: deterministic tie-break by surface id (stable, content-independent).

5. **Sky = sentinel background.**
   Pre-fill background with sky where no world span covers (evaluate: either sky-first full fill
   using the existing sky renderer with classification only at need — or keep sky-last but test a
   per-row coverage mask instead of per-px zb). Preference: sky-first + span coverage mask;
   removes the entire per-px sky zb gate.

## Non-goals / invariants

- Entities/particles/lights/blend/flex paths untouched; they z-test against zb as today.
- BENCH-EVIDENCE pins (bench- entities/flex/vars/dlights/tmp-world/semitone lines) must remain
  bit-identical — world decomposition counts (spans/blocks/walked) may shift; update pinned
  metric lines accordingly and document the mapping.
- Hard gates unchanged: RENDER/EFFECTS PPM ≤0.06% >2LSB.

## Staging (each stage = one experiment run; stop on any gate failure)

- S1: face collector + edge pool + AEL debug renderer (spans as flat per-surface debug colors,
  no texture) — verify coverage EXACTLY equals z-buffer-painted px set (compare masks offline:
  script diff of coverage bitmaps from both renderers, tolerance 0). No gate run yet (debug
  build only, local diff).
- S2: swap span core to textured/lighted no-zb variant; run experiment. Expect −8..12ms world.
  Gate risk: tie pixels. Mitigations ready: tie-break rule, or 0.5px edge bias flip.
- S3: sky sentinel path; run experiment. Expect sky ≈ ≤6ms.
- S4: remove world-row fb clear (exact-cover proof from S1); run experiment. ~−0.5ms.
- S5 (follow-up, separate): z-tile entity occlusion — zb filled ONLY inside 8x8 tiles stamped by
  entity screen-bboxes (computed in SW_RenderFrame pre-world from fd entity list); entity z-tests
  read tiles; uncovered world px write NO z at all. Pool: residual z writes ≈ 1 store/painted px
  under B1. Est −1..3ms + zb clear shrink. Zero fidelity risk (entities only test own tiles).

## Failure exits

- Tie-pixel class exceeds budget AND tie-breaks can't stabilize under 2LSB ⇒ keep today's
  z-buffer world permanently; damage limited to experiment time, all stages revert cleanly.
- AEL sort cost blows past 3ms (unexpected depth complexity) ⇒ cap active surfaces; measure.
