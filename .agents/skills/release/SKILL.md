---
name: release
description: Build, package, publish and verify Windows and Linux binary releases of Kirby's Dream Course Recomp.
---

# Kirby's Dream Course release

Release `alondero/kirbysdreamcourse-recomp` as **ready-to-run Windows and Linux x64 ZIPs**
containing the native executable, its runtime dependencies, launcher
resources and notices. The player supplies the ROM and launches the game
without Python, CMake, Ninja, a compiler or a generation step. This is the
default meaning of release/ship/distribution for this skill.

Follow the Lamborghini/AeroGauge pattern: identify shipped changes, build
and test the package, prepare notes, tag the exact commit, publish, then
verify the download. Work from the requested checkout.
A request to create or edit this skill does not authorize a release.

Run the commands below from the repository root. The helper uses Python 3.11+,
Git and authenticated `gh`; it never pushes, dispatches CI or changes releases.

## 1. Select the release and inspect readiness

Read `AGENTS.md`, `architecture.md`, `README.md`, `docs/compatibility.md`,
`rom_identity.txt` and `THIRD_PARTY_ATTRIBUTION.md` at the target commit.
Inspect the working tree, origin, published releases, tags and real workflows:

```powershell
git status --short
git remote -v
git fetch origin --tags
gh release list --repo alondero/kirbysdreamcourse-recomp
gh workflow list --repo alondero/kirbysdreamcourse-recomp
git rev-parse 'origin/main^{commit}'
```

Use the requested commit, or the fetched `origin/main` commit if none is given.
Pin its full SHA for every later step. Do not silently release uncommitted
work or select the last merge commit: this project also uses squash merges.
Use a requested version such as `v0.1.2-experimental`; otherwise propose the
next version from reachable published tags. Resolve an ambiguous version
while continuing preparation. Never move an existing tag.

Read [references/project-release.md](references/project-release.md) for current
packaging evidence and the differences from the N64 release workflows.

Use the private build-input pattern established by Lamborghini and AeroGauge:
`vars.ROM_ASSETS_REPO` names a private companion repository and
`secrets.ROM_ASSETS_PAT` grants read-only Contents access to that repository.
It holds `Kirby's Dream Course (USA).sfc` at its root. The workflow fetches it,
verifies the exact hash, generates C and compiles the guarded native EXE on
the runner. Generated source stays private; the compiled EXE is the intended
public artifact. Players still supply their own ROM for runtime assets and
interpreter fallback. Do not replace this with an interpreter-only build.

Check the configured variable and secret names without reading/logging token
values. Read [docs/release.md](../../../docs/release.md) for the one-time
setup, workflow contract and tested local commands. Missing private-repo
configuration is a setup gap to report; it is not a reason to prohibit a
recompiled EXE or substitute a source package.

The existing source releases are historical precedent only. A source package
requires an explicit source-release request; it never fulfills this skill's
default distribution goal. Its optional command is documented in the project
reference, separately from the EXE workflow.

ROMs, extracted game assets, generated source, saves and recordings stay in
local/private build storage. Do not upload them as public build artifacts. Review
the pinned framework, UI, SDL and transitive notices before packaging; root
licenses do not replace dependency terms. Keep the interpreter fallback.

## 2. Collect changes and prepare notes

```powershell
python .agents/skills/release/scripts/release.py changes --target <full-sha> --base <previous-tag>
```

Omit `--base` for the first release; the helper includes the root commit.
Choose a previous published tag reachable from the target. The helper reports
the exact commit range and merged PRs whose merge SHA is in that range, with
paginated GitHub results. It does not treat every recently closed issue as a
shipped fix. Read relevant PR bodies and closing issue references before
writing the player-facing change list.

Draft actual UTF-8 Markdown under ignored `local/releases/` using
[references/release-notes-template.md](references/release-notes-template.md).
Replace every placeholder, use actual newlines, and link documents at the
release tag. State the package type, supported ROM, setup, controls, observed
coverage, first known divergence and untested paths. Distinguish the GitHub
prerelease flag from prose such as "experimental quality".

## 3. Build, package and test before tagging

Use a clean build from the full target SHA. `tools/build_release.ps1` calls
the same setup/generator/guarded configuration as local development, sets the
release version and enables the tested timing/native-dispatch defaults in the
EXE itself. It stages notices/resources, inspects DLL imports and packages
only the binary payload. Double-click launch requires no developer script.

Before tagging, validate the candidate workflow commit with a dry run:

```powershell
gh workflow run build-release.yml --repo alondero/kirbysdreamcourse-recomp --ref <candidate-ref> -f publish=false
```

This fetches private inputs and builds the dispatched commit; the release job
is skipped. A request only to edit this skill does not authorize a CI run or
publication. For a local build using the same path:

```powershell
./tools/build_release.ps1 -Version <version> -Rom <private-ROM-path> -CMake <native-Windows-cmake>
```

For Linux x86-64, build on Ubuntu 24.04 with the dependencies and commands in
[Linux support](../../../docs/linux.md). Both platforms use `tools/setup.py`
for the same guarded patch order and generation settings. Linux keeps the
pinned framework's portable frame/audio implementations and static SDL3;
the package records required glibc and system-library dependencies.

