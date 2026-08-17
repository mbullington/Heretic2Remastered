#!/usr/bin/env python3
"""Autoresearch verify gate for the DOS software renderer benchmark.

Reads .dos-build/guest/BENCH-EVIDENCE.TXT plus the PPMs produced by
scripts/run-dos-benchmark.py, enforces the hard correctness contracts, and
prints METRIC lines for the autoresearch harness.

Hard gates (exit 1):
  * benchmark status=PASS (heads-up vectors, geometry projection, checksums)
  * RENDER.PPM and EFFECTS.PPM vs stored baselines: >2-LSB pixel fraction
    <= 0.06% (allows documented coincident-depth z-fight tie flips only)
"""
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EVIDENCE = ROOT / ".dos-build" / "guest" / "BENCH-EVIDENCE.TXT"
RUN_DIR = ROOT / ".dos-build" / "guest"
BASELINE_DIR = Path(
    os.environ.get(
        "BB_THREAD_STORAGE", "/Users/mbullington/.bb/thread-storage/thr_z5sba9gfja"
    )
) / "bench"
MAX_BIG_FRACTION = 0.0006  # 0.06% of pixels may differ by >2 LSB (z-fight ties)


def read_evidence() -> dict:
    values = {}
    for line in EVIDENCE.read_text().splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            values[key.strip()] = value.strip()
    return values


def read_ppm(path: Path):
    header = path.read_bytes().split(b"\n", 3)
    assert header[0].strip() == b"P6", path
    width, height = (int(part) for part in header[1].split())
    assert int(header[2]) == 255
    return width, height, header[3]


def diff_stats(base_path: Path, test_path: Path):
    width, height, base = read_ppm(base_path)
    width2, height2, test = read_ppm(test_path)
    assert (width, height) == (width2, height2), (base_path, test_path)
    total = 0
    big = 0
    for i in range(0, len(base), 3):
        db = abs(base[i] - test[i])
        dg = abs(base[i + 1] - test[i + 1])
        dr = abs(base[i + 2] - test[i + 2])
        delta = max(db, dg, dr)
        if delta:
            total += 1
        if delta > 2:
            big += 1
    return width, height, total, big


BENCH_LOG = Path("/tmp/h2-ar-bench.log")


def gates(ev: dict) -> int:
    if not BENCH_LOG.exists() or "status=PASS" not in BENCH_LOG.read_text():
        print("FAIL: benchmark did not report status=PASS", flush=True)
        return 1

    # Staleness guard: benchmark flow never rewrites RENDER/EFFECTS, so the
    # PPMs must be refreshed (renderer proof) after the last SILVER.EXE build.
    silver = RUN_DIR / "SILVER.EXE"
    for name in ("RENDER", "EFFECTS"):
        base = BASELINE_DIR / f"baseline-{name}.PPM"
        test = RUN_DIR / f"{name}.PPM"
        if not base.exists():
            print(f"FAIL: baseline missing: {base}", flush=True)
            return 1
        if not test.exists():
            print(f"FAIL: run output missing: {test}", flush=True)
            return 1
        if silver.exists() and test.stat().st_mtime < silver.stat().st_mtime:
            print(
                f"FAIL: {test.name} is older than SILVER.EXE (stale pixel artifact; "
                "run scripts/run-dos-renderer.py to refresh)",
                flush=True,
            )
            return 1
        width, height, total, big = diff_stats(base, test)
        fraction = big / float(width * height)
        print(
            f"GATE {name}: diff={total} big={big} ({fraction * 100.0:.4f}% >2LSB)",
            flush=True,
        )
        if fraction > MAX_BIG_FRACTION:
            print(f"FAIL: {name} exceeds >2LSB budget", flush=True)
            return 1

    return 0


def metrics(ev: dict) -> int:
    metric_map = {
        "frame_us": "bench-avg-us-frame",
        "world_us": "bench-avg-us-world",
        "sky_us": "bench-avg-us-sky",
        "entities_us": "bench-avg-us-entities",
        "clear_us": "bench-avg-us-clear",
        "present_us": "bench-avg-us-present",
        "particles_us": "bench-avg-us-particles",
        "lights_us": "bench-avg-us-lights",
        "blend_us": "bench-avg-us-blend",
        "flex_prologue_us": "bench-avg-us-flex-prologue",
        "walked_world_px": "bench-walked-world-px",
        "inside_world_px": "bench-inside-world-px",
        "walked_flex_px": "bench-walked-flex-px",
        "inside_flex_px": "bench-inside-flex-px",
        "flex_vertex_lerps": "bench-flex-vertex-lerps",
    }
    if "bench-avg-us-frame" not in ev:
        print("FAIL: bench-avg-us-frame missing", flush=True)
        return 1
    for name, key in metric_map.items():
        if key in ev:
            print(f"METRIC {name}={int(ev[key])}", flush=True)
    for key in ("bench-world-walk-waste-pct", "bench-flex-walk-waste-pct"):
        if key in ev:
            print(f"METRIC {key.replace('-', '_')}={float(ev[key])}", flush=True)
    print("status=PASS", flush=True)
    return 0


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in ("gates", "metrics"):
        print("usage: check-dos-bench.py gates|metrics", flush=True)
        return 2
    if not EVIDENCE.exists():
        print("FAIL: BENCH-EVIDENCE.TXT missing", flush=True)
        return 1
    ev = read_evidence()
    if sys.argv[1] == "gates":
        return gates(ev)
    return metrics(ev)


if __name__ == "__main__":
    sys.exit(main())
