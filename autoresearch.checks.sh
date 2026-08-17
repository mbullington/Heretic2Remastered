#!/bin/bash
# Hard correctness gates after each passing benchmark: pixel equivalence
# (<=0.06% of pixels may differ >2 LSB vs stored baselines, z-fight ties only)
# and benchmark status=PASS.
#
# REFRESH first: run-dos-benchmark.py never rewrites guest RENDER.PPM /
# EFFECTS.PPM (only BENCH-CLIENT.PPM), so gates MUST regenerate them from the
# just-built SILVER.EXE via the synthetic renderer proof, otherwise pixel gates
# silently compare a stale artifact (harness bug found 2026-08-17).
set -euo pipefail
cd "$(dirname "$0")"
rm -rf .dos-build/runs/renderer-*
python3 scripts/run-dos-renderer.py > /tmp/h2-ar-renderer.log 2>&1
python3 scripts/check-dos-bench.py gates
