# Kirby's Dream Course recompilation experiment

Experimental Windows and Linux builds of Kirby's Dream Course (USA), using [SNESRecomp](https://github.com/RetroPortingToolKit/snesrecomp) and an interpreter fallback. You must provide your own ROM. This source repository does not include the ROM, extracted game assets, generated game source, saves or recordings. Binary release packages contain the compiled executable, runtime dependencies and notices; they require your matching ROM at runtime.

The supported image is the **headerless, 1 MiB USA ROM**, SHA-256 `0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`. The build and launcher verify its hash. Do not share the ROM, extracted game assets or generated source.

## Binary releases

Extract `kirbysdreamcourse-recomp-windows-x64.zip`, place your matching
`Kirby's Dream Course (USA).sfc` beside `KirbyDreamCourseRecomp.exe`, and launch
the EXE. Keep its DLLs, `assets/` and `licenses/` directories in place. Binary
packages do not require a compiler or a code-generation step. The older
`*-source-*.zip` releases require the source-build steps below.

On Linux, extract `kirbysdreamcourse-recomp-linux-x64.zip`, put your ROM beside
`KirbyDreamCourseRecomp`, and run `./start.sh`. Keep `assets/` and `licenses/`
in place. If your extractor loses executable permissions, run
`chmod +x start.sh KirbyDreamCourseRecomp` first. Linux builds target x86-64,
Ubuntu 24.04 or newer and SteamOS; SDL3 is included in the executable. System
OpenGL and C++ runtime libraries are required. See [Linux support](docs/linux.md)
for build commands and Steam Deck verification limits.

Maintainers use the `Build & Release` workflow and the repository's release
skill. The workflow fetches build inputs from a private companion repository
via `ROM_ASSETS_REPO` and `ROM_ASSETS_PAT`, verifies the ROM hash, generates and
compiles on each platform's runner, and uploads only the reviewed binary ZIPs. See
[release setup](docs/release.md) for configuration, dry runs and validation.

## Build and play on Windows

Install Git, Python 3.11 or newer, Rust/Cargo, CMake 3.20 or newer, Ninja, and a 64-bit MinGW GCC/G++ toolchain; put them on PATH. CMake downloads the pinned SDL 3.4.10 source if an SDL3 package is not installed, plus the pinned recomp-ui launcher. Setup also needs network access to fetch the pinned SNESRecomp revision.

Place your ROM at the project root as `Kirby's Dream Course (USA).sfc`, or pass its path with `-Rom`. In PowerShell, from the repository root:

```powershell
./tools/setup.ps1
./tools/play_experimental.ps1
```

Setup finds a native Windows CMake on its own and ignores an MSYS build that shadows it on PATH. To override that choice, or to specify a ROM or an existing SDL3 source tree:

```powershell
./tools/setup.ps1 -Rom 'C:/path/to/your-game.sfc' -CMake 'C:/path/to/cmake.exe' -SdlSource 'C:/path/to/SDL3'
./tools/play_experimental.ps1 -Rom 'C:/path/to/your-game.sfc'
```

Setup creates `local/guarded-framework`, `local/guarded` and `local/guarded-build`. Run it again to regenerate the game code and replace the executable in `local/guarded-build`; each run resets and cleans `local/guarded-framework` to the pinned revision, then reapplies its patches. Keep custom files and edits out of that generated checkout. Existing saves and settings beside the executable are retained. Generated files remain in ignored local directories. The play script sets the timing options and opens the graphical launcher; use `-Fallback` to select the interpreter, or `-NoLauncher` to start directly. The launcher also remembers **Skip launcher on boot**. `-Frames` runs skip the startup launcher. Settings and saves are stored beside the local executable. Default player-one keyboard controls are arrows, X for A, Z for B, Return for Start and Right Shift for Select.

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

## Hole practice

Choose **PRACTICE** on the game's title menu, below **DEMO PLAY**. Select a mode,
course and hole using the D-pad, then choose **PLAY THIS HOLE**. A selects and B
goes back. Choose a starting ability using its original portrait and name.
The picker keeps the music and background running; finishing a hole returns
there to replay or choose another. During practice, Start opens restart,
hole selection and exit options.
Practice saves are separate from ordinary progress. See [hole practice](docs/practice.md)
for controls and limitations.

