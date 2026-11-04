---
name: release
description: Prepare, publish and verify Kirby's Dream Course Recomp source or Windows releases when asked to release or ship this project.
---

# Kirby's Dream Course release

Release `alondero/kirbysdreamcourse-recomp` from the requested checkout. Follow
the Lamborghini/AeroGauge pattern: identify shipped changes, prepare a tested
package and notes, tag the exact commit, publish, then verify the download.
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
next version from reachable published tags. Confirm an ambiguous version or
package type while continuing preparation. Never move an existing tag.

Read [references/project-release.md](references/project-release.md) for current
packaging evidence and the differences from the N64 release workflows.

- **Source release:** use when explicitly requested or already agreed. This
  follows the existing experimental source releases and requires users to
  install build tools and generate game code locally.
- **Windows binary release:** establish a distributable build before tagging.
  The current guarded build links generated game code from a local ROM. That
  executable is local-only under this repository's instructions. Prove a
  build without game-derived generated output, or report this specific gap
  and stop before publishing. Do not substitute source for a requested binary.

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

## 3. Build, package and test before tagging

For a **source** release:

```powershell
python .agents/skills/release/scripts/release.py source-package --target <full-sha> --version <version>
python -m unittest discover -s tests -p 'test_*.py' -v
```

The helper creates an archive of tracked files at that commit under
`local/releases/<version>/`, rejects known private/generated paths and missing
root licenses, checks ZIP integrity, and prints the SHA-256. Its path checks
cannot prove content provenance: review the complete file inventory too.
Run tests against the target source, not a different dirty checkout. Extract
the archive into a fresh local directory, verify source/docs/notices are
present, and follow its README setup using the local verified ROM. Check the
advertised native and fallback launch paths, settings, controls and saves.
Keep the locally built executable and outputs out of the archive. If setup
cannot be tested, report that limitation and do not claim it passed.

For a permitted **Windows binary**, inspect and validate the actual build and
packaging commands first. Include required DLLs, launcher resources and all
notices. Test the extracted ZIP in a clean Windows directory without developer
tools or a generation step: correct ROM, wrong-ROM rejection, launch, audio,
input, settings and persistence. Check runtime dependencies and the actual
release timing configuration, not only the development play script. Verify
the ROM's SHA-256 before any ROM-dependent work. Replay deterministic native
and fallback scenarios against the pinned reference where practical; record
first divergence and untested paths in the notes.

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
gh release create <version> <archive-path> --repo alondero/kirbysdreamcourse-recomp --verify-tag --prerelease --latest=false --title "Kirby's Dream Course Recomp <version> (experimental source)" --notes-file <notes-path>
```

The example is for an experimental source release. Adjust the title and flags
to the agreed type/status; do not inherit the N64 projects' stable flags.
Before pushing, confirm origin names this repository. If a verified workflow
now exists, inspect its revision, inputs, permissions, artifact names and ROM
handling and use its actual contract. Pin both workflow and build target;
observe its run ID in waits of at most 60 seconds so progress remains visible.
Replace placeholder notes and bare-tag titles if it creates them.

## 5. Verify the published release

```powershell
python .agents/skills/release/scripts/release.py verify --version <version> --target <full-sha> --archive <tested-archive-path> --title "Kirby's Dream Course Recomp <version> (experimental source)" --prerelease true
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
