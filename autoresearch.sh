#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

NIXPKGS="github:NixOS/nixpkgs/f2b17eb1b90f6a359082035b4538d4c63017aec3"
APP_BUILD=".dos-build/heretic2-dos"
JOBS="${CMAKE_BUILD_PARALLEL_LEVEL:-8}"
TIMEOUT_CMD="$(command -v gtimeout || command -v timeout || true)"

run_boxed() {
  if [ -n "$TIMEOUT_CMD" ]; then
    "$TIMEOUT_CMD" --kill-after=30s 720 "$@"
  else
    "$@"
  fi
}

build_dos() {
  if [ ! -f "$APP_BUILD/build.ninja" ]; then
    CMAKE_BUILD_PARALLEL_LEVEL="$JOBS" bash scripts/build-dos-bootstrap.sh > /tmp/h2-entity-build.log 2>&1
  else
    nix shell "$NIXPKGS#djgpp_i586" "$NIXPKGS#cmake" "$NIXPKGS#ninja" -c \
      cmake --build "$APP_BUILD" --parallel "$JOBS" --target SILVER \
      > /tmp/h2-entity-build.log 2>&1
  fi
}
export NIXPKGS APP_BUILD JOBS
export -f build_dos

run_boxed bash -c build_dos || {
  pkill -f i586-pc-msdosdjgpp 2>/dev/null || true
  sleep 2
  run_boxed bash -c build_dos
}

python3 scripts/run-dos-entity-suite.py
