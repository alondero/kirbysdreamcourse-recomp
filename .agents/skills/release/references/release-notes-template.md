# Release notes template

Save the filled body as UTF-8 under ignored `local/releases/`. Replace all
angle-bracket placeholders and remove instructions for the unused package type.
Every claim must hold for the tagged commit and the actual archive.

```markdown
> Experimental <source / Windows binary> release for testing and feedback.
> <State the observed quality and the most relevant remaining limits.>

## You must supply your own ROM

Provide the headerless Kirby's Dream Course (USA) ROM, 1,048,576 bytes.
SHA-256: `0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.
The ROM, saves, recordings and generated game-derived output are not included.

<For source: This archive contains source and build tools. It requires local
generation and compilation; it is not a ready-to-run executable.>

## What's working

<Lead with the main player-visible change. Describe observed behavior and
link the PRs included in this tag, grouped only where useful. For the first
release, describe the initial supported path instead of inventing a base.>

Changes since <previous-tag>:

- <User benefit and linked PR/issue actually fixed in this commit range.>

## What's not done yet

<Use docs/compatibility.md at this tag: recorded courses/modes, reference pin,
first known divergence, audio/timing gaps and untested paths. Preserve the
distinction between recorded success and full-game compatibility.>

Report problems with the release tag, native/fallback mode, relevant settings,
approximate course/hole and diagnostic text. Keep ROMs and game-derived
captures out of public reports.

## How to run

<For source: Extract the ZIP into a fresh directory. Install Git, Python 3.11+,
CMake 3.20+, Ninja and 64-bit MinGW GCC/G++ on PATH. Place your supported ROM
at the project root, then run the commands below in PowerShell. Setup needs
network access for pinned dependencies. Validate these steps from this ZIP.>

    ./tools/setup.ps1
    ./tools/play_experimental.ps1

<For a permitted Windows binary instead: name the archive, executable, ROM
placement, required bundled files and any system prerequisites verified from
the clean-package test. Do not require a local compiler or developer launcher.>

<Document tested controls, native/fallback selection, settings and save
location. Link README.md at this tag for the full control/settings guide.>

## Build provenance

- Commit: `<full-sha>`
- Package: `<asset-name>` (<source / Windows x64 binary>)
- SHA-256: `<archive-sha256>`
- Build/package command: `<exact-command>`
- Validation: <checks run on this commit and package; disclose skipped checks>
- Dependencies: <exact framework/UI pins and relevant build flags>
- Workflow: <run link, or local packaging with no CI release workflow>

## License and attribution

SNESRecomp and derived runtime work use PolyForm Noncommercial 1.0.0.
See [LICENSE](https://github.com/alondero/kirbysdreamcourse-recomp/blob/<version>/LICENSE)
and [third-party attribution](https://github.com/alondero/kirbysdreamcourse-recomp/blob/<version>/THIRD_PARTY_ATTRIBUTION.md).
Game content belongs to its respective rights holders and is supplied by the player.

[Setup and controls](https://github.com/alondero/kirbysdreamcourse-recomp/blob/<version>/README.md)
| [Compatibility evidence](https://github.com/alondero/kirbysdreamcourse-recomp/blob/<version>/docs/compatibility.md)
```
