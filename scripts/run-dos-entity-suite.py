#!/usr/bin/env python3
"""Run the frozen multi-map DOS entity benchmark suite."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / ".dos-build" / "guest"
RUNS = ROOT / ".dos-build" / "runs"
SCENES = ("sstown", "hive1", "tutorial2")
FRAMES = "160"


def evidence_value(text: str, key: str) -> int:
    match = re.search(rf"^{re.escape(key)}=(\d+)$", text, re.MULTILINE)
    if match is None:
        raise RuntimeError(f"missing {key}")
    return int(match.group(1))


def main() -> int:
    totals = {"entities": 0, "flex": 0, "frame": 0}
    scene_metrics: dict[str, dict[str, int]] = {}
    for scene in SCENES:
        run_id = f"ENTITY-{scene.upper()}"
        shutil.rmtree(RUNS / f"combat-{run_id}", ignore_errors=True)
        env = os.environ.copy()
        env.update(H2DOS_COMBAT_RUN=run_id, H2DOS_COMBAT_MAP=scene, H2DOS_COMBAT_FRAMES=FRAMES)
        result = subprocess.run(
            [sys.executable, "scripts/run-dos-combat.py"], cwd=ROOT, env=env,
            capture_output=True, text=True, check=False,
        )
        if result.returncode != 0 or "status=PASS" not in result.stdout:
            sys.stderr.write(result.stdout[-4000:])
            sys.stderr.write(result.stderr[-4000:])
            return result.returncode or 1
        evidence = (ARTIFACTS / "COMBAT-EVIDENCE.TXT").read_text(encoding="ascii", errors="replace")
        metrics = {
            "entities": evidence_value(evidence, "bench-avg-us-entities"),
            "flex": evidence_value(evidence, "bench-avg-us-ent-flex"),
            "frame": evidence_value(evidence, "bench-avg-us-frame"),
        }
        scene_metrics[scene] = metrics
        for key, value in metrics.items():
            totals[key] += value
        (ARTIFACTS / f"ENTITY-{scene.upper()}-EVIDENCE.TXT").write_text(evidence, encoding="ascii")

    print(f"METRIC entity_suite_us={totals['entities']}")
    print(f"METRIC flex_suite_us={totals['flex']}")
    print(f"METRIC frame_suite_us={totals['frame']}")
    for scene in SCENES:
        for key, value in scene_metrics[scene].items():
            print(f"METRIC {scene}_{key}_us={value}")
    print("status=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
