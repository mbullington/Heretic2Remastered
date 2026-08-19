# Autoresearch: minimize DOS soft-renderer frame time

## SEG-7 (active): PMMX ruler (pm-mca) + staged harness + COMBAT workload

Harness/metric/workload contract changed at the user's request (2026-08-19):
- **Workload**: tutorial2 is no longer quiescent: deterministic combat driver
  (gorgon + rat_giant planted beside the stationary player, `angermonsters`),
  orbiting camera (H2DOS_SWEEP), and renderer-boundary fxload (64 typed
  particles + 2 dlights + damage-blend pulse per frame). Pools now exercised:
  particles 34ms / lights 14ms / blend 70ms / entities 41ms. Counters +
  CLIENT.PPM bit-identical across reruns. Baseline counters:
  autoresearch.combat-baseline.txt.
- **Primary metric: `pmca_est_us`** = Σ(cycles/iter × trips/frame) over the
  pm-mca manifest loops, scaled to PMMX-233 clocks. pm-mca is a STATIC
  P55C instruction-window analyzer (U/V pairing, AGI, bank conflicts, x87
  overlap, MMX resources, decode rules; tables cited from Agner Fog's 2025
  manuals; selftest 16/16 incl. published schedules). Deterministic => no
  medians needed. scripts/pm_mca_manifest.json is the coverage contract.
  Build-state baseline: pmca_est_us=335 (78,070 cycles), flex cpi 89/79.
- **Harness stages**: T0 = pm-mca report (sub-second; exit-42 early-discard
  on deterministic regression; coverage gate routes foreign diffs to the
  legacy path). T1 = ONE combat run + gates when in-coverage. T2 = 3x medians
  for out-of-coverage/trend. T3 = checkpoint-cadence robustness, currently
  the OLD static tutorial2 scene (scripts/run-dos-benchmark.py).
- DOSBox trend telemetry (total_us et al.) remains tracked for seg chain,
  now from combat evidence.

## SEG-6 (archived 2026-08-19): 320x240 RGB565 + correctness overhaul

Bench/display contract changed at the user's request: 320x200 INDEX8
(quantized, palettized) -> 320x240 RGB565 (direct color). Seg-5 numbers are
not comparable. Baseline #54 (df1a0562): total 178,391 / frame 161,394 /
world 99,071 / entities 47,325 (flex 39.2 from now-visible player model,
640 tris/frame) / sky 15,164 / present 15,816 / clear 1,181.
Gates baselines refreshed once because the overhaul is intentional visual
change (z-domain rewrite, deferred trans pass, alpha-test colorkey guard,
lightmap layer index, flex iz1/scan-typo/cutout-depth, local brush texcoords,
exact sky cache, E50 removed, player-model alias); real visuals = user's eyes.

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

## SEG-7 handoff-agenda close-out (2026-08-18)
Baseline 15239169 (user correctness batch) 206,764 -> KEPT chain: 1b7108de E59.1 FTB world order (-15.9),
cdba5907 E55c bit-exact opaque flex (-2.75), 65a2e2c2 E58 baked render packets (-4.98), 799b8071+a8997ffb E61
(r_swtemporal infra bit-exact OFF + segment-affine warp) => primary BEST 183,074 (-11.4%); TURBO MODE
r_swtemporal=1: 164.5 (-20.4% vs baseline). rgb-affine turbo: SKIPPED per user (banked docs-only).
Falsified/skipped per ideas.md seg-7 section: E60 (gate red + regression), E57 (wash), E62 (wash),
E63/E64 (pool math), E65/E66 (quiescent pools). Turbo backlog in ideas.md (hole dilation, snapshot diet).


## Current state
- SEGMENT 4 (2026-08-17): DOSBox cputype=pentium_mmx (9307dbd1) — new realistic ruler; seg-3 numbers not comparable.
  Kept stack: commit 1797d154 (E42 span-step 32). Best total_us = 141,807 (frame 123,960, world 96,197,
  sky 13,260 [ASI], entities 13,484, clear 1,182, present 16,665-bimodal).
  Falsified seg-4: E39 pair-pack (+7.2ms), E43 step-64 (fidelity ceiling: 32<step<64; banked as opt-in turbo).
  SEG-4 CLOSE-OUT 2026-08-18 @ commit 56a18b61: final 565 total_us = 138,676. **B2b indexed internals reverted**: structurally unfittable for real H2 content (measured PAK evidence: ~1 palette per m8; 2016 distinct palettes in Htic2-0, silverspring 195/199) — no single 256-entry global palette can represent the game; user visual gate red twice (magenta = demote-path channel bug fixed, then structural finding). Learnings banked in jsonl: light-LUT chromatic vocab works; 565->565 memcpy present floor 17ms @240. B1 stage-2 hybrid clip falsified (#47): unsorted +11ms no payoff; sorted = gate-red 2.0% on coplanar painter ties (E44+E47). World span level = architectural optimum under painter semantics; HZB/subtree world ideas share that fate.
- SEGMENT 5 (2026-08-18): user-authorized 320x200 sibling mode (env-driven: H2DOS_SW_WIDTH/HEIGHT; DOS VESA driver offers 320x200 ONLY as INDEX8 -> fixed 3-3-2 palette present with 64KB 565->index LUT; ladder falls back to 565 @320x240). Baseline 116,940 (7466deaf). Kept: E50 flex screen-space backface cull (entities -1.2ms; flex px -17.5%), E51 world span-step 128 (world -446us, gate-green, no walked-px inflation). Best: 115,301 (0ae08484).
- Meter reading for target iron (PMMX-233): harness cycles 120000 ~= P120-class; PMMX-233 ~= 2.2x that: seg-5 115ms @320x200 projects to ~53ms ~= 19fps on real 233MHz hardware at Mode-13h-class settings.
- Stop condition total_us <= 85,000 (seg-3 contract, subsumed): seg-5 floor analysis says px-bound 565 world + LFB-write-bound present cannot reach it at 320x200 without a resolution/format break; campaign at credible-hypothesis exhaustion pending user call.
- Gated user sign-offs granted 2026-08-17: indexed 8-bit chromatic light vocab; Mode 13h 320x200
  (must remain a mode sibling to 640x480). Stipple: NOT granted.
