#!/usr/bin/env python3
"""Run the synthetic production mixer-to-Sound-Blaster proof in a fresh guest."""

import atexit
import hashlib
import sys
import time

from dos_proof import BUILD_ROOT, DOSBOX_MEMORY_MB, ProofError, ProofRunError, prepare_guest, run_dosbox

EVIDENCE = BUILD_ROOT / "DOS-AUDIO-EVIDENCE.TXT"
TIMEOUT_SECONDS = 30
FRAMES = 8


def main() -> int:
    run_id = f"SND{time.time_ns() & 0xFFFFFFFFFFFF:012X}"
    try:
        stage = prepare_guest("audio", run_id, "SILVER.EXE", fixture=True)
        atexit.register(stage.close)
        fixture = stage.fixture
        assert fixture is not None
        log_path = stage.output("SILVER.LOG")
        marker_path = stage.output("SILVER.OK")
        output_path = stage.output("SWAUDIO.OUT")
        EVIDENCE.unlink(missing_ok=True)
        return_code = run_dosbox(
            stage,
            f"silver.exe -basedir H2TEST -dosrun {run_id} -dosframes {FRAMES} "
            "-dosaudio +set dedicated 0 > SWAUDIO.OUT",
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
        print("Guest did not create complete DOS audio evidence", file=sys.stderr)
        if output_path.is_file():
            print(output_path.read_text(encoding="ascii", errors="replace"), file=sys.stderr)
        return 1

    log = log_path.read_text(encoding="ascii", errors="strict")
    marker = marker_path.read_text(encoding="ascii", errors="strict")
    output = output_path.read_text(encoding="ascii", errors="replace")
    required_log = [
        f"run-id={run_id}", "fixture=synthetic-non-game", "input-pak-files=1",
        "mounted-unexpected-paks=absent", "original-game-data=absent",
        "qcommon-init-completion=PASS", "static-sound-binding=PASS",
        "sound-malformed-wav-rejected=3", "sound-api=SDL3 Software Mixer",
        "sound-driver=soundblaster", "sound-fixture=sound/dos/synthetic.wav",
        "sound-fixture-kind=synthetic-non-game", "sound-wav-load=PASS",
        "sound-software-mix=PASS", "sound-sdl-submit=PASS", "sound-callback-calls=",
        "sound-submitted-bytes=", "sound-submitted-nonzero-samples=",
        "sound-mixed-nonzero-frames=", "sound-cached-sounds=", "sound-cached-source-samples=",
        f"qcommon-frame-calls={FRAMES}", "boundary=synthetic-client-software-mixer-soundblaster",
        "gameplay=UNVERIFIED", "status=PASS",
    ]
    missing = [item for item in required_log if item not in log]
    expected_marker = f"SILVER DOS QCOMMON LIFECYCLE PASS\nrun-id={run_id}\nframes={FRAMES}\n"
    required_output = [
        "Loading snd_sdl3", "SDL3 audio driver: 'soundblaster'",
        "SDL3 audio backend initialized.", "SDL audio device shut down.",
    ]
    missing_output = [item for item in required_output if item not in output]
    shutdown_errors = [
        line for line in output.splitlines()
        if "Couldn't pause SDL3" in line or "Couldn't unlock SDL3" in line
    ]
    if missing or marker != expected_marker or missing_output or shutdown_errors:
        print(
            f"Invalid DOS audio evidence; missing_log={missing}, missing_output={missing_output}, "
            f"shutdown_errors={shutdown_errors}\n--- log ---\n{log}\n--- output ---\n{output}",
            file=sys.stderr,
        )
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
        stage.publish(("SILVER.LOG", "SILVER.OK", "SWAUDIO.OUT"))
    except ProofError as error:
        print(error, file=sys.stderr)
        return 1
    EVIDENCE.write_text(evidence, encoding="ascii")
    print(evidence, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
