#!/usr/bin/env python3
"""Fresh allowlisted guest staging and bounded DOSBox execution for DOS proofs."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
BUILD_ROOT = ROOT / ".dos-build"
ARTIFACT_DIR = BUILD_ROOT / "guest"
RUNS_DIR = BUILD_ROOT / "runs"
FIXTURE_SOURCE = ARTIFACT_DIR / "H2TEST" / "base" / "Htic2-9.pak"
FIXTURE_HASH_FILE = ROOT / "scripts" / "dos-fixture.sha256"
ACCEPTED_DOSBOX_VERSION = "dosbox-staging, version 0.82.2 (f8c24)"
DOSBOX_MEMORY_MB = 64


class ProofError(RuntimeError):
    pass


class ProofRunError(ProofError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def expected_fixture_sha256() -> str:
    fields = FIXTURE_HASH_FILE.read_text(encoding="ascii").split()
    if len(fields) != 2 or len(fields[0]) != 64 or fields[1] != "Htic2-9.pak":
        raise ProofError(f"Invalid fixture hash file: {FIXTURE_HASH_FILE}")
    return fields[0]


@dataclass
class GuestStage:
    path: Path
    fixture: Path | None
    _inputs: set[Path]

    def output(self, name: str) -> Path:
        return self.path / name

    def validate_initial_inventory(self) -> None:
        actual = {
            path.relative_to(self.path)
            for path in self.path.rglob("*")
            if path.is_file()
        }
        if actual != self._inputs:
            unexpected = sorted(str(path) for path in actual - self._inputs)
            missing = sorted(str(path) for path in self._inputs - actual)
            raise ProofError(f"Guest input allowlist mismatch; unexpected={unexpected}, missing={missing}")
        if any(path.is_symlink() for path in self.path.rglob("*")):
            raise ProofError("Guest input contains a symlink")
        paks = sorted(path.relative_to(self.path) for path in self.path.rglob("*.pak"))
        expected_paks = [Path("H2TEST/base/Htic2-9.pak")] if self.fixture is not None else []
        if paks != expected_paks:
            raise ProofError(f"Guest PAK allowlist mismatch: {paks}")

    def publish(self, names: tuple[str, ...]) -> None:
        ARTIFACT_DIR.mkdir(parents=True, exist_ok=True)
        for name in names:
            source = self.output(name)
            if not source.is_file():
                raise ProofError(f"Cannot publish missing guest output: {source}")
            destination = ARTIFACT_DIR / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary = destination.with_name(destination.name + ".tmp")
            shutil.copyfile(source, temporary)
            temporary.replace(destination)

    def close(self) -> None:
        shutil.rmtree(self.path, ignore_errors=True)

    def __enter__(self) -> GuestStage:
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.close()


def prepare_guest(kind: str, run_id: str, executable_name: str, *, fixture: bool) -> GuestStage:
    executable = ARTIFACT_DIR / executable_name
    cwsdpmi = ARTIFACT_DIR / "CWSDPMI.EXE"
    required = (executable, cwsdpmi)
    missing = [str(path) for path in required if not path.is_file()]
    if fixture and not FIXTURE_SOURCE.is_file():
        missing.append(str(FIXTURE_SOURCE))
    if missing:
        raise ProofError(f"Missing build inputs {missing}; run scripts/build-dos-bootstrap.sh")

    if fixture:
        expected = expected_fixture_sha256()
        actual = sha256(FIXTURE_SOURCE)
        if actual != expected:
            raise ProofError(f"Synthetic fixture checksum mismatch: expected {expected}, got {actual}")

    RUNS_DIR.mkdir(parents=True, exist_ok=True)
    stage_path = RUNS_DIR / f"{kind}-{run_id}"
    if stage_path.exists():
        raise ProofError(f"Refusing to reuse guest stage: {stage_path}")
    stage_path.mkdir()

    inputs = {Path(executable_name), Path("CWSDPMI.EXE")}
    shutil.copyfile(executable, stage_path / executable_name)
    shutil.copyfile(cwsdpmi, stage_path / "CWSDPMI.EXE")
    fixture_path: Path | None = None
    if fixture:
        relative_fixture = Path("H2TEST/base/Htic2-9.pak")
        fixture_path = stage_path / relative_fixture
        fixture_path.parent.mkdir(parents=True)
        shutil.copyfile(FIXTURE_SOURCE, fixture_path)
        inputs.add(relative_fixture)

    stage = GuestStage(stage_path, fixture_path, inputs)
    stage.validate_initial_inventory()
    return stage


def dosbox_executable() -> str:
    requested = os.environ.get("DOSBOX", "dosbox-staging")
    executable = shutil.which(requested)
    if executable is None:
        raise ProofError(f"DOSBox executable not found: {requested}")
    result = subprocess.run(
        [executable, "--version"], check=False, capture_output=True, text=True, timeout=5
    )
    first_line = result.stdout.splitlines()[0] if result.stdout.splitlines() else ""
    if result.returncode != 0 or first_line != ACCEPTED_DOSBOX_VERSION:
        raise ProofError(
            f"Unsupported DOSBox version {first_line!r}; accepted version is {ACCEPTED_DOSBOX_VERSION!r}"
        )
    return executable


def run_dosbox(
    stage: GuestStage,
    guest_command: str,
    timeout_seconds: int,
    extra_sets: tuple[str, ...] = (),
) -> int:
    executable = dosbox_executable()
    command = [
        executable,
        "--noprimaryconf",
        "--nolocalconf",
        "--noautoexec",
        "--set",
        f"memsize={DOSBOX_MEMORY_MB}",
        "--set",
        # Target machine is a Pentium MMX 233: expose MMX to the guest
        # (DOSBox Staging >= 0.82 has real pentium_mmx emulation).
        "cputype=pentium_mmx",
    ]
    for setting in extra_sets:
        command += ["--set", setting]
    command += [
        "--exit",
        "-c",
        f'mount c "{stage.path}"',
        "-c",
        "c:",
    ]
    for line in guest_command.splitlines():
        if line.strip():
            command += ["-c", line]
    command += ["-c", "exit"]
    print("Running:", " ".join(command), flush=True)
    process = subprocess.Popen(command)
    try:
        return process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired as error:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
        raise ProofRunError(f"DOSBox exceeded {timeout_seconds}s and was stopped") from error
