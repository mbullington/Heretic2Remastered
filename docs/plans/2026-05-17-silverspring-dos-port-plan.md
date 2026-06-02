# Silverspring DOS port plan

Date: 2026-05-17

## Summary

Silverspring DOS is a proposed DJGPP/MS-DOS target for Silverspring. It should use SDL3's DOS backend for video, audio, input, timers, and filesystem basics, while treating DOS as a first-class low-spec platform rather than a novelty build.

The target is real late-1990s PC hardware first, with DOSBox and FreeDOS used as development and regression environments. The DOS build should be conservative: original data, software/VESA rendering, Sound Blaster audio through SDL3, keyboard and mouse input, and a monolithic executable.

The DOS target is separate from the modern Classic+ and Enhanced desktop renderer goals. It should not attempt to bring GL3-era effects to DOS.

## Goals

- Build a real DJGPP DOS executable for Silverspring.
- Target late-1990s protected-mode DOS PCs, not only emulators.
- Use SDL3's DOS backend where possible.
- Use VESA framebuffer output, starting with a software renderer.
- Use SDL3 audio on top of Sound Blaster-compatible hardware.
- Use keyboard and mouse input through SDL3.
- Support original Heretic II PAK data.
- Ship a DOS package that is clear, small, and free of original game data.
- Keep the DOS build maintainable through platform gates and monolithic linking.

## Non-goals

- No GL3 renderer on DOS.
- No bloom, SSAO, reflections, HD textures, or HD videos on DOS.
- No OpenAL, EFX, or HRTF on DOS.
- No dynamic DLL-style game, renderer, sound, or effects modules on DOS.
- No Steam/GOG auto-detection on DOS.
- No multiplayer in the first DOS milestone.
- No CI.
- No requirement to support every DOS-era sound card directly. SDL3's Sound Blaster path is the starting point.

## Hardware target

The DOS port should aim at real late-1990s hardware from the beginning.

### Baseline target

Recommended baseline:

- CPU: Pentium II 266 MHz or equivalent.
- RAM: 64 MB.
- Video: VESA 2.0-compatible SVGA card with linear framebuffer support.
- Audio: Sound Blaster 16 or compatible.
- Input: AT/PS/2 keyboard and PS/2 or serial mouse.
- Storage: local FAT filesystem with enough space for the executable, support files, saves, and original PAK files.
- DOS runtime: MS-DOS 6.22/7.x or FreeDOS with a DPMI host.

### Stretch target

- CPU: Pentium III 450 MHz or faster.
- RAM: 128 MB.
- Video: VESA 2.0/3.0 with fast linear framebuffer.
- Audio: Sound Blaster AWE32/AWE64, compatible PCI cards that expose DOS SB compatibility, or DOSBox-compatible SB emulation.
- Optional later hardware path: 3Dfx Voodoo/Glide support, after VESA software rendering is working.

### Minimum target to investigate

Q2DOS reports Quake II as playable on a Pentium 200 MHz with at least 48 MB RAM. Silverspring should use that as a useful reference point, not a promise.

- CPU: Pentium MMX 200 MHz.
- RAM: 48 MB.
- Video: VGA Mode 13h, Mode X 320x240, or VESA 1.2 banked framebuffer.
- Audio: Sound Blaster Pro or SB16-compatible device.

This lower target is not guaranteed. It should be used to understand performance limits and to keep the renderer honest.

## Development environments

Use emulators for iteration, but do not let emulator behavior define the port.

Recommended development environments:

- DOSBox Staging.
- DOSBox-X.
- FreeDOS in a VM.
- Real Pentium II/III hardware as soon as the executable reaches the main loop.

Real hardware should be part of the test matrix before the port is announced as supported.

## SDL3 DOS backend assumptions

SDL3's DOS backend provides the platform layer needed for a DOS port:

- VGA and VESA framebuffer video.
- Indexed and RGB framebuffer modes.
- Sound Blaster audio through IRQ-driven DMA.
- Keyboard input.
- Mouse input.
- Joystick input.
- Timers.
- Cooperative threading support.
- Basic filesystem path support.

DOS executables require a DPMI host. The package should include or document `CWSDPMI.EXE`.

SDL3 DOS does not provide OpenGL or dynamic shared-object loading. The Silverspring DOS build must not depend on either.

