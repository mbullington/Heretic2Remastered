#!/usr/bin/env python3
"""Compare temporal reprojection with matched full-render gameplay frames."""

from pathlib import Path
import os
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
GUEST = ROOT / ".dos-build" / "guest"
CAPTURE_ROOT = ROOT / ".dos-build" / "temporal-quality"
STEP_DEGREES = "1.0"
EXPECTED_WARPS = 13
BAD_CHANNEL_DELTA = 8


def parse_evidence(path: Path) -> dict[str, str]:
    values = {}
    for line in path.read_text(encoding="ascii", errors="replace").splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            values[key.strip()] = value.strip()
    return values


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    parts = path.read_bytes().split(b"\n", 3)
    if len(parts) != 4 or parts[0].strip() != b"P6" or int(parts[2]) != 255:
        raise RuntimeError(f"unsupported PPM: {path}")
    width, height = (int(value) for value in parts[1].split())
    if len(parts[3]) != width * height * 3:
        raise RuntimeError(f"truncated PPM: {path}")
    return width, height, parts[3]


def run_capture(name: str, temporal: bool) -> tuple[Path, dict[str, str]]:
    output = CAPTURE_ROOT / name
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)
    for old in GUEST.glob("BENCH-F*.PPM"):
        old.unlink()
    env = os.environ.copy()
    env["H2DOS_BENCH_RUN"] = f"TEMP-{name.upper()}"
    env["H2DOS_BENCH_EXTRA_SET"] = " ".join((
        f"H2DOS_SW_ROTATE_DEGREES={STEP_DEGREES}",
        "H2DOS_DUMPALL=1",
        f"H2DOS_SW_TEMPORAL={1 if temporal else 0}",
    ))
    result = subprocess.run(
        [sys.executable, "scripts/run-dos-benchmark.py"], cwd=ROOT, env=env,
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
    )
    Path("/tmp/h2-ar-bench.log").write_text(result.stdout, encoding="utf-8")
    if result.returncode != 0:
        print(result.stdout[-4000:], file=sys.stderr)
        raise RuntimeError(f"{name} DOS capture failed")
    frames = sorted(GUEST.glob("BENCH-F*.PPM"))
    if len(frames) != 32:
        raise RuntimeError(f"{name} produced {len(frames)} captures, expected 32")
    for frame in frames:
        shutil.copyfile(frame, output / frame.name)
    evidence = GUEST / "BENCH-EVIDENCE.TXT"
    shutil.copyfile(evidence, output / evidence.name)
    return output, parse_evidence(evidence)


def compare(reference: Path, candidate: Path) -> dict[str, float]:
    refs = {path.name: path for path in reference.glob("BENCH-F*.PPM")}
    tests = {path.name: path for path in candidate.glob("BENCH-F*.PPM")}
    if refs.keys() != tests.keys():
        raise RuntimeError("reference and candidate frame sets differ")
    total_pixels = bad_pixels = changed_pixels = absolute_error = 0
    max_frame_bad_pct = 0.0
    histogram = [0] * 256
    for name in sorted(refs):
        width, height, base = read_ppm(refs[name])
        width2, height2, test = read_ppm(tests[name])
        if (width, height) != (width2, height2):
            raise RuntimeError(f"capture dimensions differ for {name}")
        frame_bad = 0
        for offset in range(0, len(base), 3):
            deltas = tuple(abs(base[offset + channel] - test[offset + channel]) for channel in range(3))
            delta = max(deltas)
            histogram[delta] += 1
            absolute_error += sum(deltas)
            changed_pixels += delta != 0
            if delta > BAD_CHANNEL_DELTA:
                bad_pixels += 1
                frame_bad += 1
        pixels = width * height
        total_pixels += pixels
        max_frame_bad_pct = max(max_frame_bad_pct, frame_bad * 100.0 / pixels)
    rank = (total_pixels * 95 + 99) // 100
    cumulative = p95 = 0
    for delta, count in enumerate(histogram):
        cumulative += count
        if cumulative >= rank:
            p95 = delta
            break
    return {
        "temporal_bad_pct": bad_pixels * 100.0 / total_pixels,
        "temporal_changed_pct": changed_pixels * 100.0 / total_pixels,
        "temporal_mae": absolute_error / (total_pixels * 3.0),
        "temporal_p95": float(p95),
        "temporal_max_frame_bad_pct": max_frame_bad_pct,
        "capture_frames": float(len(refs)),
    }


def integer_metric(evidence: dict[str, str], key: str) -> int:
    match = re.match(r"^(\d+)", evidence.get(key, ""))
    if match is None:
        raise RuntimeError(f"missing numeric evidence: {key}")
    return int(match.group(1))


def main() -> int:
    try:
        shutil.rmtree(CAPTURE_ROOT, ignore_errors=True)
        CAPTURE_ROOT.mkdir(parents=True)
        reference_path, reference = run_capture("reference", False)
        candidate_path, candidate = run_capture("candidate", True)
        metrics = compare(reference_path, candidate_path)
        warps = integer_metric(candidate, "bench-temporal-warps")
        reference_frame_us = integer_metric(reference, "bench-avg-us-frame")
        candidate_frame_us = integer_metric(candidate, "bench-avg-us-frame")
        reference_world_us = integer_metric(reference, "bench-avg-us-world")
        candidate_world_us = integer_metric(candidate, "bench-avg-us-world")
        if warps != EXPECTED_WARPS:
            raise RuntimeError(f"temporal cadence changed: {warps} warps, expected {EXPECTED_WARPS}")
        if candidate_frame_us > reference_frame_us * 1.25:
            raise RuntimeError(
                f"temporal path exceeds 25% frame-cost guardrail: {candidate_frame_us} vs {reference_frame_us} us"
            )
        for name, value in metrics.items():
            print(f"METRIC {name}={value:.6f}")
        print(f"METRIC temporal_warps={warps}")
        print(f"METRIC reference_frame_us={reference_frame_us}")
        print(f"METRIC candidate_frame_us={candidate_frame_us}")
        print(f"METRIC reference_world_us={reference_world_us}")
        print(f"METRIC candidate_world_us={candidate_world_us}")
        print("status=PASS")
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
