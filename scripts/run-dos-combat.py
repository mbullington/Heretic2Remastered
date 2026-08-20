#!/usr/bin/env python3
"""Combat-scene benchmark (T3 holdout): deterministic monster load.

Same allowlisted read-only staging as run-dos-benchmark.py, but loads a
combat-populated map and lets the REAL game run (monsters engage the
stationary client) for a fixed frame count. Two roles:

  1. evidence that lights/blend/sprites/particles are exercised (the tutorial
     bench leaves those pools quiescent)
  2. deterministic ROBUSTNESS reference: counter-family metrics
     (walked px, spans, tris, faces, lerps) are timing-independent and must be
     bit-identical across runs of the same build; timing cycles are telemetry.

Usage: run twice (or N times) and compare counters; pm_mca harness compares
COMBAT-EVIDENCE counters vs the checked-in combat baseline.
"""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dos_proof import (
    ARTIFACT_DIR,
    GuestStage,
    ProofError,
    ProofRunError,
    RUNS_DIR,
    dosbox_executable,
    run_dosbox,
    sha256,
)

RUN_ID = os.environ.get("H2DOS_COMBAT_RUN", "COMBAT1")
STAGE_KIND = "combat"
ALLOWLISTED_PAKS = ("Htic2-0.pak", "Htic2-1.pak")
COMBAT_MAP = os.environ.get("H2DOS_COMBAT_MAP", "tutorial2")
FRAME_LIMIT = int(os.environ.get("H2DOS_COMBAT_FRAMES", "160"))
TIMEOUT_SECONDS = 900
CYCLES = "fixed 120000"

REQUIRED_LOG_LINES = (
    "qcommon-init-completion=PASS",
    "client-state=active",
    "status=PASS",
)

# deterministic counter families (timing-independent) + pool-activity metrics
COUNTER_PREFIXES = (
    "bench-frames=",
    "bench-walked-world-px=",
    "bench-inside-world-px=",
    "bench-walked-flex-px=",
    "bench-inside-flex-px=",
    "bench-world-faces=",
    "bench-world-spans=",
    "bench-flex-tris=",
    "bench-flex-vertex-lerps=",
    "bench-sky-pixels=",
)
POOL_KEYS = (
    "bench-avg-us-particles=",
    "bench-avg-us-lights=",
    "bench-avg-us-blend=",
    "bench-avg-us-entities=",
)


