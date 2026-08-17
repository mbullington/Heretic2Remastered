#!/bin/bash
# Autoresearch verify: incremental DJGPP build + DOSBox benchmark (median of 3)
# + METRIC lines. ~90s. Single-run drift (sky_us flips 36<->56ms) is why we
# take the MEDIAN: keep decisions out of the noise.
set -euo pipefail
cd "$(dirname "$0")"

NIXPKGS="github:NixOS/nixpkgs/f2b17eb1b90f6a359082035b4538d4c63017aec3"
JOBS="${CMAKE_BUILD_PARALLEL_LEVEL:-8}"
APP_BUILD=".dos-build/heretic2-dos"

if [ ! -f "$APP_BUILD/build.ninja" ]; then
  CMAKE_BUILD_PARALLEL_LEVEL="$JOBS" bash scripts/build-dos-bootstrap.sh > /tmp/h2-ar-build.log 2>&1 || {
    tail -25 /tmp/h2-ar-build.log; exit 1; }
else
  nix shell "$NIXPKGS#djgpp_i586" "$NIXPKGS#cmake" "$NIXPKGS#ninja" -c \
    cmake --build "$APP_BUILD" --parallel "$JOBS" --target H2DOS SILVER \
    > /tmp/h2-ar-build.log 2>&1 || { tail -25 /tmp/h2-ar-build.log; exit 1; }
fi

# Three benchmark runs; collect evidence copies per run, take per-metric medians.
for i in 1 2 3; do
  rm -rf .dos-build/runs/bench-BENCH1
  python3 scripts/run-dos-benchmark.py > /tmp/h2-ar-bench-$i.log 2>&1 || {
    tail -30 /tmp/h2-ar-bench-$i.log; exit 1; }
  grep -q '^status=PASS$' /tmp/h2-ar-bench-$i.log || {
    tail -30 /tmp/h2-ar-bench-$i.log; exit 1; }
  cp .dos-build/guest/BENCH-EVIDENCE.TXT /tmp/h2-ar-evidence-$i.txt
done
cp /tmp/h2-ar-bench-3.log /tmp/h2-ar-bench.log

python3 - /tmp/h2-ar-evidence-1.txt /tmp/h2-ar-evidence-2.txt /tmp/h2-ar-evidence-3.txt <<'PYEOF'
import statistics
import sys

series = {}
for path in sys.argv[1:]:
    for line in open(path):
        if "=" in line:
            key, _, value = line.partition("=")
            key = key.strip()
            if key.startswith("bench-"):
                series.setdefault(key, []).append(float(value.strip()))

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
    "walked_world_px": "bench-walked-world-px",
    "inside_world_px": "bench-inside-world-px",
    "walked_flex_px": "bench-walked-flex-px",
    "inside_flex_px": "bench-inside-flex-px",
    "flex_vertex_lerps": "bench-flex-vertex-lerps",
}
for name, key in EMIT.items():
    if key in series:
        print(f"METRIC {name}={int(statistics.median(series[key]))}", flush=True)
for key in ("bench-world-walk-waste-pct", "bench-flex-walk-waste-pct"):
    if key in series:
        print(f"METRIC {key.replace('-', '_')}={statistics.median(series[key])}", flush=True)
print("status=PASS", flush=True)
PYEOF