## Q2DOS reference learnings

The Quake II for DOS port at `github.com/basecq/q2dos` is directly relevant. It proves that a Quake II-family engine can run as a serious DJGPP DOS port on late-1990s machines, and it highlights several issues Silverspring should account for early.

Key lessons to incorporate:

- **Real hardware target:** Q2DOS documents a Pentium 200 MHz and 48 MB RAM as a practical lower bound for Quake II. Silverspring's baseline can stay higher, but performance testing should include a similar low-end reference if hardware is available.
- **VGA and VESA fallback matter:** Q2DOS supports VGA 320x200, Mode X 320x240, VESA linear framebuffer, and banked SVGA. Silverspring should not assume VESA LFB is always available on real hardware. SDL3's DOS backend supports both VESA LFB and banked modes, so the renderer should be tested with both.
- **8-bit software rendering is valuable:** Q2DOS software rendering is 8-bit paletted. Silverspring should prefer an 8-bit paletted path for the first DOS software renderer, with RGB output treated as a later compatibility path if needed.
- **Long filename support needs a policy:** Q2DOS warns when no LFN driver is loaded and recommends DOSLFN. Silverspring should be PAK-first and 8.3-host-path-friendly, but it should detect missing long filename support if loose files, mods, or replacement assets are enabled.
- **CWSDPMI version matters:** Q2DOS recommends shipping the known-good `CWSDPMI.EXE` beside the executable. Silverspring should do the same or document the exact DPMI host expectation.
- **Avoid EMM386-related instability:** Q2DOS warns against EMM386 for memory stability. Silverspring's DOS docs should include memory-manager guidance for real hardware.
- **Expose memory diagnostics:** Q2DOS has a `MEMSTATS` command for physical and virtual memory. Silverspring should add a DOS memory report command early.
- **LFB write-combining can be important:** Q2DOS recommends utilities such as FastVid or MTRRLFBE for faster linear framebuffer writes. Silverspring's real-hardware docs should mention write-combining utilities as optional performance aids.
- **DXE3 is possible but not first:** Q2DOS can load video drivers and game modules as DXE3. Silverspring should stay monolithic for the first DOS target, but DXE3 can remain a later option if modular DOS builds become important.
- **3Dfx can be a later hardware path:** Q2DOS supports 3Dfx OpenGL through Glide/Mesa/Sage. Silverspring should keep 3Dfx/Glide as a long-term idea after the VESA software renderer works. If pursued, Sage-style MiniGL/Glide paths may be more practical than full Mesa.
- **Fixed-point media decoders are useful later:** Q2DOS supports OGG through Tremor. Silverspring should not make OGG music a first DOS milestone, but Tremor is a credible later route for music playback.
- **Networking is feasible but separable:** Q2DOS uses Watt-32 and packet drivers. Silverspring should keep networking out of the first DOS milestone, then use Q2DOS and Hammer of Thyrion as references for a later Watt-32 path.
- **Compiler flags need measurement:** Q2DOS uses `-fno-strict-aliasing`, `-fomit-frame-pointer`, and notes the performance cost of `-ffloat-store`. Silverspring should document DJGPP compiler flags and measure any float-precision changes before enabling them.

## Licensing notes for DOS work

This section is not legal advice. It is a checklist of license and provenance issues to resolve before copying code, bundling libraries, or shipping DOS binaries.

### Current repository provenance

The current repository already has a top-level `LICENSE` that warns it is not a verified clean GPLv3 source release. The DOS plan must not assume that adding GPL-compatible dependencies fixes that provenance issue.

Before release work, create a dedicated license inventory for:

- engine, game, client, renderer, sound, and tool source files
- reverse-engineered or reconstructed code
- Raven/Heretic II Toolkit-derived files
- third-party libraries in `include/` and `lib/`
- generated code such as GL loaders
- any bundled runtime binaries
- any DOS-specific helper binaries

### Q2DOS as a reference

Q2DOS source files are mostly Quake II/id Software GPL code with headers stating GPL version 2 or later. Treat Q2DOS as a reference implementation unless a specific file's license and provenance are checked.

Rules for using Q2DOS material:

