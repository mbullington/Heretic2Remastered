#!/usr/bin/env python3
"""Run the preserved HELLODOS hardware proof in a fresh allowlisted guest."""

import atexit
import sys
import time

from dos_proof import ProofError, ProofRunError, prepare_guest, run_dosbox

TIMEOUT_SECONDS = 20


def main() -> int:
    run_id = f"RUN{time.time_ns() & 0xFFFFFFFF:08X}"
    try:
        stage = prepare_guest("bootstrap", run_id, "HELLODOS.EXE", fixture=False)
        atexit.register(stage.close)
        log_path = stage.output("HELLODOS.LOG")
        marker_path = stage.output("H2DOS.OK")
        output_path = stage.output("RUN.OUT")
        return_code = run_dosbox(stage, f"hellodos.exe {run_id} > RUN.OUT", TIMEOUT_SECONDS)
    except ProofRunError as error:
        print(error, file=sys.stderr)
        return 1
    except ProofError as error:
        print(error, file=sys.stderr)
        return 2

    if return_code != 0:
        print(f"DOSBox exited with status {return_code}", file=sys.stderr)
        return 1
    if not log_path.is_file() or not marker_path.is_file() or not output_path.is_file():
        print("Guest did not create HELLODOS.LOG, H2DOS.OK, and RUN.OUT", file=sys.stderr)
        if output_path.is_file():
            print(output_path.read_text(encoding="ascii", errors="replace"), file=sys.stderr)
        return 1

    log = log_path.read_text(encoding="ascii")
    marker = marker_path.read_text(encoding="ascii")
    required_log = [
        f"run-id={run_id}", "video-driver=vesa", "audio-driver=soundblaster",
        "audio-queued-ms=250", "status=PASS",
    ]
    missing = [value for value in required_log if value not in log]
    if missing:
        print(f"Guest log failed validation; missing={missing}:\n{log}", file=sys.stderr)
        return 1
    if marker != f"HELLODOS PASS\nrun-id={run_id}\n":
        print(f"Guest marker failed validation:\n{marker}", file=sys.stderr)
        return 1

    try:
        stage.publish(("HELLODOS.LOG", "H2DOS.OK", "RUN.OUT"))
    except ProofError as error:
        print(error, file=sys.stderr)
        return 1
    print(f"Validated guest run {run_id}")
    print(log, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
