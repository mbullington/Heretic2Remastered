# Heretic2 DOS Differential Security Review

## Executive Summary

| Severity | Count |
|---|---:|
| Critical | 0 |
| High | 0 |
| Medium | 0 |
| Low | 0 |

**Overall risk:** LOW for the repaired synthetic DOS proof boundary  
**Recommendation:** APPROVE for the documented synthetic production slices; gameplay remains **UNVERIFIED**.

**Key metrics:**

- 54 changed or untracked files inventoried; security review focused on untrusted format parsing, BSP relationships, network indexing, callback allocation, proof isolation, and build inputs.
- 5 malformed BSPs, 3 malformed WAVs, 1 malformed BK, and 1 malformed font fixture rejected through production validators.
- 12 required production/static-binding symbols present in the linked DOS artifact.
- No security-check removal or reintroduction found in the reviewed high-risk paths.

## What Changed

**Baseline:** `31128e429c84f7ea9bace8247a81c53bdddd6da7` plus intentional existing uncommitted DOS work. This is a working-tree review, not a commit-range review.

| Area | Risk | Result |
|---|---|---|
| WAV parser (`snd_wav.c`) | High | Offset/remaining-length parsing; format, alignment, data, and loop arithmetic checked |
| BSP/BK/font loaders (`dos/ref_soft`) | High | Byte ranges and cross-lump semantics checked before publication; numeric inputs bounded |
| DOS loopback (`dos/net_dos.c`) | High | Only client/server socket sources accepted; destination mapping explicit |
| SDL3 callback (`snd_sdl3.c`) | High | Fixed 4096-byte scratch buffer; frame-aligned chunk submission |
| Proof runners | Medium | Fresh hash-verified allowlisted guest per run; bounded foreground DOSBox |
| Build graph | Medium | Immutable Nixpkgs/archive inputs, one-job default, explicit source manifests |

The tracked diff is 651 additions and 316 deletions across 18 files. DOS implementation, runners, manifests, and documentation are new untracked files in the intentional working tree.

## Critical Findings

No unresolved Critical, High, Medium, or Low security findings were confirmed in the repaired scope.

### Adversarial checks performed

- **Malicious sound asset:** truncated RIFF, zero sample width, or oversized data declaration reaches `GetWavinfo`; each case returns invalid before unsafe reads, division, or cache access.
- **Malicious BSP:** an attacker-controlled PAK supplies `INT_MIN` node children, area 256, overflowing face ranges, undersized lightmaps, or visibility offsets into the header; `bsp_semantics_valid` rejects each before model pointers/images are published.
- **Invalid network source:** `NET_SendPacket(NS_GAMESPY, ...)` and `NET_GetPacket(NS_GAMESPY, ...)` return without indexing loopback arrays or changing counters.
- **Large SDL request:** callback demand larger than scratch capacity is split into bounded frame-aligned submissions; driver-controlled size no longer controls stack allocation.
- **Contaminated proof directory:** runners construct a fresh input inventory containing only the executable, CWSDPMI, and pinned synthetic PAK; any inventory mismatch fails before mounting.

Exploitability of the original parser/indexing defects was easy for a malicious local asset or direct invalid network call. Runtime fixtures now exercise the root-cause checks.

## Test Coverage Analysis

Permanent synthetic regression cases are worth maintaining because they catch the demonstrated OOB read, signed-overflow/index, cross-lump range, NaN conversion, and malformed-data regressions at stable production-loader boundaries.

Runtime evidence:

- Dedicated Qcommon lifecycle: PASS, 240 client frames and 121 server frame calls.
- Integrated renderer/mixer: PASS, active/spawned local handshake, one world face, two triangles, 86,246 depth writes.
- Mixer/Sound Blaster: PASS, 11,024 non-zero mixed frames and 22,048 non-zero submitted samples; no OpenAL symbols.
- HELLODOS and H2DOS diagnostic proofs: PASS.
- Desktop clean application build: PASS (`build/Heretic2R`, Mach-O arm64).

No legal original Heretic II data was available, so representative production game assets and gameplay were not tested.

## Blast Radius Analysis

| Function/boundary | Direct callers | Blast radius |
|---|---:|---|
| `GetWavinfo` | 2 | Low |
| `bsp_semantics_valid` | 1 | Low, but controls all DOS BSP publication |
| `font_metrics_valid` | 2 | Low |
| `SNDSDL3_AudioStreamCallback` | 1 SDL registration | Low call count, continuous runtime impact |
| `NET_SendPacket`/`NET_GetPacket` | engine networking API | Medium semantic impact, DOS loopback only |

The most consequential changes sit at narrow ingestion boundaries, allowing centralized validation rather than duplicated consumer guards.

## Historical Context

- Original WAV globals/parser entered in `52b07f8a` (`Implement GetWavinfo()`).
- SDL stack allocation entered in `bf308ac4` (`Implement SDL_SDL3Callback()`).
- No removed validation was traced to a security/CVE commit.
- q2dos architectural reference was fixed at `9f20dedb335d9719a57c233709cc595e06850935`; its `licenses/gpl.txt` is GPLv2. A normalized comparison found zero exact non-comment matches of at least 60 characters among 656 reviewed local DOS lines. No q2dos code was copied, so no new file-level provenance notice is required.

## Recommendations

### Immediate

- None blocking for the documented synthetic slice.

### Before any gameplay or release claim

- Implement and validate Raven flexible-model/skeleton rendering, sky and special-surface parity, model lighting/shadows, and effect-specific particles.
- Run legal user-supplied PAKs on representative real hardware as well as DOSBox.
- Retain `gameplay=UNVERIFIED` until those paths are directly exercised.

## Analysis Methodology

**Strategy:** Surgical review of a large codebase (2,591 C/C++ headers/sources), with full coverage of the changed high-risk ingestion and runtime boundaries.

Techniques included baseline diffs/history, caller searches, integer/pointer adversarial analysis, deterministic malformed fixtures, clean serial DOS compilation, repeated artifact-hash comparison, all five bounded DOSBox runs, guest log and PPM validation, static symbol/OpenAL checks, immutable dependency verification, source-manifest checking, desktop regression build, PAK inventory, and process cleanup.

**Limitations:** No proprietary data, physical DOS hardware, fuzzing campaign, or exhaustive review of unchanged desktop renderer/game code. Confidence is HIGH for the reported synthetic DOS boundary and LOW for gameplay, which is not claimed.

## Appendix: Evidence

- `.dos-build/SILVER-RUN-EVIDENCE.TXT`
- `.dos-build/SOFTWARE-RENDERER-EVIDENCE.TXT`
- `.dos-build/DOS-AUDIO-EVIDENCE.TXT`
- Fixture SHA-256: `5391b16e4e79573f5f3bee4edf27c0d8e6dfdc834001dea3b35d7e85b9b21957`
- DOSBox Staging: 0.82.2 (`f8c24`), 64 MiB guest memory
- Nixpkgs: `f2b17eb1b90f6a359082035b4538d4c63017aec3`