- Prefer architectural learnings over copied code.
- If code is copied, record the source file, commit, copyright notice, and license.
- GPLv2-or-later code can generally be used under GPLv3 terms, but only if the file is actually GPLv2-or-later and no other incompatible terms apply.
- Do not copy Q2DOS third-party code without reviewing that component's own license.

### SDL3

SDL3 uses the zlib license. It is suitable for this project from a copyleft-compatibility perspective, but DOS builds should still ship SDL3 notices and source/offers as required by the project release policy.

SDL3's DOS backend may require or recommend a DPMI host. If `CWSDPMI.EXE` is distributed, include its license and source/distribution terms in the DOS package.

### DJGPP and runtime pieces

DJGPP toolchain and runtime components have their own licenses. Before distributing DOS binaries, document:

- DJGPP runtime obligations
- libgcc obligations, if statically linked
- any libc/runtime exception terms
- DPMI host terms, especially for `CWSDPMI.EXE`
- debugger or symbol files, if distributed

### DOS helper utilities

Do not bundle third-party helper utilities until their licenses are reviewed.

Helpers to document separately:

- `DOSLFN.COM` / DOSLFN
- FastVid
- MTRRLFBE
- UNIVBE / SciTech Display Doctor
- CuteMouse, if recommended for wheel support

The first DOS package can mention these tools as optional external recommendations instead of redistributing them.

### Optional future dependency licenses

If later milestones adopt ideas from Q2DOS or Hammer of Thyrion, check these licenses before integration:

- **Watt-32 / WatTCP networking:** commonly BSD-style, but verify the exact version used.
- **Tremor / Ogg Vorbis:** Xiph-style BSD license; include notices if used.
- **libcurl:** curl permissive license; include notices if used.
- **Mesa:** MIT/XFree86-style license for core Mesa, but old Mesa trees contain components with different notices. Review component-by-component before bundling.
- **3dfx Glide:** uses the 3dfx Glide Source Code General Public License, which is not the same as GPL. Review compatibility before linking or distributing with Silverspring.
- **Sage/MiniGL-style wrappers:** verify the exact source and license before use.
- **GameSpy SDK or browser code:** avoid unless the license is reviewed and distribution is clearly allowed.
- **Mpxplay/libau PCI audio code:** review before considering any PCI sound path outside SDL3.

### Original game data and trademarks

The DOS package must not include original Heretic II assets. This includes:

- PAK files
- maps
- models
- textures
- sounds
- music
- videos
- manuals or art unless separately licensed

Use Heretic II descriptively. Do not imply endorsement by Raven Software, Activision, Microsoft, or id Software.

## Architecture

### Monolithic executable

The DOS build should produce one protected-mode executable:

```txt
SILVER.EXE
```

It should statically link:

- common engine code
- client code
- server/single-player host code
- game logic
- player logic
- client effects logic
- DOS renderer
- SDL3 sound backend
- tools needed at runtime

The existing desktop architecture may continue to use dynamic modules where supported, but DOS should bind APIs directly.

Suggested build options:

```txt
SS_PLATFORM_DOS=ON
SS_MONOLITHIC=ON
SS_BUILD_GL3=OFF
SS_BUILD_GL1=OFF
SS_BUILD_REF_SOFT=ON
SS_ENABLE_OPENAL=OFF
SS_ENABLE_VIDEO=OFF initially
SS_ENABLE_HD_TEXTURES=OFF
SS_ENABLE_NETWORK=OFF initially
```

### Static API binding

Dynamic module loading should be abstracted behind a small binding layer.

Desktop path:

```txt
load game module from shared library
load renderer module from shared library
load sound module from shared library
```

DOS path:

```txt
bind game API directly
bind renderer API directly
bind sound API directly
bind client effects API directly
```

This avoids scattering `#ifdef DOS` through high-level game code.

## Renderer plan

The renderer is the largest part of the DOS port.

SDL3 DOS gives a framebuffer. It does not give OpenGL. The DOS target needs a software/VESA renderer.

### Recommended renderer path

Create a dedicated renderer target, for example:

```txt
src/ref_soft/
src/ref_dos/
```

The renderer should write to a software framebuffer and present through SDL3.

Start with 8-bit or 16-bit output depending on what is most practical for the existing art path and SDL3 DOS framebuffer modes. Preserve palette behavior where possible.

### Initial video modes

