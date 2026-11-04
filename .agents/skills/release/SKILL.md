---
name: release
description: Build, package, publish and verify a downloadable Windows EXE release of Kirby's Dream Course Recomp.
---

# Kirby's Dream Course release

Release `alondero/kirbysdreamcourse-recomp` as a **ready-to-run Windows x64 ZIP**
containing `KirbyDreamCourseRecomp.exe`, its runtime dependencies, launcher
resources and notices. The player supplies the ROM and launches the EXE
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

Establish a distributable EXE build before tagging. `tools/setup.ps1` builds a
local EXE, but the current guarded build links generated game code from a
local ROM. That executable is local-only under this repository's instructions.
Prove a build without game-derived generated output, or report this specific
gap before publishing. Building and ZIP-packaging the current EXE do not
satisfy this gate. Do not silently switch to interpreter-only behavior or
change the recompilation strategy to clear it.

The existing source releases are historical precedent only. A source package
requires an explicit source-release request; it never fulfills this skill's
default distribution goal. Its optional command is documented in the project
reference, separately from the EXE workflow.

ROMs, extracted assets, generated game-derived output, saves and recordings
stay local, including during CI. Do not upload them as build artifacts. Review
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

## 3. Build the EXE, package and test before tagging

Use a clean build from the full target SHA. Inspect the chosen generator,
patched framework and compiler/linker inputs to establish the distribution
gate above. Record and run the verified Release build command. Do not mark a
release prepared merely because a packaging command succeeds. At the recorded
baseline there is no verified distributable EXE build command; resolve that
build gap within the authorized scope or report it as unfinished work.

Move required timing/native-dispatch defaults from the developer launcher into
the release host/configuration as needed and validate them. The downloadable
EXE must work by double-clicking it; requiring `tools/play_experimental.ps1`
would leave the distribution incomplete. Set the actual build version instead
of the current hardcoded `dev`, with commit/toolchain/pin provenance.

Stage only reviewed release files under a fresh ignored local directory:

- `KirbyDreamCourseRecomp.exe` and its required runtime DLLs.
- `assets/` launcher resources and `licenses/` with their required notices.
- `LICENSE`, `LICENSE.snesrecomp`, `LICENSE.SDL3.txt`, `THIRD_PARTY_ATTRIBUTION.md`.
- A player-facing `README.txt` with ROM placement, launch, controls, settings
  and save locations, plus `build-info.json` describing this exact build.

Inspect native imports to identify required MinGW/SDL/other runtime DLLs;
copy their corresponding notices as well. Do not ZIP the whole build directory
or a play session containing ROMs, saves, settings or diagnostics. See the
project reference for the packager's `build-info.json` contract.

```powershell
python -m unittest discover -s tests -p 'test_*.py' -v
python .agents/skills/release/scripts/release.py windows-package --target <full-sha> --version <version> --stage <reviewed-staging-directory>
```

The packager requires an x64 PE EXE, resources, notices and matching source
commit/version metadata, rejects private files and creates
`local/releases/<version>/kirbysdreamcourse-recomp-windows-x64.zip` with a
SHA-256. It cannot inspect the provenance of code linked inside the EXE or
prove every DLL is present: those require the build review and clean test.

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

No release workflow existed at the recorded baseline. For that state, publish
the locally tested artifact directly using explicit repository and tag:

```powershell
git tag -a <version> <full-sha> -m 'Kirby Dream Course Recomp <version>'
git push origin 'refs/tags/<version>'
gh release create <version> <archive-path> --repo alondero/kirbysdreamcourse-recomp --verify-tag --prerelease --latest=false --title "Kirby's Dream Course Recomp <version> (Windows x64)" --notes-file <notes-path>
```

The example publishes the tested EXE ZIP as a prerelease. Adjust the flags
to the agreed status; do not inherit the N64 projects' stable flags.
Before pushing, confirm origin names this repository. If a verified workflow
now exists, inspect its revision, inputs, permissions, artifact names and ROM
handling and use its actual contract. Pin both workflow and build target;
observe its run ID in waits of at most 60 seconds so progress remains visible.
Replace placeholder notes and bare-tag titles if it creates them.

## 5. Verify the published release

```powershell
python .agents/skills/release/scripts/release.py verify --version <version> --target <full-sha> --archive <tested-archive-path> --title "Kirby's Dream Course Recomp <version> (Windows x64)" --prerelease true
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
