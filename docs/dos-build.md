# DOS build

## Current status

**Classification: production lifecycle plus software-renderer and software-audio slices with synthetic data, and bounded production-path smokes against user-supplied original retail PAKs on every retail map; interactive play in DOSBox works for entering maps and combat, but a start-to-finish campaign remains unverified, so this is not yet a fully playable release.**

The DJGPP graph builds three independent executables:

- `HELLODOS.EXE` is the isolated SDL3 DOS hardware proof.
- `H2DOS.EXE` is the preserved interactive diagnostic shell.
- `SILVER.EXE` links the production engine, server, client, H2Common, game, Player, and client-effects source closures.

`SILVER.EXE` starts only through `Qcommon_Init` and advances only through `Qcommon_Frame`. One bounded proof runs a dedicated server on a generated IBSP v38 map. This reaches the real `GetGameAPI`, static Player initialization, client-effects API initialization, game `RunFrame`, zone allocator, and DOS loopback network paths.

A second bounded proof initializes the real non-dedicated client, local synthetic server, statically linked `ref_soft` API, and statically linked `snd_sdl3` API. The engine's console and main-menu code draw through the renderer before a deterministic frame registers and renders the synthetic BSP, a sprite entity, normal/additive particles, a dynamic light, and 2D M32/console glyphs. It then completes the local loopback handshake, reaches active/spawned client states, advances server game frames, and saves the active client framebuffer. SDL3 converts the renderer-owned RGBA framebuffer to its DOS VESA window surface.

The same integrated proof loads a generated WAV through the engine sound API, queues it as a local sound, mixes non-zero channel samples, and submits them from SDL3's stream callback to SDL's DOS `soundblaster` driver. OpenAL, EFX, HRTF, and OGG are excluded from the DOS target. The callback and main-thread circular-buffer operations synchronize through the SDL audio-stream lock, and shutdown pauses and destroys the stream before freeing its buffer. A shorter audio-only runner remains available for focused mixer checks.

These runs prove engine and format paths with generated fixtures, not gameplay.

No original Heretic II PAK is downloaded, generated, or committed by this repository. When legal user-supplied originals are placed under the ignored `.dos-data/base/` directory, an additional bounded smoke proof runs the production client, renderer, and sound path on them (see "Original-data production-path smoke"). That run is a non-interactive smoke test, not gameplay; gameplay remains unverified.

## Production lifecycle evidence

Build and run from the repository root:

```sh
./scripts/build-dos-bootstrap.sh
./scripts/run-dos-silver.py
./scripts/run-dos-renderer.py
./scripts/run-dos-audio.py
./scripts/run-dos-bootstrap.py
./scripts/run-dos-engine.py
./scripts/run-dos-realdata.py   # requires user-supplied original PAKs
```

The default build inputs are immutable and the build defaults to one job because unbounded DJGPP compilation proved unreliable on the supported host. `CMAKE_BUILD_PARALLEL_LEVEL` may override this only for local experimentation; the documented evidence uses `1`. The script builds all three executables and regenerates the deterministic synthetic PAK only when its content changes, then verifies it against `scripts/dos-fixture.sha256`. CMake compiles the reviewed paths in `dos/source-manifests/` and fails configuration if matching source directories gain, lose, or rename a file without an explicit manifest update.

Every runner creates a unique fresh guest directory under `.dos-build/runs/`, copies only an allowlisted executable, CWSDPMI, and (where needed) the hash-verified synthetic PAK, and validates that initial inventory before mounting it. Unexpected PAKs and loose assets cannot contaminate synthetic evidence. Each run uses DOSBox Staging 0.82.2 (`f8c24`) with 64 MiB guest memory in the foreground, enforces a timeout, terminates or kills its owned process on timeout, publishes validated guest-created outputs, and removes its temporary guest directory. The explicit memory setting avoids DOSBox's insufficient 16 MiB default for the integrated client, renderer, audio, Player, and game closure.

A successful lifecycle run creates:

```text
.dos-build/guest/SILVER.LOG
.dos-build/guest/SILVER.OK
.dos-build/guest/SILRUN.OUT
.dos-build/SILVER-RUN-EVIDENCE.TXT
```

