# Release notes template

Save the filled body as UTF-8 under ignored `local/releases/`. Replace all
angle-bracket placeholders. This template is for the default Windows/Linux binary
distribution. Every claim must hold for the tagged commit and actual archive.
Write separate notes only if the user explicitly requests a source release.

```markdown
> Experimental Windows and Linux x64 release for testing and feedback.
> <State the observed quality and the most relevant remaining limits.>

## You must supply your own ROM

Provide the headerless Kirby's Dream Course (USA) ROM, 1,048,576 bytes.
SHA-256: `0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.
The cartridge ROM, extracted game assets, saves, recordings and generated
source files are not included. This package contains the compiled recompiled
executables, built using private ROM input, plus runtime resources and notices.

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

1. Extract `kirbysdreamcourse-recomp-windows-x64.zip` into a fresh directory.
2. Place your supported `Kirby's Dream Course (USA).sfc` beside the EXE.
3. Double-click `KirbyDreamCourseRecomp.exe`.

On Linux, extract `kirbysdreamcourse-recomp-linux-x64.zip`, place the ROM beside
`KirbyDreamCourseRecomp`, and run `./start.sh`. If needed, restore executable
permissions with `chmod +x start.sh KirbyDreamCourseRecomp`.

Keep the bundled DLLs, `assets/` and `licenses/` in place. No compiler or local
generation step is required. <Confirm these steps from the extracted ZIP;
name any verified Windows/runtime prerequisites.>

<Document tested controls, native/fallback selection, settings and save
location. Link README.md at this tag for the full control/settings guide.>

## Build provenance

- Commit: `<full-sha>`
- Package: `kirbysdreamcourse-recomp-windows-x64.zip` (Windows x64 binary)
- Package: `kirbysdreamcourse-recomp-linux-x64.zip` (Linux x64 binary)
- SHA-256: `<archive-sha256>`
- Build/package command: `<exact-command>`
- Validation: <checks run on this commit and package; disclose skipped checks>
- Dependencies: <exact framework/UI pins and relevant build flags>
- Workflow: <Build & Release run link and exact build commit>

## License and attribution

SNESRecomp and derived runtime work use PolyForm Noncommercial 1.0.0.
See [LICENSE](https://github.com/alondero/kirbysdreamcourse-recomp/blob/<version>/LICENSE)
and [third-party attribution](https://github.com/alondero/kirbysdreamcourse-recomp/blob/<version>/THIRD_PARTY_ATTRIBUTION.md).
Game content belongs to its respective rights holders and is supplied by the player.

[Setup and controls](https://github.com/alondero/kirbysdreamcourse-recomp/blob/<version>/README.md)
| [Compatibility evidence](https://github.com/alondero/kirbysdreamcourse-recomp/blob/<version>/docs/compatibility.md)
```
