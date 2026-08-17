#!/usr/bin/env python3
"""Run the preserved H2DOS diagnostic shell in a fresh synthetic guest."""

import atexit
import sys
import time

from dos_proof import ProofError, ProofRunError, prepare_guest, run_dosbox

TIMEOUT_SECONDS = 20
WIDTH = 320
HEIGHT = 200


def main() -> int:
    run_id = f"ENG{time.time_ns() & 0xFFFFFFFF:08X}"
    try:
        stage = prepare_guest("engine", run_id, "H2DOS.EXE", fixture=True)
        atexit.register(stage.close)
        log_path = stage.output("H2DOS.LOG")
        marker_path = stage.output("H2ENG.OK")
        screenshot_path = stage.output("H2DOS.PPM")
        output_path = stage.output("H2RUN.OUT")
        return_code = run_dosbox(
            stage, f"h2dos.exe {run_id} -basedir H2TEST > H2RUN.OUT", TIMEOUT_SECONDS
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
    required_outputs = (log_path, marker_path, screenshot_path, output_path)
    if not all(path.is_file() for path in required_outputs):
        print("Guest did not create H2DOS.LOG, H2ENG.OK, H2DOS.PPM, and H2RUN.OUT", file=sys.stderr)
        if output_path.is_file():
            print(output_path.read_text(encoding="ascii", errors="replace"), file=sys.stderr)
        return 1

    log = log_path.read_text(encoding="ascii")
    marker = marker_path.read_text(encoding="ascii")
    required_log = [
        f"run-id={run_id}", "binding=monolithic-static", "opengl=disabled",
        "openal=disabled", "ffmpeg=disabled", "network=disabled",
        "core-command-buffer=PASS", "core-cvar=PASS", "core-alias=PASS",
        "core-macro-expansion=PASS", "core-wait=PASS", "engine-filesystem=PASS",
        "resource-kind=synthetic-non-game-test-fixture", "original-game-data=absent",
        "video-driver=vesa", "audio-driver=soundblaster", "ui=interactive-diagnostic-shell",
        "input-event-pipeline=PASS", "status=PASS",
    ]
    missing = [value for value in required_log if value not in log]
    if missing:
        print(f"Guest log is missing {missing}:\n{log}", file=sys.stderr)
        return 1
    expected_marker = f"H2DOS VERTICAL SLICE PASS\nrun-id={run_id}\n"
    if marker != expected_marker:
        print(f"Guest marker failed validation:\n{marker}", file=sys.stderr)
        return 1

    image = screenshot_path.read_bytes()
    header = f"P6\n{WIDTH} {HEIGHT}\n255\n".encode("ascii")
    if not image.startswith(header) or len(image) != len(header) + WIDTH * HEIGHT * 3:
        print(f"Guest screenshot is not a complete {WIDTH}x{HEIGHT} RGB PPM file", file=sys.stderr)
        return 1
    pixels = image[len(header):]
    colors = {pixels[index:index + 3] for index in range(0, len(pixels), 3)}
    if len(colors) < 6 or colors == {b"\0\0\0"}:
        print(f"Guest screenshot has only {len(colors)} color(s)", file=sys.stderr)
        return 1

    try:
        stage.publish(("H2DOS.LOG", "H2ENG.OK", "H2DOS.PPM", "H2RUN.OUT"))
    except ProofError as error:
        print(error, file=sys.stderr)
        return 1
    print(f"Validated guest vertical-slice run {run_id} and {WIDTH}x{HEIGHT} screenshot with {len(colors)} colors")
    print(log, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
