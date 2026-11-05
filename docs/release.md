# Binary releases

The release deliverable is `kirbysdreamcourse-recomp-windows-x64.zip` containing
`KirbyDreamCourseRecomp.exe`, launcher resources, required DLLs and notices.
Players provide the supported ROM beside the EXE. They do not need a compiler,
Python or code generation.

Linux releases add `kirbysdreamcourse-recomp-linux-x64.zip`, containing the
native x86-64 executable, `start.sh`, the same launcher resources and notices.
Build prerequisites, system-library requirements and Steam Deck verification
are recorded in [Linux support](linux.md). Both platforms use `tools/setup.py`
for the guarded patch stack; Windows retains its PowerShell entry points.

## Private build inputs

Follow the companion-repository pattern used by the Lamborghini and AeroGauge
`Build & Release` workflows:

1. Keep `Kirby's Dream Course (USA).sfc` at the root of a **private** companion
   repository. Use the headerless 1,048,576-byte image whose SHA-256 is recorded
   in `rom_identity.txt`; the build checks it before generation.
2. Set the Kirby repository Actions variable `ROM_ASSETS_REPO` to that private
   repository's `owner/name`.
3. Create a fine-grained token with read-only Contents access to that one
   private repository. Store it as the Kirby Actions secret `ROM_ASSETS_PAT`.
   The normal `GITHUB_TOKEN` cannot read a separate private repository.

For example, `gh variable set ROM_ASSETS_REPO --repo
alondero/kirbysdreamcourse-recomp --body OWNER/PRIVATE-REPO` sets the variable.
`gh secret set ROM_ASSETS_PAT --repo alondero/kirbysdreamcourse-recomp` accepts
the token interactively. Never put a token value in a shell command, source
file or log. No token from another project's Actions secrets can be read back
and copied by the release skill.

The private companion `alondero/kirbysdreamcourse-recomp-assets` and both Actions
settings were configured on 2026-11-04. Its supported ROM was hash-verified
before upload and the uploaded Git blob checked against the local input.
Fork maintainers must configure their own private repository and credentials.
The build workflow uses existing private inputs; it does not create the
companion repository, transfer an input there or provision credentials.

The workflow persists no checkout credentials. It uses private inputs for
generation/compilation, then removes the private checkout and generated game
source. Only the explicitly named binary ZIP is uploaded. The EXE includes
compiled translated routines; it still reads the user's ROM for assets and
fallback. Raw ROMs, generated source files, extracted game assets, saves and
recordings are excluded from public commits, caches, artifacts and releases.

## Build and test

Use a clean checkout. In PowerShell, with native Windows CMake and MinGW GCC:

```powershell
./tools/build_release.ps1 -Version v0.1.2-experimental -Rom 'C:/private/Kirby.sfc' -CMake 'C:/path/to/native/cmake.exe'
python .agents/skills/release/scripts/release.py smoke-windows --archive local/releases/v0.1.2-experimental/kirbysdreamcourse-recomp-windows-x64.zip --rom 'C:/private/Kirby.sfc'
```

The shared build calls ordinary setup with release version/defaults enabled.
It uses the same framework pin, patches, generated-code configuration and
interpreter fallback as development. Fresh `local/guarded*` paths are required;
existing outputs are preserved rather than silently overwritten. An optional
`-SdlSource` supports an already verified SDL 3.4.10 source tree.

The framework fetches SDL 3.4.10 with its pinned SHA-256 and links SDL and the
MinGW runtime statically by default. Staging walks the PE import closure and
copies required non-system DLLs from the build/compiler directories, failing
if a dependency cannot be found. It copies the selected compiler's GCC and
MinGW runtime notices, project/framework licenses and the UI notices/resources
staged by CMake. `build-info.json` records commit, version, dependency pins,
compiler, build command and imports; packaging prints the ZIP SHA-256.

Release builds set guarded timing/native-dispatch defaults in the EXE itself,
matching `tools/play_experimental.ps1`. Explicit environment settings still
win, including `SNESRECOMP_LLE_BOUNCE=0` for interpreter fallback. The default
development build is unchanged.

The smoke command hashes the private ROM, extracts the actual ZIP into fresh
local directories, removes developer tools from PATH, runs 400 no-input frames
in both native/fallback modes, compares direct defaults with explicit
developer settings and reports the first mismatch. It also checks wrong-ROM
rejection before guest execution. Captures and temporary inputs remain local.
This is not a fresh Windows installation, interactive launcher/controller
qualification, audio comparison or full-game reference replay. Keep those
limits and the existing first known reference divergence in release notes.

## Workflow contract

`.github/workflows/build-release.yml` is manual only. It must be available on
the default branch before its first normal dispatch.

```powershell
gh workflow run build-release.yml --repo alondero/kirbysdreamcourse-recomp --ref <candidate-ref> -f publish=false
```

A dry run builds the dispatched commit with a `v0.0.0-dryrun-<sha>` version,
uploads the tested Windows and Linux ZIPs and skips publication. It uses private inputs and
CI minutes. Editing the release skill alone does not authorize dispatch.

After a tested commit is tagged and the tag pushed:

```powershell
gh workflow run build-release.yml --repo alondero/kirbysdreamcourse-recomp --ref main -f tag=v0.1.2-experimental -f publish=true -f prerelease=true
```

Publication builds the requested tag, rather than whichever main commit is
current when a runner starts. Both build jobs have read-only public-repo access;
only the release job receives `contents: write`. A build or smoke-check failure
prevents publication. The release job requires both platforms to agree on the
source commit/version, publishes both tested ZIPs with initial notes and verifies
the remote tag commit, exact asset inventory and each downloaded SHA-256.
Use the release skill to replace the initial notes with the complete reviewed
body. Never overwrite conflicting tags/assets while resuming a failed run.

