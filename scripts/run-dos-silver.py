#!/usr/bin/env python3
"""Run the production DOS Qcommon lifecycle proof in a fresh allowlisted guest."""

import atexit
import hashlib
import sys
import time

from dos_proof import BUILD_ROOT, DOSBOX_MEMORY_MB, ProofError, ProofRunError, prepare_guest, run_dosbox

EVIDENCE = BUILD_ROOT / "SILVER-RUN-EVIDENCE.TXT"
TIMEOUT_SECONDS = 20
FRAMES = 240


def main() -> int:
    run_id = f"SIL{time.time_ns() & 0xFFFFFFFFFFFF:012X}"
    try:
        stage = prepare_guest("silver", run_id, "SILVER.EXE", fixture=True)
        atexit.register(stage.close)
        fixture = stage.fixture
        assert fixture is not None
        log_path = stage.output("SILVER.LOG")
        marker_path = stage.output("SILVER.OK")
        output_path = stage.output("SILRUN.OUT")
        EVIDENCE.unlink(missing_ok=True)
        return_code = run_dosbox(
            stage,
            f"silver.exe -basedir H2TEST -dosrun {run_id} -dosframes {FRAMES} "
            "+set dedicated 1 +map synthetic > SILRUN.OUT",
            TIMEOUT_SECONDS,
        )
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
        print("Guest did not create SILVER.LOG, SILVER.OK, and SILRUN.OUT", file=sys.stderr)
        return 1

    log = log_path.read_text(encoding="ascii", errors="strict")
    marker = marker_path.read_text(encoding="ascii", errors="strict")
    output = output_path.read_text(encoding="ascii", errors="replace")
    required_log = [
        "artifact=SILVER.EXE",
        f"run-id={run_id}",
        "path=production-Qcommon",
        "fixture=synthetic-non-game",
        "input-pak-files=1",
        "mounted-unexpected-paks=absent",
        "original-game-data=absent",
        "qcommon-init-entry=PASS",
        "qcommon-init-completion=PASS",
        "fixture-filesystem=PASS",
        "client-effects-api-init=PASS",
        "static-game-binding=PASS",
        "static-client-effects-binding=PASS",
        "game-api-init=PASS",
        "player-api-init=PASS",
        "server-map=synthetic",
        "server-state=2",
        "zone-malloc-zero=PASS",
        "zone-tag-cleanup=PASS",
        "loopback-gamespy-rejected=PASS",
        "loopback-send=1",
        "loopback-receive=1",
        "loopback-payload=PASS",
        f"qcommon-frame-calls={FRAMES}",
        "sv-frame-calls=",
        "cl-frame-calls=",
        "server-frame-end=",
        "curtime-unit=milliseconds",
        "boundary=synthetic-dedicated-server-client-null-renderer",
        "gameplay=UNVERIFIED",
        "status=PASS",
    ]
    missing = [item for item in required_log if item not in log]
    expected_marker = f"SILVER DOS QCOMMON LIFECYCLE PASS\nrun-id={run_id}\nframes={FRAMES}\n"
    if missing or marker != expected_marker:
        print(f"Invalid guest evidence; missing={missing}\n--- log ---\n{log}\n--- marker ---\n{marker}", file=sys.stderr)
        return 1
    required_output = [
        "Loading Player (static)",
        "Server Initialization",
        "DOS loopback packets: sent=1 received=1 dropped=0",
    ]
    missing_output = [item for item in required_output if item not in output]
    if missing_output:
        print(f"Guest stdout lacks {missing_output}:\n{output}", file=sys.stderr)
        return 1

    evidence = (
        f"run-id={run_id}\n"
        f"fixture-sha256={hashlib.sha256(fixture.read_bytes()).hexdigest()}\n"
        "staged-input-allowlist=PASS\n"
        f"dosbox-memory-mb={DOSBOX_MEMORY_MB}\n"
        f"{log}"
        "guest-process-exit=PASS\n"
        "dosbox-process-left-running=NO\n"
    )
    try:
        stage.publish(("SILVER.LOG", "SILVER.OK", "SILRUN.OUT"))
    except ProofError as error:
        print(error, file=sys.stderr)
        return 1
    EVIDENCE.write_text(evidence, encoding="ascii")
    print(evidence, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