def main() -> int:
    try:
        original_dir = Path(os.environ.get("H2DOS_ORIGINAL_DATA", ".dos-data"))
        pak_sources: list[Path] = []
        for name in ALLOWLISTED_PAKS:
            source = original_dir / "base" / name
            if not source.is_file():
                raise ProofError(f"Original PAK not found: {source}")
            pak_sources.append(source)

        executable = ARTIFACT_DIR / "SILVER.EXE"
        cwsdpmi = ARTIFACT_DIR / "CWSDPMI.EXE"
        missing = [str(p) for p in (executable, cwsdpmi) if not p.is_file()]
        if missing:
            raise ProofError(f"Missing build inputs {missing}")

        RUNS_DIR.mkdir(parents=True, exist_ok=True)
        stage_path = RUNS_DIR / f"{STAGE_KIND}-{RUN_ID}"
        if stage_path.exists():
            raise ProofError(f"Refusing to reuse guest stage: {stage_path}")
        stage_path.mkdir()
        shutil.copyfile(executable, stage_path / "SILVER.EXE")
        shutil.copyfile(cwsdpmi, stage_path / "CWSDPMI.EXE")
        base_dir = stage_path / "H2REAL" / "base"
        base_dir.mkdir(parents=True)
        for name, source in zip(ALLOWLISTED_PAKS, pak_sources):
            shutil.copyfile(source, base_dir / name)

        dosbox_executable()
        # H2DOS_COMBAT arms the deterministic driver in dos/h2dos_main.c:
        # 8 frames after the bench seed, it issues `angermonsters`(production
        # cheat: every SVF_MONSTER edict acquires the player) + `god` (window
        # survives). SWEEP=1 orbits the camera at a fixed angle per frame so
        # span coverage sweeps a deterministic sequence.
        extra_blocks = ""
        extra_env = os.environ.get("H2DOS_BENCH_EXTRA_SET", "").strip()
        world_half_mode = "H2DOS_SW_WORLD_HALF=1" in extra_env.split()
        if extra_env:
            for item in extra_env.split():
                if "=" in item and all(c.isalnum() or c in "_=.,-" for c in item):
                    extra_blocks += f"set {item}\n"
        run_dosbox(
            GuestStage(stage_path, None, set()),
            f"set H2DOS_RUN_ID={RUN_ID}\nset H2DOS_FRAMES={FRAME_LIMIT}\n"
            "set H2DOS_RENDERER=1\nset H2DOS_AUDIO=1\nset H2DOS_REALDATA=1\n"
            "set H2DOS_BENCH=1\nset H2DOS_SWEEP=1\nset H2DOS_COMBAT=1\nset H2DOS_SW_FXLOAD=1\n"
            f"{extra_blocks}"
            "set H2DOS_SW_WIDTH=320\nset H2DOS_SW_HEIGHT=240\n"
            f"silver.exe -basedir H2REAL +set developer 1 +map {COMBAT_MAP} > SWREAL.OUT",
            TIMEOUT_SECONDS,
            extra_sets=(f"cycles={CYCLES}",),
        )

        log_path = stage_path / "SILVER.LOG"
        if not (stage_path / "SILVER.OK").is_file():
            raise ProofError("guest marker SILVER.OK missing (guest did not finish cleanly)")
        log_text = log_path.read_text(encoding="ascii", errors="replace")
        failures: list[str] = []
        for line in REQUIRED_LOG_LINES:
            if line not in log_text:
                failures.append(f"missing evidence line: {line}")
        leftover = subprocess.run(
            ["pgrep", "-f", "dosbox-staging"], capture_output=True, text=True, check=False
        )
        if leftover.returncode == 0:
            failures.append("dosbox-staging process left running")

        bench_lines = [ln for ln in log_text.splitlines() if ln.startswith("bench-")]
        counters = {}
        for line in bench_lines:
            k, _, v = line.partition("=")
            counters[k] = v

        # pool-activity assertions: the whole point of the scene
        def num(key: str) -> int:
            m = re.match(r"^(\d+)", counters.get(key.rstrip("="), "0"))
            return int(m.group(1)) if m else -1

        pools = {k.rstrip("=").replace("bench-avg-us-", ""): num(k)
                 for k in POOL_KEYS}
        active = [k for k, v in pools.items() if v > 0]
        if pools.get("entities", 0) < 5000 or len(active) < 3:
            failures.append(
                f"combat pools too quiet on map {COMBAT_MAP}: {pools} "
                "(need entities>=5ms/frame and >=3 of the pools > 0)")

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}", file=sys.stderr)
            raise ProofError("combat scene did not produce usable evidence")

        evidence = ARTIFACT_DIR / "COMBAT-EVIDENCE.TXT"
        evidence.write_text(
            f"combat-map={COMBAT_MAP}\nframes={FRAME_LIMIT}\ncycles={CYCLES}\nsweep=1\n"
            f"world-half={int(world_half_mode)}\n" + "\n".join(bench_lines) + "\n", encoding="ascii")
        if (stage_path / "CLIENT.PPM").is_file():
            shutil.copyfile(stage_path / "CLIENT.PPM", ARTIFACT_DIR / "COMBAT-CLIENT.PPM")
        if (stage_path / "SWREAL.OUT").is_file():
            shutil.copyfile(stage_path / "SWREAL.OUT", ARTIFACT_DIR / "COMBAT-SWREAL.OUT")

        print(f"map={COMBAT_MAP} frames={FRAME_LIMIT}")
        print(f"pools-active={'+'.join(active)} {pools}")
        for line in bench_lines:
            if any(line.startswith(p) for p in COUNTER_PREFIXES + POOL_KEYS):
                print(f"  {line}")
        print(f"evidence={evidence}")
        print("status=PASS")
        shutil.rmtree(stage_path, ignore_errors=True)
        return 0
    except (ProofError, ProofRunError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