Prioritize 4:3 modes:

```txt
320x240
400x300
512x384
640x480
```

Stretch targets:

```txt
800x600
```

Widescreen is not a DOS goal. DOS should be visually classic and performance-conscious.

### Renderer milestones

#### Renderer milestone 1: 2D API

Implement enough rendering for:

- console
- loading screen
- menus
- fonts
- pics
- simple fills
- screenshots if feasible

This proves framebuffer presentation, palette/color conversion, and input loop timing.

#### Renderer milestone 2: BSP world

Implement world rendering:

- textured BSP surfaces
- visibility traversal
- depth ordering or z-buffer path
- lightmap support
- gamma/brightness handling suitable for DOS

Start with correctness before performance.

#### Renderer milestone 3: entities and effects

Implement:

- sprites
- particles
- alias/flex models
- skeletal/flex model paths used by Heretic II
- transparent surfaces
- weapon/player models where applicable

#### Renderer milestone 4: water and sky

Implement classic-feeling:

- water warp or stable approximation
- sky rendering
- no screen-space reflections
- no modern post-processing

#### Renderer milestone 5: performance pass

Optimize for real hardware:

- reduce overdraw
- fixed-point math where useful
- cache-friendly spans
- mode-specific framebuffers
- optional lower resolution modes
- configurable particle/detail limits

### 3Dfx/Glide later

A 3Dfx path would be period-correct and attractive, but it should not be the first DOS renderer.

Treat Glide as a later project after:

- VESA software renderer works.
- Input and sound are stable.
- real hardware testing exists.
- package and documentation are mature.

## Sound plan

Use the existing SDL3 software-mixer path where possible.

For DOS:

- Use SDL3 audio streams.
- Let SDL3 handle Sound Blaster output.
- Disable OpenAL.
- Disable HRTF.
- Disable EFX.
- Disable or reduce expensive audio effects.
- Start with 22050 Hz stereo or mono if needed.
- Add 44100 Hz as a configuration option if performance allows.

Music/video considerations:

- WAV and original game sounds should be first priority.
- OGG music may be too expensive or dependency-heavy for the first DOS target.
- CD audio or MIDI can be investigated later.
- HD videos are out of scope.

## Input plan

Use SDL3 DOS input:

- keyboard
- mouse
- optional joystick/gameport

Priorities:

- reliable keyboard mapping
- raw-feeling mouse input
- configurable mouse sensitivity
- no dependence on modern controller APIs
- no advanced gamepad assumptions in the first DOS build

The DOS build should document supported input devices and known limitations.

## Filesystem and data layout

Use a short, DOS-friendly layout.

Recommended install layout:

```txt
C:\SILVER\
  SILVER.EXE
  CWSDPMI.EXE
  README.DOS
  BASE\
    HTIC2-0.PAK
    HTIC2-1.PAK
    CONFIG.CFG
    SAVE\
```

Optional real-hardware helpers should be documented but not required in the base package:

```txt
DOSLFN.COM      long filename TSR, if loose files or mods need it
FASTVID.EXE     optional LFB write-combining helper on some systems
MTRRLFBE.EXE    optional LFB write-combining helper on some systems
UNIVBE/SDD      optional VESA compatibility layer for older cards
```

The DOS target should support original PAK data first.

Avoid initially:

- long loose-file paths
- HD texture replacement directories
- Steam/GOG path detection
- OS-specific application data folders
- MP4/MKV cinematics
- mandatory `base.pak` remaster assets

PAK-internal paths can remain as expected by the engine. Host filesystem behavior should remain DOS-friendly.

## Networking plan

Networking is out of scope for the first DOS port.

Initial target:

- single-player only
- no dedicated server package
- networking code compiled out or stubbed where needed

Later investigation:

- Watt-32 or another DOS TCP/IP stack
- UDP support
- LAN play
- protocol compatibility
- DOSBox IPX or NE2000 emulation only if useful

Do not let networking block the first playable DOS build.

## Toolchain plan

Use DJGPP with SDL3's DOS toolchain support.

Expected SDL3 build pattern:

```sh
cmake -S SDL -B build-sdl-dos \
  -DCMAKE_TOOLCHAIN_FILE=SDL/build-scripts/i586-pc-msdosdjgpp.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-sdl-dos --parallel
```

