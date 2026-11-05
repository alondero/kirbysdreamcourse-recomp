# Linux x86-64 support

The Linux package contains `KirbyDreamCourseRecomp`, `start.sh`, launcher
resources, notices and `build-info.json`. Players provide the same headerless
USA ROM as on Windows. Extract into a writable directory, put the ROM beside
the executable, and run `./start.sh`. The script selects its own directory,
including when launched through a non-Steam shortcut with a different working
directory. If necessary, restore permissions with
`chmod +x start.sh KirbyDreamCourseRecomp`.

The supported build baseline is Ubuntu 24.04 x86-64. The package uses system
glibc, libstdc++ and OpenGL; it does not bundle graphics drivers or glibc. SDL
3.4.10 is built from the existing hash-pinned tarball and linked statically;
its display/audio backends load the host's libraries. Optional launcher chat
emoji rasterization is disabled to avoid machine-dependent FreeType/HarfBuzz
dependencies. `build-info.json` records direct ELF dependencies and the highest
required glibc symbol version. Older distributions and non-x86-64 hosts remain
untested.

## Build and package

Install Rust/Cargo (the pinned native analyzer requires it), Git and these
Ubuntu packages. Put Cargo on PATH, for example by sourcing `$HOME/.cargo/env`
after installing Rust with rustup.

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends build-essential cmake ninja-build python3 pkg-config binutils libgl1-mesa-dev libasound2-dev libpulse-dev libudev-dev libdbus-1-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev libdecor-0-dev
python3 tools/setup.py --rom /private/Kirby.sfc --release-defaults
./local/guarded-build/KirbyDreamCourseRecomp --rom /private/Kirby.sfc
```

Python 3.11+ is required. Setup verifies the ROM before generation, checks the
framework pin, applies the guarded patch stack and generates into ignored
`local/guarded/`. Both platforms share this recipe. Fresh `local/guarded*`
directories are required; use `--name other-name` for a separate development
build. `--release-defaults` sets the same guarded runtime defaults used by the
Windows release. To select interpreter fallback, set
`SNESRECOMP_LLE_BOUNCE=0`. A plain development build without release defaults
requires the timing switches documented in `tools/play_experimental.ps1`.

From a clean checkout, the full release command is:

```bash
python3 tools/build_release.py --version v0.1.2-experimental --rom /private/Kirby.sfc
python3 -m unittest discover -s tests -p 'test_*.py' -v
python3 .agents/skills/release/scripts/release.py smoke-linux --archive local/releases/v0.1.2-experimental/kirbysdreamcourse-recomp-linux-x64.zip --rom /private/Kirby.sfc
```

Packaging checks ELF architecture, reviewed system dependencies, absence of
build-directory RPATH/RUNPATH, version/defaults and resource/notice inventory.
Only the reviewed stage is zipped. The smoke test extracts that ZIP, runs
native/fallback with a clean PATH, compares direct launch defaults with
explicit settings for 400 frames and checks rejection of a wrong ROM before
guest execution. No ROM, generated source, saves or recordings enter staging.

The manual `Build & Release` workflow uses `ubuntu-24.04` and the existing
private ROM repository credentials. It runs the full build/package command,
ROM-free tests and extracted-package smoke checks, removes private inputs and
generated source, then uploads only the Linux ZIP. Publication waits for both
Windows and Linux builds. A `publish=false` dispatch validates both without
creating a release or tag.

The optional startup tests exercise launcher skipping using a real SDL video
driver. On CI they run under `xvfb-run` (Xvfb and xauth), rather than SDL's
dummy driver, which would bypass the launcher decision automatically.

## Steam Deck

Build on the workstation or CI and transfer only the binary package plus the
owner's separate runtime ROM. SteamOS needs no compiler, Proton or changes to
its read-only system. Settings, bindings and saves remain beside the executable;
preserve them during updates. The owner adds non-Steam shortcuts personally:
choose `start.sh` as the target and its directory as Start In.

The pinned framework defaults to LLE frame composition, LLE audio time
advancement and byte CRC on Linux, versus Windows' HLE/native/slice8 paths.
These implementation choices are retained. Platform comparison evidence and
remaining gaps are recorded in [compatibility](compatibility.md).
