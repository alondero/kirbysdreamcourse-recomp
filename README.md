# Kirby's Dream Course recompilation experiment

An experimental Windows build of Kirby's Dream Course (USA), using [SNESRecomp](https://github.com/RetroPortingToolKit/snesrecomp) and an interpreter fallback. You must provide your own ROM. This repository does not include the ROM, extracted assets, generated game code, saves, recordings, or a game-derived executable.

The supported image is the **headerless, 1 MiB USA ROM**, SHA-256 `0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`. The build and launcher verify its hash. Do not share the ROM or generated game-derived output.

## Build and play on Windows

Install Git, Python 3, CMake 3.20 or newer, Ninja, and a 64-bit MinGW GCC toolchain; put them on PATH. CMake downloads the pinned SDL 3.4.10 source if an SDL3 package is not installed. Setup also needs network access to fetch the pinned SNESRecomp revision.

Place your ROM at the project root as `Kirby's Dream Course (USA).sfc`, or pass its path with `-Rom`. In PowerShell, from the repository root:

```powershell
./tools/setup.ps1
./tools/play_experimental.ps1
```

To specify a ROM, CMake executable or existing SDL3 source tree:

```powershell
./tools/setup.ps1 -Rom 'C:/path/to/your-game.sfc' -CMake 'C:/path/to/cmake.exe' -SdlSource 'C:/path/to/SDL3'
./tools/play_experimental.ps1 -Rom 'C:/path/to/your-game.sfc'
```

Setup creates `local/guarded-framework`, `local/guarded` and `local/guarded-build`; use a clean checkout to repeat setup. Generated files remain in ignored local directories. The launcher sets the timing options; use `-Fallback` to select the interpreter. Settings and saves are stored beside the local executable. Default player-one keyboard controls are arrows, X for A, Z for B, Return for Start and Right Shift for Select. Edit `keybinds.ini` beside the executable and restart to configure player two; both players default to the same keys. Controllers can also be configured in the host.

The included ROM-free input and comparison tests can be run with `python -m unittest discover -s tests -p 'test_*.py' -v`. The headless and reference-capture tools support locally stored input recordings; keep recordings and captures out of the repository.

## Status

Recorded single-player runs completed courses one and two in native and fallback modes. A two-player smoke test completed one hole. The pinned reference and host still differ in startup and gameplay state; audio, timing, save-state compatibility and many paths remain under investigation. See [compatibility and evidence](docs/compatibility.md) and the repository issues. Native blocks fall back to the interpreter when they cannot safely execute.

The framework is pinned to `147b55e252dcb3e7680a99c7c2f50e7183891844`. SNESRecomp and derived runtime work are subject to the [PolyForm Noncommercial license](LICENSE); third-party notices are in [THIRD_PARTY_ATTRIBUTION.md](THIRD_PARTY_ATTRIBUTION.md), and SDL's license is in [LICENSE.SDL3.txt](LICENSE.SDL3.txt). Game content is owned by its respective rights holders and is not supplied by this project.