## Start directly on a hole

After building, use the developer shortcut to skip the menus:

```powershell
./tools/play_experimental.ps1 -DevHole normal:3:7
./tools/play_experimental.ps1 -DevHole extra:8:8
./tools/play_experimental.ps1 -DevHole multi:2:4
./tools/play_experimental.ps1 -DevHole multi-extra:4:8
./tools/play_experimental.ps1 -DevHole demo:2
./tools/play_experimental.ps1 -DevHole map:160
```

Course and hole numbers start at 1. `demo:1` through `demo:4` select the attract-mode holes with human controls. `map:0` through `map:255` expose every ROM map-table slot, including unused and shared data; add `-DevTwoPlayer` to play a raw slot with two players. Some unused slots contain incomplete or placeholder layouts. Use `-DevHoleList` for syntax and `-Fallback` for interpreter testing.

The script stores developer saves and settings in `local/dev-play`, separately from ordinary play. **Ctrl+R** restarts on the selected hole. See [developer starts](docs/dev-start.md) for raw-slot ranges, executable arguments, headless captures and validation limits.

The [hole ID reference](docs/hole-ids.md) maps all 256 slots to their known courses and explains how to generate a local screenshot gallery from your ROM.

## Widescreen

Enable **Widescreen** on the launcher's **Mods** page, or set `Widescreen = 1` in the `[Graphics]` section of `local/guarded-build/config.ini`. Its **View aspect** option follows the window automatically by default, or can be fixed at 16:9, 21:9 or 32:9. Fixed views are letterboxed as needed; pixel aspect remains a separate Settings choice. Both controls can be changed during play. The aspect choice is saved in `kdc-widescreen.cfg` beside the executable. Resize the window or use Alt+Return for fullscreen. A 4:3 window with automatic sizing still shows the original width with the default 4:3 pixel aspect. Widescreen is off by default.

The course's extra terrain is rendered from its full map in memory, beyond the original streaming viewport. Draw requests captured before the game's viewport culling supply sprites in the horizontal margins. The HUD and bounded menu foregrounds remain centered. Complete coverage of every object/effect is still unqualified. For rotation and zoom, use the orbit camera below. See [widescreen implementation and validation](docs/widescreen.md) and [sprite rendering](docs/widescreen-sprites.md) for coverage and limits.

## Orbit camera

On a hole, use player one's **right stick** to orbit left/right and zoom
in/out. **R3** restores the original view. Keyboard equivalents are keypad
**4/6**, **8/2** and **5**. The **Orbit camera** option on the launcher's Mods
page is enabled by default; the original renderer remains active until you
move the camera. Original characters, effects and aiming guides remain in
play. Newly exposed terrain faces are reconstructed from the course geometry
and palette. See [camera implementation, tests and limits](docs/orbit-camera.md).

## HD textures

Enable **HD textures** on the launcher's **Mods** page to redraw course
terrain, scenery and sprites at 4x. Supported checkerboard edges are drawn
geometrically, including under orbit zoom; other art uses smoothed edges. Every HD pixel keeps
one of the original character's palette colours, so palette effects still
work. Menus and the HUD keep their original pixels.
HD textures are off by default. See [HD textures](docs/hd-textures.md) for
how it works, validation and limits.

## Status

Recorded single-player runs completed courses one and two in native and fallback modes. A two-player smoke test completed one hole. The pinned reference and host still differ in startup and gameplay state; audio, timing, save-state compatibility and many paths remain under investigation. See [compatibility and evidence](docs/compatibility.md) and the repository issues. Native blocks fall back to the interpreter when they cannot safely execute.

The framework is pinned to `147b55e252dcb3e7680a99c7c2f50e7183891844`. SNESRecomp and derived runtime work are subject to the [PolyForm Noncommercial license](LICENSE); third-party notices are in [THIRD_PARTY_ATTRIBUTION.md](THIRD_PARTY_ATTRIBUTION.md), and SDL's license is in [LICENSE.SDL3.txt](LICENSE.SDL3.txt). Game content is owned by its respective rights holders and is not supplied by this project.
