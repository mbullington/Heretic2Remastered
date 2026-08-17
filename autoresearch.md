# Autoresearch: minimize DOS soft-renderer frame time

## Objective and Workload
Minimize `frame_us` (bench-avg-us-frame) of the Heretic II DOS software
renderer measured by the fixed benchmark: DOSBox Staging headless,
`cycles=fixed 120000` (≈ Pentium-90-class), real production PAKs
(gameplay UNVERIFIED; renderer benchmark only). The workload renders 64
fixed frames of a flex-model-heavy tutorial scene (99 models, ~5.2K tris,
rotating sky) via `.dos-data/` real-data mode.

## Metric Contract
- **Primary**: frame_us (µs, lower is better) — per-frame render+present time.
- **Secondary guardrails**: benchmark must report `status=PASS`; RENDER.PPM
  and EFFECTS.PPM vs stored baselines must have ≤ 0.06% of pixels differing
  by > 2 LSB (only coincident-depth z-fight tie flips are allowed, and they
  must stay full-color swaps — count enforced by the gate, pattern noted in
  ASI when they move).
- **Secondary tie-breakers / telemetry**: world_us, sky_us, entities_us,
  clear_us, present_us, walked_world_px, inside_world_px, flex_vertex_lerps,
  flex_prologue_us.
- DOSBox fixed-cycle timing has run-to-run drift; treat < ~3% deltas as
  within noise unless a mechanism explains them. Keep decisions honest with
  the confidence score.

## How to Run
`./autoresearch.sh` — incremental DJGPP build (nix toolchain), then the
DOSBox benchmark, then prints METRIC lines. Takes ~10 minutes (single run;
workload is slow and deterministic-ish, medians are not affordable).
`autoresearch.checks.sh` runs the pixel/status gates on success.

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
- Everything under `src/` (game logic, client, server, shared headers)
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
- frame_us ≤ 60000 (≈ 4× from the 223 ms contract baseline)
- 3 consecutive discards with no credible mechanism left, or user's call
