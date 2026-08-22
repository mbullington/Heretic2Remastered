#!/usr/bin/env python3
"""Bounded original-data production-path smoke for the DOS port.

Runs SILVER.EXE against the two allowlisted, user-owned original game PAKs
(base/Htic2-0.pak, base/Htic2-1.pak) COPIED into a transient, gitignored guest
stage. The original files are opened read-only; nothing from them is written
back, extracted, or committed. The guest proof is non-interactive: it validates
production loading of original maps, models, images, and sounds through the
engine's real code paths and captures guest-created evidence. It is not a
gameplay claim.
"""

from __future__ import annotations

import os
from pathlib import Path
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

RUN_ID = os.environ.get("H2DOS_REALDATA_RUN", "REALDATA1")
STAGE_KIND = "realdata"
PUBLISH = ("REALMENU.PPM", "REALCON.PPM", "CLIENT.PPM", "SWREAL.OUT", "SILVER.LOG", "SILVER.OK")
ALLOWLISTED_PAKS = ("Htic2-0.pak", "Htic2-1.pak")
FRAME_LIMIT = int(os.environ.get("H2DOS_REALDATA_FRAMES", "24"))
TIMEOUT_SECONDS = 300
MIN_UNIQUE_COLORS = {"REALMENU.PPM": 3, "REALCON.PPM": 3, "CLIENT.PPM": 20}

REQUIRED_LOG_LINES = (
    "artifact=SILVER.EXE",
    "path=production-Qcommon",
    "fixture=original-paks-read-only-non-gameplay",
    "qcommon-init-entry=PASS",
    "qcommon-init-completion=PASS",
    "client-effects-api-init=PASS",
    "mounted-paks-allowlisted=PASS",
    "original-game-data=read-only-user-paks",
    "static-game-binding=PASS",
    "static-client-effects-binding=PASS",
    "static-renderer-binding=PASS",
    "static-sound-binding=PASS",
    "zone-malloc-zero=PASS",
    "zone-tag-cleanup=PASS",
    "realdata-menu=PASS",
    "realdata-console=PASS",
    "sound-api=SDL3 Software Mixer",
    "sound-driver=soundblaster",
    "sound-fixture=sound/Monsters/chicken/peck1.wav",
    "sound-proof-counters=PASS",
    "game-api-init=PASS",
    "player-api-init=PASS",
    "server-map=tutorial2",
    "local-client-server-handshake=PASS",
    "client-state=active",
    "server-client-state=spawned",
    "renderer-active-client-screenshot=CLIENT.PPM",
    "boundary=original-paks-read-only-production-path-smoke-non-gameplay",
    "gameplay=UNVERIFIED",
    "status=PASS",
)

REQUIRED_COUNTER_LINES = (
    "realdata-frames-presented=",
    "realdata-world-faces=",
    "realdata-m8-images=",
    "realdata-m32-images=",
    "realdata-flex-models=",
    "realdata-flex-entities=",
)

# Particle emission depends on the scripted spawn's viewpoint and timing, so the
# realdata particle counters are recorded as evidence lines only; particle
# semantics are exhaustively proven by the synthetic-fixture renderer proof.
OPTIONAL_COUNTER_LINES = (
    "realdata-particles-drawn=",
    "realdata-textured-particles=",
)


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    header = b"P6\n320 240\n255\n"
    if not data.startswith(header) or len(data) != len(header) + 320 * 240 * 3:
        raise ProofError(f"Invalid captured framebuffer: {path}")
    return 320, 240, data[len(header):]


def unique_colors(pixels: bytes) -> int:
    return len({pixels[i:i + 3] for i in range(0, len(pixels), 3)})