## Pins and verification status

- SNESRecomp: `147b55e252dcb3e7680a99c7c2f50e7183891844`.
- recomp-ui: `3f1d6f122a58823614abce14f5b86f4d165a629c`.
- CI CMake package: `4.3.1`; Ninja package: `1.13.0`. MinGW is installed by
  Chocolatey; its resolved compiler version and runtime notices are recorded
  in the package. The compiler package itself is not yet pinned in CI.
- Actions checkout: `11d5960a326750d5838078e36cf38b85af677262`;
  upload-artifact: `ea165f8d65b6e75b540449e92b4886f43607fa02`;
  download-artifact: `d3f86a106a0bac45b974a628896c90dbdf5c8093`.
  Their v4 refs and MIT licenses were inspected at these exact revisions.

Local and GitHub-hosted build/smoke results are recorded below. Publication is
recorded in the section after the dry-run evidence.

## Local verification, 2026-11-04

The shared `tools/build_release.ps1` path generated and compiled the actual
Release EXE at source commit `a0f752b0ba174eb30b2d31831112537ad5a65e79`, using
GCC 15.2.0, native CMake 4.3.1, Ninja 1.13.2 and fetched SDL 3.4.10. Staging
initially exposed an incorrect cache-field assumption; using the project's
actual `RECOMP_UI_ROOT` field fixed it. Staging and ZIP packaging then passed
on that compiled EXE. The complete shared command was subsequently verified
by the GitHub-hosted dry run below.

The local validation version is `v0.0.0-private-input-validation`, with ZIP
SHA-256 `6430ffb6229374ade556fa0ce8e8a524439bf40a6cb38db3e5dfbd1f6a4aa892`.
Its import inventory contains only Windows system DLLs. The ZIP contains the
EXE, 11 launcher resources, provenance/readme and project/UI/toolchain notices;
no ROM, generated source, saves or recordings are included.

The extracted ZIP completed 400 no-input frames in native mode and 400 in
fallback mode with developer tools removed from PATH. Each mode's direct
defaults matched the CPU logs produced with explicit developer settings over
all 400 frames; no divergence occurred in those comparisons. A wrong-ROM
input was rejected before guest execution. These tests did not rerun the
pinned reference comparison or qualify interactive UI, physical controllers,
audible output, fresh-process saves or full-game paths. The existing reference
divergence in `docs/compatibility.md` remains open.

The helper tests, PowerShell parsing, skill validation and actionlint 1.7.12
workflow validation also passed. Actionlint was downloaded from its official
release and checked against the published SHA-256 list. The pinned CI
CMake/Ninja Windows wheels were confirmed available on PyPI.

## GitHub-hosted verification, 2026-11-04

[Dry run 41882419218](https://github.com/alondero/kirbysdreamcourse-recomp/actions/runs/41882419218)
passed at source commit `85c76d5d72797338061bca249b1c6c279cf712c4`. It fetched
the private input, checked the ROM hash, generated and compiled with the
shared build command, ran tests, removed private inputs/generated source and
uploaded exactly one binary ZIP artifact. The release job was skipped.

The GitHub runner used MinGW GCC 16.2.0 and CMake 4.3.1. All 46 tests completed:
40 passed and 6 opt-in checks were skipped. Native and fallback packaged-EXE
checks each ran 400 no-input frames with a clean PATH, matching explicit
developer settings throughout. A correctly sized ROM with the wrong SHA-256
was rejected before guest execution. The first CI attempt exposed Windows
8.3 path aliases in TEMP; staging now resolves its root before checking child
paths, and tests cover aliases, relative roots and rejection of internal links.

The downloaded ZIP passed integrity, notice/resource inventory, build metadata
and SHA-256 verification against the checksum printed by the successful CI
build. It is 5,754,748 bytes with 38 files, SHA-256
`67c3486def88defdc0eb7199dd9d8151d8bb66579c59edf892864e9986a04fb6`.
This dry run does not qualify the publication job or the interactive/full-game
paths excluded from the local validation above.

## Publication, 2026-11-05

`v0.1.0` was published from tag `v0.1.0`, peeled commit
`ff0649da58643fc8ef0d3a0a30d4b31e433492d4`, by
[run 41937752098](https://github.com/alondero/kirbysdreamcourse-recomp/actions/runs/41937752098).
It is a full release rather than a prerelease, so GitHub's `/releases/latest`
now resolves to it. The asset is
`kirbysdreamcourse-recomp-windows-x64.zip`, 5,755,912 bytes, SHA-256
`c2e75047bfca352b0e0763b653a6ab06695afcfa020cb2148ff7d53a0555bde0`.
The published body was replaced with the reviewed notes and the title was set
to `Kirby's Dream Course Recompiled v0.1.0 (Windows x64)`.

The first attempt at this publication failed in the release job before any
release object existed. `$flags = if (...) { @('--latest') } else { ... }`
assigned the flags through a pipeline, which unrolls a single-element array
into a string, so `@flags` splatted `--latest` one character at a time and
PowerShell stopped with `no matches found for '-'`. The two-flag prerelease
branch stayed an array, so only the latest-release path was affected, and
every earlier run had used `publish=false`. The workflow now assigns the
array directly, and `tests/test_release_skill.py` runs the workflow's own
flag selection to check the arguments `gh` receives.

Verification after publication re-checked the remote tag's peeled commit,
tag-bound URL, title, published and non-prerelease state, exact asset
inventory, uploaded size, ZIP integrity and the downloaded SHA-256 against the
tested archive. The interactive launcher, physical controllers, audible output
and full-game reference replay remain unqualified, and the reference
divergence in `docs/compatibility.md` is still open.
