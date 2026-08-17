#!/bin/bash
# Hard correctness gates after each passing benchmark: pixel equivalence
# (<=0.06% of pixels may differ >2 LSB vs stored baselines, z-fight ties only)
# and benchmark status=PASS.
set -euo pipefail
cd "$(dirname "$0")"
python3 scripts/check-dos-bench.py gates