```bash
python3 tools/build_release.py --version <version> --rom <private-ROM-path>
python3 .agents/skills/release/scripts/release.py smoke-linux --archive local/releases/<version>/kirbysdreamcourse-recomp-linux-x64.zip --rom <private-ROM-path>
```

`package-linux` inspects ELF dependencies and stages only the executable,
`start.sh`, resources, notices, readme and build metadata. Test the extracted
archive on the target Linux host; report headless and visible checks separately.

The shared build stages only reviewed release files under `local/release-stage`:

- `KirbyDreamCourseRecomp.exe` and its required runtime DLLs.
- `assets/` launcher resources and `licenses/` with their required notices.
- `LICENSE`, `LICENSE.snesrecomp`, `LICENSE.SDL3.txt`, `THIRD_PARTY_ATTRIBUTION.md`.
- A player-facing `README.txt` with ROM placement, launch, controls, settings
  and save locations, plus `build-info.json` describing this exact build.

The framework statically links SDL and MinGW runtimes by default. The staging
helper walks native imports, copies any required non-system DLLs and bundles
toolchain notices. Do not ZIP the whole build directory or a play session
containing ROMs, saves, settings or diagnostics. See the project reference for
the packager's `build-info.json` contract.

```powershell
python -m unittest discover -s tests -p 'test_*.py' -v
python .agents/skills/release/scripts/release.py smoke-windows --archive local/releases/<version>/kirbysdreamcourse-recomp-windows-x64.zip --rom <private-ROM-path>
```

The packager requires an x64 PE EXE, resources, notices and matching source
commit/version metadata, rejects private files and creates
`local/releases/<version>/kirbysdreamcourse-recomp-windows-x64.zip` with a
SHA-256. The smoke test extracts the ZIP with a clean PATH, runs 400 no-input
frames in native and fallback modes, compares direct defaults with explicit
developer settings and checks wrong-ROM rejection. It keeps diagnostics
private. These checks do not qualify interactive UI or full-game playability.

Test the extracted ZIP in a clean Windows directory without developer tools
or a generation step: correct ROM, wrong/missing-ROM handling, launch, audio,
input, settings and persistence. Test direct EXE launch and the actual release
timing configuration, not just the development play script. Verify the local
ROM's SHA-256 before ROM-dependent work. Replay deterministic native and
fallback scenarios against the pinned reference where practical; record first
divergence and untested paths in the notes. Report skipped checks accurately.

If a needed workflow or build path is missing, implement and validate it only
within the authorized release scope before proceeding. Never assume a workflow
mentioned in a comment exists. Recheck readiness after changing build inputs.

## 4. Tag and publish the prepared result

Only proceed after the applicable packaging gates pass and the version and
package type are settled. Check the remote tag and release for the version
first. A matching existing release is a verification/resume case; conflicting
tags or assets require investigation, never force pushes or blind retries.

Tag the tested commit and dispatch the implemented workflow explicitly:

```powershell
git tag -a <version> <full-sha> -m "Kirby's Dream Course Recompiled <version>"
git push origin 'refs/tags/<version>'
gh workflow run build-release.yml --repo alondero/kirbysdreamcourse-recomp --ref main -f tag=<version> -f publish=true -f prerelease=true
```

The workflow checks out the requested tag for publishing and the dispatched
commit for dry runs. Confirm the workflow revision contains the tested build
changes and origin names this repository. Capture the exact run ID and observe
it in waits of at most 60 seconds so progress remains visible. The expected
assets are `kirbysdreamcourse-recomp-windows-x64.zip` and
`kirbysdreamcourse-recomp-linux-x64.zip`. Both platform jobs must pass and agree
on commit/version before publishing. Keep prerelease true for the current
experimental state unless the user requests otherwise.

The workflow creates an initial notes body and a Windows & Linux x64 title. Replace
the body with the complete prepared UTF-8 notes once the build has passed:

```powershell
gh release edit <version> --repo alondero/kirbysdreamcourse-recomp --title "Kirby's Dream Course Recompiled <version> (Windows & Linux x64)" --notes-file <notes-path>
```

## 5. Verify the published release

```powershell
python .agents/skills/release/scripts/release.py verify --version <version> --target <full-sha> --archive <tested-windows-archive> --extra-archive <tested-linux-archive> --title "Kirby's Dream Course Recompiled <version> (Windows & Linux x64)" --prerelease true
```

The helper checks the remote tag's peeled commit (including annotated tags),
tag-bound URL, title, published state, prerelease flag, exact asset inventory,
uploaded size, ZIP integrity and the downloaded archive's SHA-256 against the
tested local file. Downloads use a temporary directory, without extraction.
It does not certify provenance or gameplay; those belong to step 3.

For an intended stable/latest release, also check GitHub's `/releases/latest`
points at this version. Experimental releases need not be latest.
Report the URL, commit, package type, asset, checksum, checks and remaining
gaps. On failure, report any tag/release already created and the failing step;
do not rebuild and overwrite an uploaded asset silently.
