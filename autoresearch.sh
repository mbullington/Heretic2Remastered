#!/bin/bash
# Autoresearch verify: incremental DJGPP build + DOSBox COMBAT-scene benchmark
# (median of RUNS) + METRIC lines + T0 pm-mca deterministic pre-filter.
# Single-run drift (sky_us flips 36<->56ms) is why out-of-coverage runs take
# the MEDIAN: keep decisions out of the noise. In-coverage asm-lane runs use
# the pm-mca ruler (deterministic) and take ONE gate run.
set -euo pipefail
cd "$(dirname "$0")"
export H2DOS_COMBAT_RUN=COMBAT1

NIXPKGS="github:NixOS/nixpkgs/f2b17eb1b90f6a359082035b4538d4c63017aec3"
JOBS="${CMAKE_BUILD_PARALLEL_LEVEL:-8}"
# Only SILVER participates in the benchmark; building H2DOS doubles the
# 376-step gamex86 rebuild after .ninja_deps loss and wastes ~10 min/run.
APP_BUILD=".dos-build/heretic2-dos"

TIMEOUT_CMD="$(command -v gtimeout || command -v timeout || true)"
run_boxed() { if [ -n "$TIMEOUT_CMD" ]; then "$TIMEOUT_CMD" --kill-after=30s 720 "$@"; else "$@"; fi }
export NIXPKGS JOBS APP_BUILD

build_dos() {
  if [ ! -f "$APP_BUILD/build.ninja" ]; then
    CMAKE_BUILD_PARALLEL_LEVEL="$JOBS" bash scripts/build-dos-bootstrap.sh > /tmp/h2-ar-build.log 2>&1
  else
    nix shell "$NIXPKGS#djgpp_i586" "$NIXPKGS#cmake" "$NIXPKGS#ninja" -c \
      cmake --build "$APP_BUILD" --parallel "$JOBS" --target SILVER \
      > /tmp/h2-ar-build.log 2>&1
  fi
}
export -f build_dos
# djgpp cc1 occasionally wedges at 0% CPU for hours; time-box each attempt and
# retry (ninja resumes incrementally where the hung compile died).
run_boxed bash -c build_dos || { echo "build attempt 1 failed/hung, retrying" >&2; pkill -f i586-pc-msdosdjgpp 2>/dev/null; sleep 2; run_boxed bash -c build_dos; } || {
  tail -25 /tmp/h2-ar-build.log; exit 1; }

# --- T0: pm-mca static PMMX ruler (deterministic; early-discard gate) -------
# Coverage: only manifest-declared renderer files. Anything else => abstain to
# the old full 3-run path. REGRESS => exit 42 BEFORE any DOSBox run.
RUNS=3
PMCA_BASELINE=autoresearch.pmca-baseline.json
if [ -f scripts/pm_mca.py ] && [ -f scripts/pm_mca_manifest.json ]; then
  CHANGED=$(git diff --name-only HEAD -- dos src scripts include lib CMakeLists.txt 2>/dev/null | grep -v '^$' || true)
  OUT_OF_COVERAGE=$(python3 - "$CHANGED" <<'PYEOF'
import json, sys
changed = [c for c in sys.argv[1].splitlines() if c]
m = json.load(open("scripts/pm_mca_manifest.json"))
allowed = set(m.get("covered_files", [])) | {
    "scripts/pm_mca.py", "scripts/pm_mca_data.py", "scripts/pm_mca_manifest.json",
    "scripts/pm_mca_pathclass.py", "autoresearch.sh", "autoresearch.checks.sh"}
bad = [c for c in changed if c not in allowed]
print(";".join(bad))
PYEOF
)
  if [ -n "$OUT_OF_COVERAGE" ]; then
    echo "PMCA-T0-SKIP out-of-coverage: $OUT_OF_COVERAGE"
  else
    verdict=0
    python3 scripts/pm_mca.py report --exe .dos-build/guest/SILVER.EXE \
      --manifest scripts/pm_mca_manifest.json \
      --evidence .dos-build/guest/COMBAT-EVIDENCE.TXT \
      --baseline "$PMCA_BASELINE" --json /tmp/h2-ar-pmca.json || verdict=$?
    python3 scripts/pm_mca_pathclass.py
    if [ $verdict -eq 42 ]; then
      echo "PMCA-T0 REGRESS: deterministic PMMX ruler says worse; discarding before DOSBox"
      exit 42
    fi
    if [ $verdict -eq 43 ]; then
      echo "PMCA-T0 REBASELINE: ruler definition changed; adopting fresh reference, full verify"
      cp /tmp/h2-ar-pmca.json "$PMCA_BASELINE"
    elif [ $verdict -eq 44 ]; then
      echo "PMCA-T0 ABSTAIN: layout churn only; full 3-run trend path decides"
      # RUNS stays 3: the ruler has no vote, medians decide on the legacy path
    else
      RUNS=1   # in-coverage + deterministic metric: single gate run suffices
    fi
  fi
