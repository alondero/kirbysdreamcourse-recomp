# Project release baseline

Linux support was added on 2026-11-05. The current workflow builds Windows and
Linux x64 ZIPs, requires both jobs before publication and verifies both assets.
Use [Linux support](../../../../docs/linux.md) for commands and platform limits;
the historical Windows baseline and evidence below remain useful provenance.

Inspected 2026-11-04 at `8fb46c67b57489f8a2b5b970ba31e2bc2a90457d`.
Recheck these facts against the selected release commit and live GitHub state.

- The initial baseline had no workflow. This change adds
  `.github/workflows/build-release.yml`: Windows EXE builds with private ROM
  inputs, dry-run support and explicit tag-based publication. Actions settings
  must be configured before the first dispatch; see `docs/release.md`.
- GitHub has published prereleases `v0.1.0-experimental` and
  `v0.1.1-experimental`. The latter is titled `v0.1.1 experimental source`
  and has one asset, `kirbysdreamcourse-recomp-source-v0.1.1.zip`. Its body
  explains that Git, Python, CMake, Ninja and MinGW are needed to generate and
  build locally. This is precedent for source packaging, not a binary release.
  The requested distribution target for this skill is now explicitly an EXE
  ZIP. The source precedent must not select a different default deliverable.
- `tools/build_release.ps1` uses `tools/setup.ps1` and the same guarded
  generation/build stack as development. It sets `KDC_BUILD_VERSION` and
  `KDC_RELEASE_DEFAULTS`, then stages and packages the compiled EXE with notices
  and import-checked runtime DLLs. Development defaults remain unchanged.
- The framework pin is `147b55e252dcb3e7680a99c7c2f50e7183891844`; recomp-ui
  is `3f1d6f122a58823614abce14f5b86f4d165a629c`. Check the target's pins and
  patches instead of updating dependencies as part of an ordinary release.
- Per the user's clarified distribution requirement, private ROM checkout and
  generated source are build inputs; the compiled EXE is a release artifact.
  Updated project instructions retain the exclusion of ROMs, extracted game
  assets, generated source, saves and recordings from public commits/artifacts.
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

Carry over their `ROM_ASSETS_REPO` variable and read-only `ROM_ASSETS_PAT`
secret pattern. The private companion repository contains the matching ROM;
CI generates and compiles it, removes private inputs and generated source,
and uploads only the binary ZIP. Do not copy the N64 platform list, required
merge parent, build estimates or stable-release flags. Kirby uses its own
guarded Windows build. A prerelease should use the final tag
and an explicitly checked URL; a temporary draft URL is not itself proof of
a tag-binding defect. Diagnose actual metadata instead of deleting releases.

## Notes precedent

Use previous source-release tags as a change-list base where appropriate.
Current notes must describe the EXE download and player-supplied runtime ROM,
with observed coverage and build provenance. Keep source-build instructions
out of the binary setup path. Do not copy the N64 template's claim that a
recompiled executable contains no original game code: describe the actual
compiled artifact and exclude raw ROMs/generated source files from the ZIP.

## Windows staging contract

`tools/build_release.ps1` generates and compiles the EXE. `stage-windows`
collects the binary, resources, dependency notices and DLL import closure.
`windows-package` ZIPs that reviewed stage; it does not itself compile.
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

Staging also records the actual DLL import inventory. Test the extracted EXE
with `smoke-windows` before publishing. The release EXE applies the same four
runtime defaults as `tools/play_experimental.ps1` when unset, and respects
explicit overrides such as `SNESRECOMP_LLE_BOUNCE=0`. Development builds keep
their existing defaults. The real version replaces `dev` in release builds.

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
