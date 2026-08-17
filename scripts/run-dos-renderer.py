#!/usr/bin/env python3
"""Run the bounded synthetic local-server, renderer, and mixer integration proof."""

import atexit
import hashlib
from pathlib import Path
import sys
import time

from dos_proof import BUILD_ROOT, DOSBOX_MEMORY_MB, ProofError, ProofRunError, prepare_guest, run_dosbox

EVIDENCE = BUILD_ROOT / "SOFTWARE-RENDERER-EVIDENCE.TXT"
TIMEOUT_SECONDS = 90
FRAMES = 36
WIDTH = 320
HEIGHT = 240


def read_ppm(path: Path) -> bytes:
    payload = path.read_bytes()
    header = f"P6\n{WIDTH} {HEIGHT}\n255\n".encode("ascii")
    if not payload.startswith(header) or len(payload) != len(header) + WIDTH * HEIGHT * 3:
        raise ValueError(f"{path.name} is not a complete {WIDTH}x{HEIGHT} RGB PPM")
    return payload[len(header):]


def sampled_colors(pixels: bytes) -> set[bytes]:
    return {pixels[index:index + 3] for index in range(0, len(pixels), 3)}


def main() -> int:
    run_id = f"SWR{time.time_ns() & 0xFFFFFFFFFFFF:012X}"
    try:
        stage = prepare_guest("renderer", run_id, "SILVER.EXE", fixture=True)
        atexit.register(stage.close)
        fixture = stage.fixture
        assert fixture is not None
        log_path = stage.output("SILVER.LOG")
        marker_path = stage.output("SILVER.OK")
        output_path = stage.output("SWREND.OUT")
        menu_path = stage.output("MENU.PPM")
        console_path = stage.output("CONSOLE.PPM")
        render_path = stage.output("RENDER.PPM")
        surface_old_path = stage.output("SURFOLD.PPM")
        surface_new_path = stage.output("SURFNEW.PPM")
        sky_path = stage.output("SKY.PPM")
        flex_old_path = stage.output("FLEXOLD.PPM")
        flex_midpoint_path = stage.output("FLEXMID.PPM")
        flex_new_path = stage.output("FLEXNEW.PPM")
        flex_skeletal_path = stage.output("FLEXSKEL.PPM")
        effects_path = stage.output("EFFECTS.PPM")
        client_path = stage.output("CLIENT.PPM")
        EVIDENCE.unlink(missing_ok=True)
        return_code = run_dosbox(
            stage,
            f"silver.exe -basedir H2TEST -dosrun {run_id} -dosframes {FRAMES} "
            "-dosrenderer -dosaudio +set dedicated 0 +map synthetic > SWREND.OUT",
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
    outputs = (log_path, marker_path, output_path, menu_path, console_path, render_path,
               surface_old_path, surface_new_path, sky_path, flex_old_path, flex_midpoint_path,
               flex_new_path, flex_skeletal_path, client_path)
    if not all(path.is_file() for path in outputs):
        print("Guest did not create complete integrated renderer/audio evidence", file=sys.stderr)
        if output_path.is_file():
            print(output_path.read_text(encoding="ascii", errors="replace"), file=sys.stderr)
        return 1

    log = log_path.read_text(encoding="ascii", errors="strict")
    marker = marker_path.read_text(encoding="ascii", errors="strict")
    required = [
        f"run-id={run_id}", "path=production-Qcommon", "fixture=synthetic-non-game",
        "input-pak-files=1", "mounted-unexpected-paks=absent", "original-game-data=absent",
        "qcommon-init-completion=PASS", "static-renderer-binding=PASS",
        "static-sound-binding=PASS", "renderer-api=PASS", "renderer-console=PASS",
        "renderer-world=PASS", "renderer-sprite=PASS", "renderer-particles=PASS",
        "renderer-malformed-bsp-rejected=10", "renderer-malformed-book-rejected=1",
        "renderer-malformed-font-rejected=1", "renderer-malformed-fm-rejected=18",
        "sound-malformed-wav-rejected=3", "renderer-dlight=PASS",
        "renderer-sky-six-sided=PASS", "renderer-sky-invalid-params-sanitized=PASS",
        "renderer-sprite-variants=dynamic-variable-line-PASS",
        "renderer-sprite-additive-line=PASS",
        "renderer-effect-particles=typed-atlas-softmask-additive-PASS",
        "renderer-effects-screenshot=EFFECTS.PPM",
        "renderer-sprite-dynamic=", "renderer-sprite-variable=", "renderer-sprite-line=",
        "renderer-particles-textured=", "renderer-sprite-variant-checksums=",
        "renderer-particle-type-checksums=",
        "renderer-special-surfaces=animation-flow-warp-undulate-quake-PASS",
        "renderer-find-surface=PASS", "renderer-sky-face-mask=3f",
        "renderer-surface-old-screenshot=SURFOLD.PPM", "renderer-surface-new-screenshot=SURFNEW.PPM",
        "renderer-sky-screenshot=SKY.PPM", "renderer-animated-faces=", "renderer-flowing-faces=",
        "renderer-turbulent-faces=", "renderer-undulate-faces=", "renderer-quake-faces=",
        "renderer-surface-queries=1", "renderer-flex-accepted=1", "renderer-flex-animation=PASS",
        "renderer-flex-skeleton=PASS", "renderer-flex-references=REF_CORVUS-PASS",
        "renderer-flex-node-policy=PASS", "renderer-flex-lighting-dlight=PASS",
        "renderer-flex-shadow=PASS", "renderer-flex-fixture=synthetic-non-game",
        "renderer-flex-old-screenshot=FLEXOLD.PPM", "renderer-flex-midpoint-screenshot=FLEXMID.PPM",
        "renderer-flex-new-screenshot=FLEXNEW.PPM", "renderer-flex-skeletal-screenshot=FLEXSKEL.PPM",
        "renderer-flex-reference-left-hand=0.000,-12.000,9.000",
        "renderer-flex-reference-head=0.000,2.500,34.500", "renderer-flex-models=1",
        "renderer-flex-entities=", "renderer-flex-triangles=", "renderer-flex-shadow-entities=",
        "renderer-reference-updates=", f"renderer-framebuffer={WIDTH}x{HEIGHT}",
        "renderer-world-faces=", "renderer-triangles=", "renderer-depth-writes=",
        "renderer-m8-images=", "renderer-m32-images=", "renderer-menu-screenshot=MENU.PPM",
        "renderer-console-screenshot=CONSOLE.PPM", "renderer-world-screenshot=RENDER.PPM",
        "local-client-server-handshake=PASS", "client-state=active",
        "server-client-state=spawned", "client-refresh-prepped=PASS",
        "loopback-total-sent=", "loopback-total-received=", "loopback-total-dropped=0",
        "renderer-active-client-screenshot=CLIENT.PPM", "sound-api=SDL3 Software Mixer",
        "sound-driver=soundblaster", "sound-wav-load=PASS", "sound-software-mix=PASS",
        "sound-sdl-submit=PASS", "sound-submitted-nonzero-samples=",
        f"qcommon-frame-calls={FRAMES}",
        "boundary=synthetic-local-client-server-renderer-mixer-soundblaster",
        "gameplay=UNVERIFIED", "status=PASS",
    ]
    missing = [value for value in required if value not in log]
    expected_marker = f"SILVER DOS QCOMMON LIFECYCLE PASS\nrun-id={run_id}\nframes={FRAMES}\n"
    if missing or marker != expected_marker:
        print(f"Invalid renderer evidence; missing={missing}\n--- log ---\n{log}\n--- marker ---\n{marker}", file=sys.stderr)
        return 1

    try:
        menu_pixels = read_ppm(menu_path)
        console_pixels = read_ppm(console_path)
        render_pixels = read_ppm(render_path)
        surface_old_pixels = read_ppm(surface_old_path)
        surface_new_pixels = read_ppm(surface_new_path)
        sky_pixels = read_ppm(sky_path)
        flex_old_pixels = read_ppm(flex_old_path)
        flex_midpoint_pixels = read_ppm(flex_midpoint_path)
        flex_new_pixels = read_ppm(flex_new_path)
        flex_skeletal_pixels = read_ppm(flex_skeletal_path)
        effects_pixels = read_ppm(effects_path)
        client_pixels = read_ppm(client_path)
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1
    menu_colors = sampled_colors(menu_pixels)
    console_colors = sampled_colors(console_pixels)
    render_colors = sampled_colors(render_pixels)
    sky_colors = sampled_colors(sky_pixels)
    effects_colors = sampled_colors(effects_pixels)
    client_colors = sampled_colors(client_pixels)
    changed_pixels = sum(
        console_pixels[index:index + 3] != render_pixels[index:index + 3]
        for index in range(0, len(render_pixels), 3)
    )
    client_changed_pixels = sum(
        render_pixels[index:index + 3] != client_pixels[index:index + 3]
        for index in range(0, len(client_pixels), 3)
    )
    surface_changed_pixels = sum(
        surface_old_pixels[index:index + 3] != surface_new_pixels[index:index + 3]
        for index in range(0, len(surface_old_pixels), 3)
    )
    flex_midpoint_changed_pixels = sum(
        flex_old_pixels[index:index + 3] != flex_midpoint_pixels[index:index + 3]
        for index in range(0, len(flex_old_pixels), 3)
    )
    flex_new_changed_pixels = sum(
        flex_midpoint_pixels[index:index + 3] != flex_new_pixels[index:index + 3]
        for index in range(0, len(flex_old_pixels), 3)
    )
    flex_skeletal_changed_pixels = sum(
        flex_midpoint_pixels[index:index + 3] != flex_skeletal_pixels[index:index + 3]
        for index in range(0, len(flex_old_pixels), 3)
    )
    if (
        len(menu_colors) < 3 or len(console_colors) < 3 or len(render_colors) < 12
        or len(client_colors) < 3 or len(sky_colors) < 8 or len(effects_colors) < 8
        or changed_pixels < 5000
        or client_changed_pixels < 1000 or surface_changed_pixels < 100
        or flex_midpoint_changed_pixels < 40 or flex_new_changed_pixels < 40
        or flex_skeletal_changed_pixels < 20
    ):
        print(
            f"Weak screenshot evidence: menu colors={len(menu_colors)}, console colors={len(console_colors)}, "
            f"render colors={len(render_colors)}, sky colors={len(sky_colors)}, "
            f"effects colors={len(effects_colors)}, "
            f"client colors={len(client_colors)}, changed pixels={changed_pixels}, "
            f"client changed pixels={client_changed_pixels}, surface changed pixels={surface_changed_pixels}, "
            f"flex midpoint changed pixels={flex_midpoint_changed_pixels}, "
            f"flex new changed pixels={flex_new_changed_pixels}, "
            f"flex skeletal changed pixels={flex_skeletal_changed_pixels}",
            file=sys.stderr,
        )
        return 1

    output = output_path.read_text(encoding="ascii", errors="replace")
    for value in (
        "Loading ref_soft", "Refresh: DOS SDL3 software", "SDL video driver is \"vesa\"",
        "Loading snd_sdl3", "SDL3 audio driver: 'soundblaster'", "Loading Player (static)",
        "Server Initialization", "SDL audio device shut down.",
    ):
        if value not in output:
            print(f"Guest stdout lacks {value!r}:\n{output}", file=sys.stderr)
            return 1

    evidence = (
        f"run-id={run_id}\n"
        f"fixture-sha256={hashlib.sha256(fixture.read_bytes()).hexdigest()}\n"
        "staged-input-allowlist=PASS\n"
        f"dosbox-memory-mb={DOSBOX_MEMORY_MB}\n"
        f"menu-sha256={hashlib.sha256(menu_path.read_bytes()).hexdigest()}\n"
        f"console-sha256={hashlib.sha256(console_path.read_bytes()).hexdigest()}\n"
        f"render-sha256={hashlib.sha256(render_path.read_bytes()).hexdigest()}\n"
        f"surface-old-sha256={hashlib.sha256(surface_old_path.read_bytes()).hexdigest()}\n"
        f"surface-new-sha256={hashlib.sha256(surface_new_path.read_bytes()).hexdigest()}\n"
        f"sky-sha256={hashlib.sha256(sky_path.read_bytes()).hexdigest()}\n"
        f"flex-old-sha256={hashlib.sha256(flex_old_path.read_bytes()).hexdigest()}\n"
        f"flex-midpoint-sha256={hashlib.sha256(flex_midpoint_path.read_bytes()).hexdigest()}\n"
        f"flex-new-sha256={hashlib.sha256(flex_new_path.read_bytes()).hexdigest()}\n"
        f"flex-skeletal-sha256={hashlib.sha256(flex_skeletal_path.read_bytes()).hexdigest()}\n"
        f"client-sha256={hashlib.sha256(client_path.read_bytes()).hexdigest()}\n"
        f"menu-unique-colors={len(menu_colors)}\n"
        f"console-unique-colors={len(console_colors)}\n"
        f"render-unique-colors={len(render_colors)}\n"
        f"sky-unique-colors={len(sky_colors)}\n"
        f"effects-sha256={hashlib.sha256(effects_path.read_bytes()).hexdigest()}\n"
        f"effects-unique-colors={len(effects_colors)}\n"
        f"client-unique-colors={len(client_colors)}\n"
        f"console-render-changed-pixels={changed_pixels}\n"
        f"render-client-changed-pixels={client_changed_pixels}\n"
        f"surface-animation-changed-pixels={surface_changed_pixels}\n"
        f"flex-midpoint-changed-pixels={flex_midpoint_changed_pixels}\n"
        f"flex-new-changed-pixels={flex_new_changed_pixels}\n"
        f"flex-skeletal-changed-pixels={flex_skeletal_changed_pixels}\n"
        f"{log}"
        "guest-process-exit=PASS\n"
        "dosbox-process-left-running=NO\n"
    )
    try:
        stage.publish(("SILVER.LOG", "SILVER.OK", "SWREND.OUT", "MENU.PPM", "CONSOLE.PPM",
                       "RENDER.PPM", "SURFOLD.PPM", "SURFNEW.PPM", "SKY.PPM",
                       "FLEXOLD.PPM", "FLEXMID.PPM", "FLEXNEW.PPM", "FLEXSKEL.PPM",
                       "EFFECTS.PPM", "CLIENT.PPM"))
    except ProofError as error:
        print(error, file=sys.stderr)
        return 1
    EVIDENCE.write_text(evidence, encoding="ascii")
    print(evidence, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