Expected Silverspring build pattern:

```sh
cmake -S . -B build-dos \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/SDL/build-scripts/i586-pc-msdosdjgpp.cmake \
  -DSS_PLATFORM_DOS=ON \
  -DSS_MONOLITHIC=ON \
  -DSS_BUILD_GL3=OFF \
  -DSS_BUILD_REF_SOFT=ON \
  -DSS_ENABLE_OPENAL=OFF \
  -DSS_ENABLE_NETWORK=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-dos --parallel
```

The exact flags should be refined after the first SDL3 DOS toolchain proof.

Initial DJGPP compiler flag policy:

```txt
-fno-strict-aliasing   likely needed for old Quake-family C code
-fomit-frame-pointer   likely useful for release builds on slow CPUs
-g                     useful for debug-symbol builds even on DOS
-ffloat-store          measure before use; may reduce prediction drift but costs performance
-march=pentium         optional low-end tuned build after baseline works
-march=pentium3        optional SSE/Pentium III build only as a separate executable
```

Do not enable SSE or other SIMD in the default DOS executable. SDL3's own DOS work disables SIMD to avoid real-hardware surprises.

## Milestones

### Milestone 0: SDL3 DOS proof

Goal: prove SDL3 DOS works in this development environment.

Tasks:

- Build SDL3 for DOS with DJGPP.
- Build a tiny SDL3 DOS test app.
- Present a framebuffer.
- Play a test tone or sample.
- Read keyboard and mouse input.
- Run in DOSBox Staging and DOSBox-X.
- Run on real hardware if available.

Deliverable:

```txt
HELLODOS.EXE
```

### Milestone 1: CMake DOS target

Goal: configure the Silverspring tree for DOS without building the full game.

Tasks:

- Add `SS_PLATFORM_DOS` option.
- Add `SS_MONOLITHIC` option.
- Disable unsupported features at configure time.
- Add DOS-specific compiler flags.
- Add a minimal DOS executable target.

Deliverable:

```txt
CMake configure succeeds for DOS.
```

### Milestone 2: Core compile

Goal: compile platform-neutral engine pieces.

Tasks:

- Build qcommon-style code.
- Build command/cvar/filesystem code.
- Build memory/hunk code.
- Build selected tools if practical.
- Fix DJGPP compiler errors.
- Fix 32-bit pointer and integer assumptions.

Useful early target:

```txt
MAKEPAK.EXE
```

Deliverable:

```txt
A DOS executable initializes the core and exits cleanly.
```

### Milestone 3: Monolithic engine shell

Goal: replace dynamic module loading with static API binding for DOS.

Tasks:

- Add direct game API binding.
- Add direct player API binding.
- Add direct client effects API binding.
- Add direct sound API binding.
- Add a null renderer binding.
- Enter the main loop without rendering a map.

Deliverable:

```txt
SILVER.EXE starts, mounts BASE, initializes subsystems, and reaches the main loop.
```

### Milestone 4: DOS 2D renderer

Goal: get console and menu rendering working.

Tasks:

- Add framebuffer abstraction.
- Present through SDL3 DOS video.
- Draw fills, pics, chars, and menu elements.
- Load palette and image data correctly.
- Implement mode switching for initial 4:3 modes.

Deliverable:

```txt
SILVER.EXE shows console/menu in DOS.
```

### Milestone 5: Map load with stubbed world rendering

Goal: reach map loading and frame loop.

Tasks:

- Load original PAK data.
- Load a map.
- Initialize client/server state.
- Replace missing renderer calls with safe stubs.
- Keep input and frame timing stable.

Deliverable:

```txt
A map loads without drawing the 3D world.
```

### Milestone 6: First playable software world

Goal: render enough world/entity content to move around.

Tasks:

- Draw BSP world surfaces.
- Add basic texture mapping.
- Add lightmaps or a temporary lighting approximation.
- Add player movement view rendering.
- Add basic entities.
- Add depth handling.

Deliverable:

```txt
A player can move around a map in DOSBox and on at least one real PC.
```

### Milestone 7: Sound and input integration

Goal: make the DOS build playable with audio and stable controls.

Tasks:

- Enable SDL3 audio stream output.
- Integrate software mixer.
- Test SB16-compatible output.
- Verify mouse and keyboard controls.
- Add DOS-specific cvars where needed.

