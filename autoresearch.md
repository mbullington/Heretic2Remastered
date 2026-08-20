# Autoresearch: improve DOS temporal reprojection quality

## Objective and Workload
Reduce visible shimmer, geometry wobble, stale-edge smears, and holes from
`r_swtemporal` while retaining its reuse cadence. The ruler runs the real
`tutorial2` scene twice at 320x240 RGB565 with a deterministic 1-degree yaw per
rendered frame and a fixed camera origin: temporal OFF is the full-render
reference; temporal ON is the candidate. Both runs use the same executable,
clock, PAKs, frame sequence, and 32 paired PPM captures.

Ruler sensitivity was established before baseline: full-render vs itself is
0% error; the existing temporal path produces 1.516886% pixels/frame whose
largest RGB channel error exceeds 8/255, with a 6.673178% worst frame and 13
warped frames. This separates exact output from the reported artifact class.

## Metric Contract
- **Primary**: `temporal_bad_pct` (%, lower is better) — fraction across all
  paired frames whose largest RGB-channel error against the matched full render
  is greater than 8/255 (roughly more than two RGB565 quantization steps).
- **Secondary guardrails**:
  - exactly 13 temporal warps per 32 captured frames; cadence reductions cannot
    buy quality;
  - candidate average frame cost must be no more than 125% of its matched
    full-render reference;
  - `temporal_max_frame_bad_pct` must not exceed 7%;
  - renderer proof/status/pixel gates in `autoresearch.checks.sh` must pass.
- **Secondary tie-breakers**: `temporal_changed_pct`, `temporal_mae`,
  `temporal_p95`, candidate/reference frame and world times.

## How to Run
`./autoresearch.sh` builds `SILVER.EXE`, captures the paired workload, and emits
`METRIC name=value` lines. Captures are retained under
`.dos-build/temporal-quality/{reference,candidate}` for direct inspection.

## Files in Scope
- `dos/ref_soft/ref_soft_raster.c` — temporal scheduler, snapshot, warp,
  rejection/repair, and quality-ruler rotation hook
- `dos/ref_soft/ref_soft_public.h`, `dos/h2dos_main.c` — temporal cadence
  telemetry and deterministic diagnostic camera controls only
- `scripts/run-dos-temporal-quality.py` — paired capture and image metric
- `autoresearch.sh`, `autoresearch.checks.sh` — build/ruler and correctness
  checks
- `autoresearch.md`, `autoresearch.ideas.md` — experiment contract/backlog

## Off Limits
- Gameplay, BSP/model loading, networking, sound, PAKs, vendor/toolchain data
- Reducing temporal cadence, angle step, capture count, comparison threshold,
  or image region after baseline
- Degrading or changing the temporal-OFF reference renderer to resemble reuse
- Hiding errors with a post-process blur or changing output resolution/format

## Hard Constraints
- Same executable and deterministic scene/frame sequence for reference and
  candidate captures
- Exactly 13 warped frames and unchanged 1-degree workload
- Candidate frame cost <=125% of matched full-render cost
- Existing renderer status and pixel-equivalence gates pass
- C99 DJGPP build with `-Wall -Werror`; no UB or race shortcuts

## Stop Conditions
Stop when `temporal_bad_pct <= 0.5%` and worst-frame bad pixels <=3%, after 8
measured iterations, after 3 consecutive discards with no distinct mechanism
left, on user interruption, or at a tooling dead end.