The dedicated lifecycle run records:

```text
qcommon-init-entry=PASS
qcommon-init-completion=PASS
client-effects-api-init=PASS
game-api-init=PASS
player-api-init=PASS
server-map=synthetic
qcommon-frame-calls=240
sv-frame-calls=>0
cl-frame-calls=>0
server-frame-start=0
server-frame-end=>0
loopback-send=1
loopback-receive=1
zone-malloc-zero=PASS
zone-tag-cleanup=PASS
loopback-gamespy-rejected=PASS
input-pak-files=1
mounted-unexpected-paks=absent
staged-input-allowlist=PASS
boundary=synthetic-dedicated-server-client-null-renderer
gameplay=UNVERIFIED
```

Counts vary with host timing, so the runner checks milestones and the executable checks that the active server advances at least one game frame. `curtime` is updated in milliseconds while `Qcommon_Frame` receives measured microsecond deltas. This fork has no `Hunk_*` API; the proof exercises its real malloc-backed zone allocator and tag cleanup instead.

The software-renderer run additionally requires milestones like:

```text
renderer-api=PASS
renderer-console=PASS
renderer-world=PASS
renderer-sprite=PASS
renderer-particles=PASS
renderer-dlight=PASS
renderer-sky-six-sided=PASS
renderer-special-surfaces=animation-flow-warp-undulate-quake-PASS
renderer-find-surface=PASS
renderer-sky-face-mask=3f
renderer-malformed-bsp-rejected=10
renderer-malformed-book-rejected=1
renderer-malformed-font-rejected=1
renderer-malformed-fm-rejected=18
renderer-flex-accepted=1
renderer-flex-animation=PASS
renderer-flex-skeleton=PASS
renderer-flex-references=REF_CORVUS-PASS
renderer-flex-shadow=PASS
renderer-framebuffer=320x240
renderer-world-faces=1
renderer-triangles=2
renderer-depth-writes=>0
renderer-m8-images=>=2
renderer-m32-images=>=1
local-client-server-handshake=PASS
client-state=active
server-client-state=spawned
client-refresh-prepped=PASS
loopback-total-sent=>1
loopback-total-received=>1
renderer-active-client-screenshot=CLIENT.PPM
sound-software-mix=PASS
sound-sdl-submit=PASS
boundary=synthetic-local-client-server-renderer-mixer-soundblaster
gameplay=UNVERIFIED
```

It validates the UI, world, sky, special-surface, and client PPM files as complete 320x240 RGB images. Four flexible-model images cover frame 0, the interpolated midpoint, frame 1, and a changed skeletal pose. The runner checks color and changed-pixel counts and records SHA-256 hashes. `CLIENT.PPM` comes from the active local client after the synthetic server handshake. Current ignored evidence is under `.dos-build/guest/` with the host summary at `.dos-build/SOFTWARE-RENDERER-EVIDENCE.TXT`.

The production-audio run additionally requires:

```text
static-sound-binding=PASS
sound-api=SDL3 Software Mixer
sound-driver=soundblaster
sound-fixture-kind=synthetic-non-game
sound-malformed-wav-rejected=3
sound-wav-load=PASS
sound-software-mix=PASS
sound-sdl-submit=PASS
sound-callback-calls=>0
sound-submitted-bytes=>0
sound-submitted-nonzero-samples=>0
sound-mixed-nonzero-frames=>0
boundary=synthetic-client-software-mixer-soundblaster
gameplay=UNVERIFIED
```

The guest counters are read while holding SDL's audio-stream lock. They prove the generated WAV crossed the production loader, cache, channel mixer, callback, and SDL Sound Blaster submission boundaries; they do not prove game content or gameplay. The runner rejects callback-lock or shutdown errors and writes the ignored host summary to `.dos-build/DOS-AUDIO-EVIDENCE.TXT`.

The production loopback check sends through `NET_SendPacket(NS_CLIENT, ...)` and receives through `NET_GetPacket(NS_SERVER, ...)`. The DOS backend preserves `NA_LOOPBACK`, packet bounds, latency, send-rate, and receive-rate behavior for local play.