fi

# Benchmark run(s) on the COMBAT scene (SEG-7 workload: tutorial2 +
# angered planted monsters + deterministic fxload; counters bit-identical
# across reruns, timings median-ed over RUNS runs like the old bench).
for i in $(seq 1 "$RUNS"); do
  rm -rf .dos-build/runs/combat-COMBAT1
  python3 scripts/run-dos-combat.py > /tmp/h2-ar-bench-$i.log 2>&1 || {
    tail -30 /tmp/h2-ar-bench-$i.log; exit 1; }
  grep -q '^status=PASS$' /tmp/h2-ar-bench-$i.log || {
    tail -30 /tmp/h2-ar-bench-$i.log; exit 1; }
  cp .dos-build/guest/COMBAT-EVIDENCE.TXT /tmp/h2-ar-evidence-$i.txt
done
cp "/tmp/h2-ar-bench-$RUNS.log" /tmp/h2-ar-bench.log
EVFILES=$(for i in $(seq 1 "$RUNS"); do printf '/tmp/h2-ar-evidence-%s.txt ' "$i"; done)

# pm-mca succeeded and everything downstream passed: refresh the tracked ruler
# reference from this run (committed on keep, reverted on discard/crash).
if [ -f /tmp/h2-ar-pmca.json ] && [ "$RUNS" -eq 1 ]; then
  cp /tmp/h2-ar-pmca.json "$PMCA_BASELINE"
fi

python3 - $EVFILES <<'PYEOF'
import statistics
import sys

series = {}
for path in sys.argv[1:]:
    for line in open(path):
        if "=" in line:
            key, _, value = line.partition("=")
            key = key.strip()
            if key.startswith("bench-"):
                try:
                    series.setdefault(key, []).append(float(value.strip()))
                except ValueError:
                    pass  # bench-combat-* diagnostic strings are not metrics

EMIT = {
    "frame_us": "bench-avg-us-frame",
    "world_us": "bench-avg-us-world",
    "sky_us": "bench-avg-us-sky",
    "entities_us": "bench-avg-us-entities",
    "clear_us": "bench-avg-us-clear",
    "present_us": "bench-avg-us-present",
    "particles_us": "bench-avg-us-particles",
    "lights_us": "bench-avg-us-lights",
    "blend_us": "bench-avg-us-blend",
    "flex_prologue_us": "bench-avg-us-flex-prologue",
    "ent_flex_us": "bench-avg-us-ent-flex",
    "ent_sprite_us": "bench-avg-us-ent-sprite",
    "ent_dynamic_us": "bench-avg-us-ent-dynamic",
    "ent_variable_us": "bench-avg-us-ent-variable",
    "ent_line_us": "bench-avg-us-ent-line",
    "model_dynamic": "bench-model-dynamic",
    "model_variable": "bench-model-variable",
    "model_line": "bench-model-line",
    "ent_brush_us": "bench-avg-us-ent-brush",
    "flex_swaprefs_us": "bench-avg-us-flex-swaprefs",
    "flex_tris_us": "bench-avg-us-flex-tris",
    "flex_tris": "bench-flex-tris",
    "flex_tris_fast": "bench-flex-tris-fast",
    "flex_tris_clip": "bench-flex-tris-clip",
    "flex_tris_bbox": "bench-flex-tris-bbox",
    "flex_spans": "bench-flex-spans",
    "flex_blocks": "bench-flex-blocks",
    "brush_faces": "bench-brush-faces",
    "brush_culled": "bench-brush-culled",
    "walked_world_px": "bench-walked-world-px",
    "inside_world_px": "bench-inside-world-px",
    "walked_flex_px": "bench-walked-flex-px",
    "inside_flex_px": "bench-inside-flex-px",
    "flex_vertex_lerps": "bench-flex-vertex-lerps",
}

def as_number(v):
    try:
        return float(v)
    except ValueError:
        return None

numeric = {k: [n for n in (as_number(v) for v in vs) if n is not None]
           for k, vs in series.items()}
series = {k: v for k, v in numeric.items() if v}
for name, key in EMIT.items():
    if key in series:
        print(f"METRIC {name}={int(statistics.median(series[key]))}", flush=True)
for key in ("bench-world-walk-waste-pct", "bench-flex-walk-waste-pct"):
    if key in series:
        print(f"METRIC {key.replace('-', '_')}={statistics.median(series[key])}", flush=True)
print("status=PASS", flush=True)
PYEOF
