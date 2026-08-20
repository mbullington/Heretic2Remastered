#!/bin/bash
# Temporal-reprojection quality ruler: matched full-render and temporal runs.
set -euo pipefail
cd "$(dirname "$0")"

NIXPKGS="github:NixOS/nixpkgs/f2b17eb1b90f6a359082035b4538d4c63017aec3"
JOBS="${CMAKE_BUILD_PARALLEL_LEVEL:-8}"
APP_BUILD=".dos-build/heretic2-dos"
TIMEOUT_CMD="$(command -v gtimeout || command -v timeout || true)"
run_boxed() { if [ -n "$TIMEOUT_CMD" ]; then "$TIMEOUT_CMD" --kill-after=30s 720 "$@"; else "$@"; fi; }
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
run_boxed bash -c build_dos || {
  echo "build attempt 1 failed/hung, retrying" >&2
  pkill -f i586-pc-msdosdjgpp 2>/dev/null || true
  sleep 2
  run_boxed bash -c build_dos
} || {
  tail -25 /tmp/h2-ar-build.log
  exit 1
}

python3 scripts/run-dos-temporal-quality.py