External multiplayer uses IPX (`dos/net_dos.c`): when an IPX driver answers INT 2Fh AX=7A00h at startup, the backend opens the engine's client/server sockets and routes `NA_IPX`/`NA_BROADCAST_IPX` datagrams through ECBs in DOS memory, using the same `net:node:socket` address strings as `net_wins.c`. DOSBox Staging supplies that API with `ipx=true` plus `ipxnet startserver`/`connect` (see `H2DOS_IPXSERVER`/`H2DOS_IPXCONNECT` in `scripts/run-dos-play.sh`); on real hardware the same calls run against a loaded IPX driver. A two-instance DOSBox proof (dedicated DOS server plus a DOS client connecting to its printed IPX address and entering the map) passes. IP/UDP networking is not implemented in the DOS target.

## Synthetic fixture

`.dos-build/guest/H2TEST/base/Htic2-9.pak` is generated by `scripts/make-dos-test-pak.py`. It contains only labeled, non-proprietary data:

- startup and empty user/menu configs;
- CRLF `gamemsg.txt` and `levelmsg.txt` tables;
- a fixture identity file and the existing `doscheck.txt` entry;
- `maps/synthetic.bsp`, a small renderable/collidable IBSP v38 map with one PVS-visible, RGB-lightmapped surface;
- generated M8 wall, animated, flowing, turbulent, quake, sprite, six-sided sky, and console-background images;
- generated M32 console glyph, menu-book segment, and UI panel images;
- one generated SP2 sprite and one generated BK menu book;
- one deterministic 22050 Hz, 16-bit mono synthetic WAV for the production mixer proof.

The PAK also contains explicitly malformed synthetic cases: truncated/invalid WAV chunks, BSP node/area/face/lightmap/visibility/texture-animation relationships, a BK frame outside its canvas, and NaN font metrics. The bounded renderer/audio proofs require the production validators to reject them before continuing with valid assets.

The BSP and assets exist only to exercise production collision, registration, rasterization, static APIs, game-frame paths, and malformed-input rejection. They are not copied from Heretic II and are not visual or gameplay substitutes. The fixture contains 69 entries and has SHA-256 `5c7ce341b48b59555731369cec029f5f6ce819bb00e62e65dfb2bdf297baaa6f`. The host verifies this pinned hash before staging, while the guest verifies its identity through `FS_LoadFile`, inventories its mounted PAK input, and records an engine checksum.

## Getting the game data

The DOS build is only an engine; it does not include the game. It needs the two data archives (\u201cPAK files\u201d) from a legal copy of Heretic II — the Steam or GOG release, or the original 1998 CD:

- `Htic2-0.pak` (about 206 MB)
- `Htic2-1.pak` (about 42 MB)

`scripts/run-dos-play.sh` searches for them automatically, in this order: the directory named by `H2DOS_ORIGINAL_DATA` (default `.dos-data/` in this repository), then the usual installation locations for the host operating system, and announces what it found. Searched locations include:

| Source | Windows | macOS | Linux |
|---|---|---|---|
| Steam | `C:\\Program Files (x86)\\Steam\\steamapps\\common\\Heretic 2\\base\\` | `~/Library/Application Support/Steam/steamapps/common/Heretic 2/base/` | `~/.local/share/Steam/steamapps/common/Heretic 2/base/` |
| Steam extra libraries | read from `libraryfolders.vdf` | read from `libraryfolders.vdf` | read from `libraryfolders.vdf` (incl. Flatpak Steam) |
| GOG | `C:\\GOG Games\\Heretic II\\base\\` (Galaxy: `...\\GOG Galaxy\\Games\\Heretic II\\base\\`) | `~/GOG Games/Heretic II/base/` | `~/GOG Games/Heretic II/base/`, Heroic under `~/Games/Heroic/` |
| Original CD | copy from `Setup\\zip\\h2.zip` on the disc | mounted disc auto-extracted from `/Volumes/.../Setup/zip/h2.zip` | mounted disc auto-extracted from `/media/.../Setup/zip/h2.zip` |

