# Silverspring modernization plan

Date: 2026-05-17

## Summary

Silverspring is the proposed public identity for this Heretic II source port. The goal is to move the project away from an "HD remaster" posture and toward a high-quality, preservation-first source port in the style of Hammer of Thyrion, dhewm3, and the ioquake ports.

The default experience should be **Classic+**: original art direction, modern platform support, stable rendering, good input, widescreen support, and restrained visual cleanup. Optional enhancements may exist, but they should be curated and tasteful. Features that make the port feel like a shader showcase or texture-pack bundle should be removed rather than hidden behind an experimental preset.

CI is intentionally out of scope for this plan. Local build and check scripts are preferred.

## Goals

- Rename the public project identity to **Silverspring**.
- Make Classic+ the default launch experience.
- Keep tasteful rendering improvements such as conservative bloom, SSAO, and sky work.
- Remove low-quality visual features instead of preserving them as "Experimental" options.
- Clean the repository so a fresh checkout looks like a source port, not a local build directory.
- Make the build system boring and reproducible on supported desktop platforms.
- Document data ownership, build instructions, compatibility limits, and release process.
- Prepare the codebase for future platform work, including a DOS/DJGPP target.

## Non-goals

- No CI setup.
- No bundled original Heretic II assets.
- No bundled low-quality HD texture pack.
- No "Experimental" visual preset.
- No attempt to make every modern renderer feature part of the project identity.
- No gameplay redesign unless a fix is documented as an intentional compatibility or bug-fix change.

## Project identity

### Public name

Use **Silverspring** as the public project name.

Recommended README opening:

```md
# Silverspring

Silverspring is a Classic+ source port of Heretic II focused on preservation, portability, and tasteful modern usability.
```

Use "Heretic II" descriptively. Avoid making "Heretic II Remastered" the product identity.

### Language to prefer

- source port
- Classic+
- preservation-first
- modern systems
- original game data
- optional enhanced settings
- restrained visual cleanup

### Language to remove or reduce

- remastered
- HD remaster
- best way to play
- shader showcase phrasing
- long feature-list marketing copy
- language that implies HD textures are the core project

## Visual modes

Silverspring should have two supported visual modes.

### Classic+

Classic+ is the default. It should preserve the original art direction while improving the runtime experience.

Classic+ should include:

- GL3 renderer on modern platforms.
- Widescreen and HiDPI support.
- HUD and menu scaling that respects the original layout.
- 64-bit desktop builds.
- SDL3 input and controller support.
- Framerate decoupling and interpolation where stable.
- OGG/music/video backend improvements where available.
- Automatic original data detection where it does not compromise portability.
- Conservative bloom.
- Conservative SSAO.
- Tasteful sky improvements.
- Original-ish menus and loading presentation.
- Original texture art by default.

Classic+ should not include:

- HD texture replacement by default.
- Screen-space or water reflections.
- Aurora/starfield menu or loading backgrounds.
- Flashy menu transitions.
- Aggressive post-processing.

### Enhanced

Enhanced is opt-in and curated. It should still feel like Heretic II.

Enhanced may include:

- Slightly stronger bloom and SSAO.
- Dynamic shadows if stable and visually restrained.
- Improved sky work.
- Optional HD videos or music where legally and artistically appropriate.
- User-supplied texture replacement support.

Enhanced should not include:

- Sloppy reflections.
- Bundled bad HD texture packs.
- Modern UI skins that conflict with the original presentation.
- A broad "turn every graphics feature on" policy.

### Experimental

Do not keep an Experimental preset. If a feature is known to be low quality, delete it.

## Feature decisions

### Keep

- SDL3 platform/input layer.
- Gamepad support.
- Widescreen and HiDPI support.
- Framerate decoupling where stable.
- GL3 renderer for modern platforms.
- Conservative bloom.
- Conservative SSAO.
- Tasteful sky improvements.
- OGG music support.
- Improved video backend support on modern platforms.
- Automatic original game data detection where platform-appropriate.
- PAK loading improvements that preserve original data compatibility.

### Remove or rewrite

- Reflections, unless replaced later by a much better implementation.
- Aurora menu/loading effects.
- Starfield menu/loading effects if they make the UI feel like a fan skin rather than a source port.
- Flashy menu animation that is not needed for readability or feedback.
- Hype-oriented README sections.
- Bundled or assumed HD texture content.
- Dead cvars for deleted features.

### Keep as opt-in support only

- HD texture replacement paths.
- Enhanced renderer settings.
- User-supplied media replacement.

## Default cvar direction

Initial Classic+ defaults should move toward:

```txt
r_bloom 1
r_bloom_strength 0.20
r_ssao 1
r_ssao_strength 0.75
r_shadows 0 or 1 after stability review
r_hd_textures 0
r_antialiasing 0 or 1 after visual review
```

Reflections should be removed. If removal is staged, default them off immediately:

```txt
r_reflections 0
```

Enhanced can use slightly stronger values:

```txt
r_bloom 1
r_bloom_strength 0.30
r_ssao 1
r_ssao_strength 1.0
r_shadows 1
r_hd_textures 0
```

HD texture replacement should remain user-controlled, not preset-driven, unless a coherent replacement pack exists and is reviewed separately.

## Repository hygiene

A major quality milestone is removing tracked build products and generated files.

Remove from source control:

- `build/`
- `cmake-build-*` directories
- `.obj`
- `.log`
- `.tlog`
- `.recipe`
- `.pdb`
- `.exp`
- generated `.lib` outputs
- local `x64/Release` outputs
- `debug_x64` and `release_x64` outputs
- generated Visual Studio build state
- local release zips and package outputs

