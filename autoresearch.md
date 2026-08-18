# Autoresearch: minimize DOS soft-renderer frame time

## Objective and Workload
Minimize `total_us` = `bench-avg-us-frame` + `bench-avg-us-present` +
`bench-avg-us-clear` of the Heretic II DOS software
(render+present per frame; bench times present/clear outside the frame window —
intent was always render+present; scope corrected 2026-08-17 when the
framebuffer-layout flip exposed the seam by moving −11.7ms of memcpy through it).
Legacy series: pre-fix experiments logged frame_us only; old totals = frame+present+clear.
renderer measured by the fixed benchmark: DOSBox Staging headless,
`cycles=fixed 120000` (≈ Pentium-90-class), real production PAKs
(gameplay UNVERIFIED; renderer benchmark only). The workload renders 64
fixed frames of a flex-model-heavy tutorial scene (99 models, ~5.2K tris,
rotating sky) via `.dos-data/` real-data mode.

## Metric Contract
- **Primary**: total_us (µs, lower is better) — per-frame render+present+clear time.
- **Also tracked**: frame_us (legacy primary; bench-avg-us-frame alone).
- **Secondary guardrails**: benchmark must report `status=PASS`; RENDER.PPM
  and EFFECTS.PPM vs stored baselines must have ≤ 0.06% of pixels differing
  by > 2 LSB (only coincident-depth z-fight tie flips are allowed, and they
  must stay full-color swaps — count enforced by the gate, pattern noted in
  ASI when they move).
- **Secondary tie-breakers / telemetry**: world_us, sky_us, entities_us,
  clear_us, present_us, walked_world_px, inside_world_px, flex_vertex_lerps,
  flex_prologue_us.
- DOSBox fixed-cycle timing drifts (sky_us observed flipping 36<->56ms
  across identical builds), so `autoresearch.sh` runs the bench THREE times
  and reports per-metric MEDIANS. ~90s per verify.

## How to Run
`./autoresearch.sh` — incremental DJGPP build (nix toolchain), then the
THREE DOSBox benchmark runs, then prints per-metric MEDIAN lines. ~90s per verify.
`autoresearch.checks.sh` refreshes guest RENDER/EFFECTS PPMs via
`scripts/run-dos-renderer.py` (the bench only rewrites BENCH-EVIDENCE.TXT and
BENCH-CLIENT.PPM) and then runs the pixel/status gates; `check-dos-bench.py`
refuses PPMs older than guest SILVER.EXE so stale pixel artifacts can never
pass or fail silently. (Added 2026-08-17 after a control run proved the gate
had been comparing a stale RENDER.PPM.)

## Files in Scope
- `dos/ref_soft/ref_soft_raster.c` — span engines, walkers, draw_* paths
- `dos/ref_soft/ref_soft_main.c`, `ref_soft.h` — frame orchestration
- `dos/ref_soft/ref_soft_framebuffer.c` — present path
- `dos/ref_soft/ref_soft_image.c`, `ref_soft_model.c`, `ref_soft_flex.c`,
  `ref_soft_local.h`, `ref_soft_public.h` — supporting renderer code
- `dos/h2dos_main.c` — ONLY the bench-mode instrumentation/counters
- `scripts/check-dos-bench.py`, `scripts/run-dos-benchmark.py` — harness
  metrics only (do NOT loosen gates or change the workload)

## Off Limits
- `src/` game logic, client, server, shared headers. EXCEPTION:
  `src/client/glimp_sdl3.c` may receive H2_DOS-ifdef'd DOS video-mode
  selection changes only.
- BSP/model loading semantics, network, sound
- The `r_swspans 0` fan/fallback paths: keep them functionally intact
- `vendor-*`, `.dos-deps`, `.dos-data`, toolchains; never touch PAK data
- Do not add proprietary game data or change the benchmark scene/camera

## Hard Constraints
- status=PASS from the bench harness (heads-up vectors, projection, checksums)
- Pixel gate: ≤0.06% of pixels >2 LSB in each of RENDER/EFFECTS vs baselines
- No race/UB shortcuts: DOSBox timing is not proof of real-hardware speed —
  keep instruction counts and memory traffic as the honest proxy
- C style: this file compiles as C99 under DJGPP gcc 12 with -Wall -Werror

## Stop Conditions
- total_us ≤ 85,000 (≈ frame_us 60,000 + present ~19,000 + clear ~6,000)
- 3 consecutive discards with no credible mechanism left, or user's call

## Current state
- SEGMENT 4 (2026-08-17): DOSBox cputype=pentium_mmx (9307dbd1) — new realistic ruler; seg-3 numbers not comparable.
  Kept stack: commit 1797d154 (E42 span-step 32). Best total_us = 141,807 (frame 123,960, world 96,197,
  sky 13,260 [ASI], entities 13,484, clear 1,182, present 16,665-bimodal).
  Falsified seg-4: E39 pair-pack (+7.2ms), E43 step-64 (fidelity ceiling: 32<step<64; banked as opt-in turbo).
  Stop line: total_us <= 85,000 unchanged.
- MAIN EVENT NEXT: B1 edge-list world — staged plan at docs/plans/2026-08-17-b1-edge-list-world.md
- Gated user sign-offs granted 2026-08-17: indexed 8-bit chromatic light vocab; Mode 13h 320x200
  (must remain a mode sibling to 640x480). Stipple: NOT granted.