To point at an install manually, copy both files into `.dos-data/base/` in this repository, or set `H2DOS_ORIGINAL_DATA` to the folder that *contains* the `base/` folder (for example `H2DOS_ORIGINAL_DATA="$HOME/GOG Games/Heretic II"`). If nothing is found, the launcher prints these same locations and exits without starting DOSBox. The PAK files are identified by their `PACK` header before use, are only read (never modified), and are staged as copies under the ignored `.dos-build/play/` directory.

## Original-data production-path smoke

`scripts/run-dos-realdata.py` is an opt-in bounded smoke proof that runs the production path against user-owned original retail PAKs placed at `.dos-data/base/Htic2-0.pak` and `.dos-data/base/Htic2-1.pak`. The directory is git-ignored, the files are treated as read-only proprietary user data, and they are never copied into the repository or its commits. The script records the SHA-256 of each PAK in the evidence (currently `72e0b4b589…b545` and `6b552ade72…20ef` for the user's retail files), verifies the staged copies match the originals byte-for-byte, and removes that private run directory after a successful run exactly like the synthetic proofs. On failure the run directory is kept for debugging and must be deleted after inspection.

The guest runs `SILVER.EXE -dosrealdata -basedir H2REAL +set developer 1 +map tutorial2` with the proof flags passed as `H2DOS_*` environment variables, because the DOS command line is limited. `+exec` scripts are not honored by the engine's early command parsing on DOS, so the map command stays on the command line. The harness refuses any non-allowlisted PAK, requires exactly the two original inputs, and verifies the synthetic fixture identity is absent.

The proof loads the smallest retail BSP (`maps/tutorial2.bsp`) through the unmodified production `Qcommon_Frame` path: SV_SpawnServer entity spawning (including brush-model ropes and their persistent effect registrations), the local loopback handshake, client refresh registration of the real map, sprites, flexible models, and M8/M32 images, plus non-dedicated frames rendered by `ref_soft`. It also loads a real original WAV (`sound/Monsters/chicken/peck1.wav`, 494 bytes) through the production sound API, software mixer, and SDL3 Sound Blaster submission, and saves guest screenshots: `REALMENU.PPM`, `REALCON.PPM`, and the active client framebuffer `CLIENT.PPM`.

Evidence is published to `.dos-build/guest/REALDATA-EVIDENCE.TXT` with `boundary=original-paks-read-only-production-path-smoke-non-gameplay`, `gameplay=UNVERIFIED`, and guest counters such as frames presented, world faces, triangles, depth writes, sprite entities, flex models/entities, M8/M32 image counts, and sound mixer/submission counters.

The retail 1.06 data layout predates remaster asset paths: requests such as `players/male/*.m8` and `pics/menu/num_*.m32` do not exist in the retail PAKs. The DOS renderer resolves them through explicit player-alias entries and an `.m8`/`.m32` sibling retry (so the `FindFile` log lines for those names are probes, not failures); genuinely absent assets such as the Tornado pickup model still fall back and are counted in `realdata-missing-assets=`.

## Preserved diagnostic proofs

The earlier bounded proofs remain available:

```sh
./scripts/run-dos-bootstrap.py
./scripts/run-dos-engine.py
```

They still validate SDL3 VESA presentation, guest-created screenshots, input dispatch, and SDL's Sound Blaster diagnostic tone. That tone does not prove the game mixer.

## Pinned public dependencies

Requirements:

- Nix with flake support;
- `curl`, `shasum`, `unzip`, and Python 3;
- DOSBox Staging 0.82.2 (`f8c24`), the accepted evidence runtime.

The default DJGPP, CMake, and Ninja packages come from immutable Nixpkgs revision `f2b17eb1b90f6a359082035b4538d4c63017aec3`. SDL is pinned to commit `b669ac244e10fbab4734c7389151f7a4e8ac725f` with archive SHA-256 `9171e2ecec13d0cbe90d49a5faf9dee56c387ff161f6467b42b39c381d0eb724`.

CWSDPMI release 7 (`csdpmi7b.zip`) is pinned by SHA-256 `deacda0488e1cdd7c4a9f32fab45662b34c0ed6b2d7d4d13bc07041b62004a8c`. Its executable and notice are extracted only into the ignored guest directory.

Downloaded sources, build products, fixtures, logs, and screenshots stay under ignored `.dos-deps/` and `.dos-build/` directories.

## Artifact and symbol checks

DJGPP prefixes C symbols with an underscore. The fresh linked `SILVER.EXE` contains:

```text
_Qcommon_Init
_Qcommon_Frame
_CL_Frame
_SV_Frame
_GetGameAPI
_H2R_ClientEffects_GetfxAPI
_GetPlayerAPI
_H2R_Player_P_Init
_NET_SendPacket
_Z_TagMalloc
```

Re-run the check with:

```sh
nix shell github:NixOS/nixpkgs/f2b17eb1b90f6a359082035b4538d4c63017aec3#djgpp_i586 -c \
  i586-pc-msdosdjgpp-nm .dos-build/guest/SILVER.EXE | \
  grep -E ' (_Qcommon_Init|_Qcommon_Frame|_CL_Frame|_SV_Frame|_GetGameAPI|_H2R_ClientEffects_GetfxAPI|_GetPlayerAPI|_H2R_Player_P_Init|_NET_SendPacket|_Z_TagMalloc)$'
```

Symbols and compilation are not runtime proof; use the bounded guest runner.

## DOS software renderer boundary

`dos/ref_soft/` is an original implementation based on this repository's public format declarations and existing renderer behavior. No q2dos or other third-party renderer code was copied, so there is no external file-level provenance to record.

Implemented production paths:

- static `refexport_t` loading and SDL3 VESA presentation;
- M8 palette and M32 RGBA loading with bounds checks;
- BK menu books, SP2 sprites, console chars, available big-font metrics/images, fills, tiles, fades, names, and paletted cinematics;
- BSP byte-range and cross-lump semantic validation, bounded visibility RLE and lightmaps, view-leaf/PVS/area visibility, near clipping, perspective-correct texture and lightmap coordinates, RGB lightmaps/lightstyles, z/depth writes, alpha surfaces, bounded animated-texture chains, periodic flow, turbulent water, undulate and quake deformation, and `FindSurface` segment/polygon queries;
- projected six-sided M8 skies with axis rotation and a bounded cache for unchanged views;
- BSP brush and SP2 sprite entities, bounded standard-frame Raven flexible models, generic normal/additive particles, dynamic lights, and full-screen blends;
- flexible-model skins, mesh nodes, frame and skeletal interpolation, reference metadata, point/model lighting, alpha materials, and a visible FM shadow-material path.

Synthetic checker fallback is enabled only when the renderer finds the explicit fixture identity. Missing assets in a normal data directory return no image and are logged.

## Remaining boundary

The renderer accepts bounded standard-frame Raven flexible models and renders validated mesh command spans with skins, frame interpolation, model lighting, skeletal/reference interpolation, node policy, and translucent shadow material. Compressed FM blocks and malformed skeleton/reference blocks without serialized placements are rejected rather than misrendered. The permanent proof uses an original synthetic Corvus fixture with 9 vertices, 3 mesh nodes, 2 frames, and 18 malformed variants. Original retail data now loads and renders through a bounded non-interactive smoke (see "Original-data production-path smoke"), but interactive performance and visual parity against the reference GL renderer remain unverified. Exact per-type behavior for all 62 particle effects and complete sprite variants also remain incomplete.

The generated map proves the local client/server handshake and precache sequence only for the labeled synthetic fixture. The real-data runner accepts `H2DOS_REALDATA_MAP` and has now been run per map across all 29 retail BSPs (each reaching the active/spawned client state with rendered frames; `sspalace` initially exposed an over-strict submodel headnode check, since fixed), and an `H2DOS_SAVELOAD=1` run proves a console save/load round trip returns to the spawned state. These are load and lifecycle proofs, not campaign completion: objectives, transitions, bosses, cinematics, and long-session stability still need a human playthrough before any fully-playable claim. OGG music remains excluded from the DOS target, physical-hardware IPX is implemented against the standard driver API but untested on a real machine, and hardware beyond SDL's DOSBox-provided Sound Blaster driver remains unverified.