Keep only intentional source-controlled content:

- source code
- CMake files
- intentionally maintained project files
- dependency source or documented third-party binaries
- docs
- packaging scripts
- config templates
- legal notices

Update `.gitignore` so these files do not return.

## Build system

CMake should be the authoritative build path.

Expected local desktop build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Optional local validation command:

```sh
scripts/check
```

Build goals:

- Desktop Windows, Linux, and macOS should have documented build paths.
- Generated project files should not be committed unless intentionally maintained.
- Third-party dependencies should have a documented policy.
- Debug and Release should both build.
- Platform-specific features should be isolated behind CMake options.

Suggested CMake options:

```txt
SS_BUILD_GL3=ON
SS_BUILD_GL1=ON or OFF after review
SS_BUILD_TOOLS=ON
SS_ENABLE_OPENAL=ON
SS_ENABLE_VIDEO=ON
SS_ENABLE_HD_TEXTURES=ON
SS_MONOLITHIC=OFF
SS_PLATFORM_DOS=OFF
```

These options prepare the project for the later DOS plan without forcing DOS work into the modern desktop path.

## Local checks instead of CI

No CI will be added. Use local scripts instead.

Suggested `scripts/check` behavior:

- configure a clean build directory
- build the default target
- build tools
- optionally run lightweight unit or smoke tests when they exist
- verify no generated artifacts are tracked
- print the commands needed to reproduce failures

This keeps the project maintainable without introducing hosted CI.

## Documentation plan

Create or rewrite these docs:

```txt
README.md
docs/build.md
docs/data-files.md
docs/configuration.md
docs/renderer.md
docs/compatibility.md
docs/release.md
docs/development.md
```

### README.md

The README should answer:

- What is Silverspring?
- What are the project goals?
- What original game data is required?
- How do I build it?
- How do I run it?
- What are Classic+ and Enhanced?
- What platforms are supported?
- What are the known limitations?
- What is the upstream lineage?

### docs/data-files.md

Document:

- required original files
- supported editions or install sources
- manual install layout
- auto-detection behavior
- what files the project may redistribute
- what files it must not redistribute
- how user replacement assets are loaded

### docs/compatibility.md

Document:

- save compatibility
- 32-bit save limitations
- demo compatibility
- multiplayer compatibility
- mod compatibility
- renderer differences
- known gameplay behavior differences

### docs/renderer.md

Document:

- Classic+ defaults
- Enhanced defaults
- renderer cvars
- removed visual features and why
- HD texture replacement policy
- platform-specific renderer notes

### docs/release.md

Document:

- how to build release packages
- what files ship
- what files must not ship
- versioning policy
- changelog format
- local release smoke test steps
- package verification steps

## Compatibility policy

Adopt this rule:

> Gameplay-affecting behavior should match original Heretic II unless a fix is deliberate, documented, and reviewed as a compatibility or bug-fix change.

Classify changes as:

- platform support
- renderer-only improvement
- input improvement
- data loading improvement
- bug fix
- compatibility break
- gameplay behavior change
- visual enhancement

This helps prevent accidental remaster drift.

## First-run and data UX

Missing data should produce a useful error.

The message should include:

- that original Heretic II data is required
- the required PAK filenames
- directories that were searched
- how to set the base directory
- where to copy files manually

Avoid cryptic fatal errors when the expected data is absent.

## Release discipline

Release packages should be boring and predictable.

They should include:

- executable(s)
- required project runtime libraries
- project config templates
- docs
- license files

They should not include:

- original Heretic II game data
- low-quality HD texture packs
- build logs
- compiler intermediates
- debug symbols unless packaged separately
- local developer files

## Implementation phases

### Phase 1: Identity and defaults

- Rename public docs to Silverspring.
- Introduce Classic+ and Enhanced terminology.
- Make Classic+ the default.
- Default HD texture replacement off.
- Default reflections off if deletion is not done yet.
- Rewrite README tone.

### Phase 2: Remove low-quality features

- Delete reflection rendering path.
- Delete reflection cvars and docs.
- Remove aurora/starfield menu and loading effects.
- Remove or simplify flashy menu transitions.
- Remove dead cvars.
- Build and smoke test.

### Phase 3: Repository cleanup

- Update `.gitignore`.
- Remove tracked build products.
- Remove generated logs and intermediates.
- Decide which Visual Studio files are intentionally kept.
- Verify a clean checkout builds from source.

### Phase 4: Documentation

- Add build docs.
- Add data file docs.
- Add renderer docs.
- Add compatibility docs.
- Add release docs.

### Phase 5: Build and platform preparation

- Add CMake options for feature gating.
- Add local `scripts/check`.
- Add monolithic build option for future DOS work.
- Separate dynamic module loading from API binding.
- Make platform-specific dependencies explicit.

### Phase 6: Polish

- Improve missing-data UX.
- Tune Classic+ renderer defaults.
- Review controller defaults and docs.
- Review release package contents.
- Begin warning cleanup, starting with 64-bit and unsafe string/format warnings.

## Success criteria

Silverspring is on the right path when:

- A fresh checkout contains source and docs, not build products.
- The README reads like a source port, not a remaster advertisement.
- Classic+ is the default.
- Reflections and low-quality UI effects are gone.
- Original textures are used by default.
- CMake builds the project from a clean tree.
- Missing game data produces actionable instructions.
- Compatibility limits are documented.
- Release packages are reproducible using local instructions.