Deliverable:

```txt
Playable DOS build with sound, keyboard, and mouse.
```

### Milestone 8: Real hardware pass

Goal: prove the port outside emulators.

Tasks:

- Test on at least one Pentium II/III-class machine.
- Test VESA linear framebuffer modes.
- Test SB16-compatible audio.
- Test PS/2 mouse behavior.
- Test file saves and config writes on FAT.
- Collect performance data by resolution.

Deliverable:

```txt
README.DOS includes verified hardware notes.
```

### Milestone 9: DOS package

Goal: produce a clean DOS package.

Package layout:

```txt
SILVERDOS.ZIP
  SILVER.EXE
  CWSDPMI.EXE
  README.DOS
  BASE\
    CLASSIC.CFG
```

The package must not include original game data.

Deliverable:

```txt
A documented DOS package that runs from a clean directory after original PAK files are copied in.
```

## Performance strategy

Performance should be measured on real hardware, not inferred from DOSBox.

Initial metrics:

- frame time at 320x240, 400x300, 512x384, and 640x480
- audio underruns
- map load time
- memory use after loading a map
- input latency feel
- save/load reliability

Potential performance controls:

```txt
r_mode
r_detail
r_particles
r_dynamic_lights
s_khz
s_mixahead
```

Avoid adding many controls before measurements show a need.

## Memory strategy

DOS memory constraints need early attention.

Tasks:

- Audit large static allocations.
- Audit assumptions about available virtual memory.
- Track hunk allocations during map load.
- Avoid loading HD assets in DOS.
- Avoid unnecessary video/audio backends.
- Keep the executable and runtime data small enough for 64 MB target systems.

DPMI makes protected-mode memory available, but real machines still have practical limits.

## Risk register

### Renderer scope

Risk: A software renderer for Heretic II may take substantial work.

Mitigation: Stage the renderer, starting with 2D, then BSP, then entities.

### Dynamic module assumptions

Risk: Engine code may assume DLL loading in many places.

Mitigation: Add a binding layer and use monolithic APIs on DOS.

### Filesystem path assumptions

Risk: long paths, case sensitivity, and loose replacement assets may not behave well on DOS.

Mitigation: Use a DOS-specific base layout and original PAK-first loading.

### Sound stability

Risk: SDL3 DOS audio may behave differently across emulators and real Sound Blaster hardware.

Mitigation: start with conservative sample rates and test real hardware early.

### Compiler assumptions

Risk: DJGPP may expose non-portable code paths, pointer casts, or unsupported APIs.

Mitigation: compile core code early and fix portability issues before renderer work expands.

### Performance

Risk: Heretic II content may be heavy for software rendering on late-1990s hardware.

Mitigation: start with low resolutions, measure on real machines, and add detail controls only where needed.

## Documentation deliverables

Add:

```txt
docs/dos.md
docs/dos-build.md
docs/dos-hardware.md
```

### docs/dos.md

Should explain:

- what the DOS build is
- what it supports
- what it does not support
- required original game data
- install layout
- common command-line options
- known limitations

### docs/dos-build.md

Should explain:

- DJGPP setup
- SDL3 DOS build
- Silverspring DOS build
- package creation
- emulator setup for testing

### docs/dos-hardware.md

Should document:

- tested CPUs
- tested video cards
- tested sound cards
- tested DOS versions
- tested mice/keyboards
- performance notes by resolution
- known hardware-specific issues

## Success criteria

The DOS port is successful when:

- `SILVER.EXE` builds with DJGPP.
- The executable runs under DOSBox and FreeDOS.
- The executable runs on at least one real Pentium II/III-class PC.
- Original PAK files load from a DOS-friendly `BASE` directory.
- Menus and console render through VESA.
- At least one map is playable with keyboard and mouse.
- Sound plays through SDL3's DOS audio path.
- The package contains no original game data.
- The DOS limitations are documented honestly.

## Long-term ideas

Only consider these after the VESA software DOS build is playable:

- 3Dfx/Glide renderer.
- CD audio support.
- MIDI support.
- Watt-32 networking.
- real-hardware installer helper.
- additional DOS video modes.
- lower-memory mode for 32 MB systems.
