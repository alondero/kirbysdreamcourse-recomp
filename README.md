# Kirby's Dream Course recompilation experiment

An experimental Windows build of Kirby's Dream Course (USA), using [SNESRecomp](https://github.com/RetroPortingToolKit/snesrecomp) and an interpreter fallback. You must provide your own ROM. This repository does not include the ROM, extracted assets, generated game code, saves, recordings, or a game-derived executable.

The supported image is the **headerless, 1 MiB USA ROM**, SHA-256 `0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`. The build and launcher verify its hash. Do not share the ROM or generated game-derived output.

## Build and play on Windows

Install Git, Python 3.11 or newer, CMake 3.20 or newer, Ninja, and a 64-bit MinGW GCC/G++ toolchain; put them on PATH. CMake downloads the pinned SDL 3.4.10 source if an SDL3 package is not installed, plus the pinned recomp-ui launcher. Setup also needs network access to fetch the pinned SNESRecomp revision.

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

Setup creates `local/guarded-framework`, `local/guarded` and `local/guarded-build`; use a clean checkout to repeat setup. Generated files remain in ignored local directories. The play script sets the timing options and opens the graphical launcher; use `-Fallback` to select the interpreter, or `-NoLauncher` to start directly. The launcher also remembers **Skip launcher on boot**. `-Frames` runs skip the startup launcher. Settings and saves are stored beside the local executable. Default player-one keyboard controls are arrows, X for A, Z for B, Return for Start and Right Shift for Select.

Open the launcher during play with **Ctrl+L**, or **Select+L3** on a gamepad (Back + left-stick click on most controllers). The game and audio pause while the settings window is open. Choose **Resume** to apply changes and continue. Closing the settings window also resumes; **Quit Game** exits. The game window is hidden while settings are open.

Use **Settings** for fullscreen, window scale, pixel aspect, filtering, VSync and volume; use **Controller** for player-one/player-two input sources and bindings. Changes persist in `config.ini` and `keybinds.ini`. Renderer and audio sample rate can be selected in the startup launcher. Those controls are hidden during play while snapshot restart support remains unqualified; see [menu integration and validation](docs/menu-research.md) for tested paths and remaining limits.

For two players on the keyboard, choose Keyboard for both players on the Controller page and assign different keys to player two. The same sources can still be configured directly in `local/guarded-build/config.ini`:

```ini
[Controller]
SourceP1 = 1
SourceP2 = 1
```

Controller source values are `0` for none, `1` for keyboard and `2` for gamepad. For a gamepad seat, use `2` and connect the controller before starting the game. Each seat needs its own configured input source.

The included ROM-free input and comparison tests can be run with `python -m unittest discover -s tests -p 'test_*.py' -v`. The headless and reference-capture tools support locally stored input recordings; keep recordings and captures out of the repository.

For source-built SPC/DSP differential tests and the optional APU queue-boundary experiment, see [APU timing and audio checks](docs/apu-accuracy.md).

## Widescreen

Enable **Adaptive widescreen** on the launcher's **Mods** page, or set `Widescreen = 1` in the `[Graphics]` section of `local/guarded-build/config.ini`. It can be changed during play. Resize the window or use Alt+Return for fullscreen. The view follows the window's aspect ratio, including ultrawide displays, while retaining the original pixel proportions. A 4:3 window still shows the original width with the default 4:3 pixel aspect. Widescreen is off by default.

The course's extra terrain is rendered from its full map in memory, beyond the original streaming viewport. Draw requests captured before the game's viewport culling supply sprites in the horizontal margins. The HUD and bounded menu foregrounds remain centered. Whole-hole zoom and complete coverage of every object/effect are still unqualified. See [widescreen implementation and validation](docs/widescreen.md) and [sprite rendering](docs/widescreen-sprites.md) for coverage and limits.

## Status

Recorded single-player runs completed courses one and two in native and fallback modes. A two-player smoke test completed one hole. The pinned reference and host still differ in startup and gameplay state; audio, timing, save-state compatibility and many paths remain under investigation. See [compatibility and evidence](docs/compatibility.md) and the repository issues. Native blocks fall back to the interpreter when they cannot safely execute.

The framework is pinned to `147b55e252dcb3e7680a99c7c2f50e7183891844`. SNESRecomp and derived runtime work are subject to the [PolyForm Noncommercial license](LICENSE); third-party notices are in [THIRD_PARTY_ATTRIBUTION.md](THIRD_PARTY_ATTRIBUTION.md), and SDL's license is in [LICENSE.SDL3.txt](LICENSE.SDL3.txt). Game content is owned by its respective rights holders and is not supplied by this project.
