# Project release baseline

Inspected 2026-11-04 at `8fb46c67b57489f8a2b5b970ba31e2bc2a90457d`.
Recheck these facts against the selected release commit and live GitHub state.

- `.github/` contains no tracked workflow, and `gh workflow list --repo
  alondero/kirbysdreamcourse-recomp` returned none. The release workflow named
  in a comment in `rom_identity.txt` is not an implemented release path.
- GitHub has published prereleases `v0.1.0-experimental` and
  `v0.1.1-experimental`. The latter is titled `v0.1.1 experimental source`
  and has one asset, `kirbysdreamcourse-recomp-source-v0.1.1.zip`. Its body
  explains that Git, Python, CMake, Ninja and MinGW are needed to generate and
  build locally. This is precedent for source packaging, not a binary release.
  The requested distribution target for this skill is now explicitly an EXE
  ZIP. The source precedent must not select a different default deliverable.
- `tools/setup.ps1` verifies the ROM and prepares a guarded local build through
  `tools/prepare_guarded.ps1`. `CMakeLists.txt` calls
  `snesrecomp_target_generated_code` and hardcodes `SNES_GAME_VERSION="dev"`.
  Neither establishes distributable binary packaging or release provenance.
- The framework pin is `147b55e252dcb3e7680a99c7c2f50e7183891844`; recomp-ui
  is `3f1d6f122a58823614abce14f5b86f4d165a629c`. Check the target's pins and
  patches instead of updating dependencies as part of an ordinary release.
- `README.md`, `AGENTS.md` and `docs/compatibility.md` exclude generated game
  output from distribution. Requiring a ROM at runtime does not change the
  provenance of generated code already linked into an executable.
- The supported ROM is the headerless 1,048,576-byte USA image, SHA-256
  `0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.
  Treat `rom_identity.txt` at the target as the identity source.
- Current compatibility evidence covers recorded first/second single-player
  courses and one two-player hole, with known state and audio differences.
  The first full-WRAM boot mismatch is host frame 15 versus reference frame 16.
  Snes9x 1.63 is pinned in `docs/compatibility.md`; it is not a hardware oracle.

## What to carry over from Lamborghini and AeroGauge

Keep structured notes, exact commit selection, preparation before tagging,
explicit release flags, remote verification and checking every expected asset.
Keep UTF-8 notes in a file to avoid Windows shell quoting/encoding problems.

Do not copy their `Build & Release` dispatch, platform list, required merge
parent, five-minute build estimates, stable flags or ROM-secret handling.
Kirby currently supports a different local Windows build path and forbids
uploading game-derived inputs/outputs. A prerelease should use the final tag
and an explicitly checked URL; a temporary draft URL is not itself proof of
a tag-binding defect. Diagnose actual metadata instead of deleting releases.

## Notes precedent

Use the existing source releases for package-type precedent. Their brief body
is not a reason to omit player-facing changes, coverage or build provenance.
The release-note template adds those sections without claiming new binary or
platform support. Do not copy the N64 template's claim that a recompiled
executable contains no original game code: audit the actual build inputs.

## Windows staging contract

`windows-package` packages an already built and reviewed staging directory;
it does not compile or certify the EXE's distributability. The build review
must first establish that excluded game-derived output is not linked into it.
It accepts the main x64 PE EXE and root DLLs, licensed files under `assets/`
and `licenses/`, root notice files, `README.txt` and `build-info.json`.
Keep personal configuration and game data out of staging. Inspect all assets
for provenance; a file living under `assets/` does not establish permission.

The required `build-info.json` fields are:

```json
{
  "source_commit": "<full target SHA>",
  "version": "<release tag>",
  "build_command": "<exact verified Release build command>",
  "toolchain": "<compiler and version>",
  "framework_revision": "<exact pinned revision>",
  "ui_revision": "<exact pinned revision>"
}
```

This metadata records provenance claims for review; it cannot prove them.
Test the packaged EXE and dependencies before publishing. Current timing
defaults live in `tools/play_experimental.ps1`; a direct EXE release must
establish those defaults without requiring that developer script. The build
also needs a real release version in place of the hardcoded `dev` string.

## Explicit source-release requests only

For a user who specifically requests source distribution:

```powershell
python .agents/skills/release/scripts/release.py source-package --target <full-sha> --version <version>
```

This creates a tracked-source ZIP, rejects known private/generated paths and
missing notices, and prints the SHA-256. Review the inventory and test setup
from a freshly extracted copy. Explain the required compiler/toolchain and
local generation steps in source-release notes. Publishing source is never
a fallback completion of an EXE release request.