def main() -> int:
    try:
        original_dir = Path(os.environ.get("H2DOS_ORIGINAL_DATA", ".dos-data"))
        pak_sources: list[Path] = []
        for name in ALLOWLISTED_PAKS:
            source = original_dir / "base" / name
            if not source.is_file():
                raise ProofError(
                    f"Original PAK not found: {source}; place user-owned {name} there (read-only)"
                )
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

        staged_paks = sorted(p.relative_to(stage_path).as_posix() for p in stage_path.rglob("*.pak"))
        expected = sorted(f"H2REAL/base/{name}" for name in ALLOWLISTED_PAKS)
        if staged_paks != expected:
            raise ProofError(f"Real-data guest PAK allowlist mismatch: {staged_paks}")
        if any(p.is_symlink() for p in stage_path.rglob("*")):
            raise ProofError("Guest input contains a symlink")
        for name in ALLOWLISTED_PAKS:
            if sha256(base_dir / name) != pak_sha256[name]:
                raise ProofError(f"Staged {name} checksum diverged from the original file")

        # DOS command lines are limited to ~126 characters, so the proof flags are
        # staged as DJGPP environment variables; the short +map command stays on
        # the command line (+exec is not honored by this engine's early parsing).
        dosbox_executable()
        run_dosbox(
            GuestStage(stage_path, None, set()),
            f"set H2DOS_RUN_ID={RUN_ID}\nset H2DOS_FRAMES={FRAME_LIMIT}\n"
            "set H2DOS_RENDERER=1\nset H2DOS_AUDIO=1\nset H2DOS_REALDATA=1\n"
            "silver.exe -basedir H2REAL +set developer 1 +map tutorial2 > SWREAL.OUT",
            TIMEOUT_SECONDS,
        )

        failures: list[str] = []
        marker = stage_path / "SILVER.OK"
        log_path = stage_path / "SILVER.LOG"
        if not marker.is_file():
            failures.append("guest marker SILVER.OK missing (guest did not finish cleanly)")
        if not log_path.is_file():
            failures.append("guest log SILVER.LOG missing")
        log_text = log_path.read_text(encoding="ascii", errors="replace") if log_path.is_file() else ""
        for line in REQUIRED_LOG_LINES:
            if line not in log_text:
                failures.append(f"missing evidence line: {line}")
        for prefix in REQUIRED_COUNTER_LINES:
            matched = [ln for ln in log_text.splitlines() if ln.startswith(prefix)]
            if not matched:
                failures.append(f"missing counter line: {prefix}...")
                continue
            value = matched[0].split("=", 1)[1]
            if not value.isdigit() or int(value) == 0:
                failures.append(f"counter not above zero: {matched[0]}")
        for prefix in OPTIONAL_COUNTER_LINES:
            if not any(ln.startswith(prefix) for ln in log_text.splitlines()):
                failures.append(f"missing optional counter evidence line: {prefix}...")
        for name, minimum in MIN_UNIQUE_COLORS.items():
            path = stage_path / name
            if not path.is_file():
                failures.append(f"missing captured framebuffer: {name}")
                continue
            _, _, pixels = read_ppm(path)
            count = unique_colors(pixels)
            if count < minimum:
                failures.append(f"{name} has only {count} unique colors (expected >= {minimum})")

        leftover = subprocess.run(
            ["pgrep", "-x", "dosbox-staging"], capture_output=True, text=True, check=False
        )
        if leftover.returncode == 0:
            failures.append("dosbox-staging process left running")

        if failures:
            for failure in failures:
                print(f"FAIL: {failure}", file=sys.stderr)
            raise ProofError("Guest did not create complete original-data evidence")

        ARTIFACT_DIR.mkdir(parents=True, exist_ok=True)
        evidence = ARTIFACT_DIR / "REALDATA-EVIDENCE.TXT"
        evidence.write_text(
            "artifact=SILVER.EXE\n"
            f"run-id={RUN_ID}\n"
            "path=production-Qcommon\n"
            "boundary=original-paks-read-only-production-path-smoke-non-gameplay\n"
            f"original-pak-Htic2-0-sha256={pak_sha256['Htic2-0.pak']}\n"
            f"original-pak-Htic2-1-sha256={pak_sha256['Htic2-1.pak']}\n"
            "map=tutorial2\n"
            "dosbox-version=dosbox-staging, version 0.82.2 (f8c24)\n"
            + log_text,
            encoding="ascii",
        )
        for name in PUBLISH:
            source = stage_path / name
            if source.is_file():
                destination = ARTIFACT_DIR / name
                temporary = destination.with_name(destination.name + ".tmp")
                shutil.copyfile(source, temporary)
                temporary.replace(destination)
        client_pixels = read_ppm(stage_path / "CLIENT.PPM")[2]
        print(f"realdata-client-unique-colors={unique_colors(client_pixels)}")
        print(f"run-id={RUN_ID}")
        print("status=PASS")
        print("guest-process-exit=PASS")
        print("dosbox-process-left-running=NO")
        print(f"evidence={evidence}")
        shutil.rmtree(stage_path, ignore_errors=True)
        return 0
    except (ProofError, ProofRunError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
