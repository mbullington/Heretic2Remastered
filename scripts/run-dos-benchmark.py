#!/usr/bin/env python3
"""Bounded wall-clock benchmark of the DOS software renderer against original PAKs.

Same allowlisted read-only staging as run-dos-realdata.py, but with
H2DOS_BENCH=1 and pinned DOSBox cycles (fixed 120000, roughly Pentium-class)
so frame-time comparisons across renderer revisions are meaningful. Emits
BENCH-EVIDENCE.TXT and keeps the guest-produced CLIENT.PPM as
BENCH-CLIENT.PPM for pixel-equivalence diffing across optimizations.
"""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dos_proof import (  # noqa: E402
    ARTIFACT_DIR,
    GuestStage,
    ProofError,
    ProofRunError,
    RUNS_DIR,
    dosbox_executable,
    run_dosbox,
    sha256,
)

RUN_ID = os.environ.get("H2DOS_BENCH_RUN", "BENCH1")
STAGE_KIND = "bench"
ALLOWLISTED_PAKS = ("Htic2-0.pak", "Htic2-1.pak")
FRAME_LIMIT = 48
TIMEOUT_SECONDS = 900
CYCLES = "fixed 120000"

REQUIRED_LOG_LINES = (
    "qcommon-init-completion=PASS",
    "mounted-paks-allowlisted=PASS",
    "local-client-server-handshake=PASS",
    "client-state=active",
    "gameplay=UNVERIFIED",
    "status=PASS",
)

REQUIRED_BENCH_LINES = (
    "bench-frames=",
    "bench-us-frame=",
    "bench-avg-us-frame=",
    "bench-avg-us-world=",
    "bench-avg-us-entities=",
    "bench-avg-us-particles=",
    "bench-avg-us-present=",
    "bench-walked-world-px=",
    "bench-inside-world-px=",
    "bench-walked-flex-px=",
    "bench-inside-flex-px=",
    "bench-world-walk-waste-pct=",
    "bench-flex-walk-waste-pct=",
)


def bench_average(log_text: str, key: str) -> int:
    match = re.search(rf"^{re.escape(key)}=(\d+)$", log_text, re.MULTILINE)
    return int(match.group(1)) if match else -1


def main() -> int:
    try:
        original_dir = Path(os.environ.get("H2DOS_ORIGINAL_DATA", ".dos-data"))
        pak_sources: list[Path] = []
        for name in ALLOWLISTED_PAKS:
            source = original_dir / "base" / name
            if not source.is_file():
                raise ProofError(f"Original PAK not found: {source}")
            pak_sources.append(source)
        pak_sha256 = {name: sha256(source) for name, source in zip(ALLOWLISTED_PAKS, pak_sources)}

        executable = ARTIFACT_DIR / "SILVER.EXE"
        cwsdpmi = ARTIFACT_DIR / "CWSDPMI.EXE"
        missing = [str(path) for path in (executable, cwsdpmi) if not path.is_file()]
        if missing:
            raise ProofError(f"Missing build inputs {missing}; run scripts/build-dos-bootstrap.sh")

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
        for name in ALLOWLISTED_PAKS:
            if sha256(base_dir / name) != pak_sha256[name]:
                raise ProofError(f"Staged {name} checksum diverged from the original file")
        if any(p.is_symlink() for p in stage_path.rglob("*")):
            raise ProofError("Guest input contains a symlink")

        dosbox_executable()
        run_dosbox(
            GuestStage(stage_path, None, set()),
            f"set H2DOS_RUN_ID={RUN_ID}\nset H2DOS_FRAMES={FRAME_LIMIT}\n"
            "set H2DOS_RENDERER=1\nset H2DOS_AUDIO=1\nset H2DOS_REALDATA=1\nset H2DOS_BENCH=1\n"
            "silver.exe -basedir H2REAL +set developer 1 +map tutorial2 > SWREAL.OUT",
            TIMEOUT_SECONDS,
            extra_sets=(f"cycles={CYCLES}",),
        )

        log_path = stage_path / "SILVER.LOG"
        if not (stage_path / "SILVER.OK").is_file():
            raise ProofError("guest marker SILVER.OK missing (guest did not finish cleanly)")
        if not log_path.is_file():
            raise ProofError("guest log SILVER.LOG missing")
        log_text = log_path.read_text(encoding="ascii", errors="replace")
        failures: list[str] = []
        for line in REQUIRED_LOG_LINES:
            if line not in log_text:
                failures.append(f"missing evidence line: {line}")
        for prefix in REQUIRED_BENCH_LINES:
            matched = [ln for ln in log_text.splitlines() if ln.startswith(prefix)]
            if not matched:
                failures.append(f"missing bench line: {prefix}...")

        leftover = subprocess.run(
            ["pgrep", "-f", "dosbox-staging"], capture_output=True, text=True, check=False
        )
        if leftover.returncode == 0:
            failures.append("dosbox-staging process left running")
        if failures:
            for failure in failures:
                print(f"FAIL: {failure}", file=sys.stderr)
            raise ProofError("benchmark run did not produce complete evidence")

        bench_lines = [ln for ln in log_text.splitlines() if ln.startswith("bench-")]
        evidence = ARTIFACT_DIR / "BENCH-EVIDENCE.TXT"
        evidence.write_text(
            "artifact=SILVER.EXE\n"
            f"run-id={RUN_ID}\n"
            f"cycles={CYCLES}\n"
            f"frames={FRAME_LIMIT}\n"
            f"original-pak-Htic2-0-sha256={pak_sha256['Htic2-0.pak']}\n"
            f"original-pak-Htic2-1-sha256={pak_sha256['Htic2-1.pak']}\n"
            + "\n".join(bench_lines)
            + "\n",
            encoding="ascii",
        )
        if (stage_path / "CLIENT.PPM").is_file():
            shutil.copyfile(stage_path / "CLIENT.PPM", ARTIFACT_DIR / "BENCH-CLIENT.PPM")
        if (stage_path / "SWREAL.OUT").is_file():
            shutil.copyfile(stage_path / "SWREAL.OUT", ARTIFACT_DIR / "BENCH-SWREAL.OUT")

        print("bench-lines=")
        for line in bench_lines:
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
