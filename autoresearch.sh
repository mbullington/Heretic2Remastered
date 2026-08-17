#!/bin/bash
# Autoresearch verify: incremental DJGPP build + DOSBox benchmark + gate-checks.
# Prints METRIC lines parsed by the autoresearch harness. ~10 min per run.
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

rm -rf .dos-build/runs/bench-BENCH1
python3 scripts/run-dos-benchmark.py > /tmp/h2-ar-bench.log 2>&1 || {
  tail -30 /tmp/h2-ar-bench.log; exit 1; }
grep -q '^status=PASS$' /tmp/h2-ar-bench.log || { tail -30 /tmp/h2-ar-bench.log; exit 1; }

python3 scripts/check-dos-bench.py metrics
