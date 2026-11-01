# Launcher and in-game settings research

Source inspection: 2026-11-01. This compares source at the revisions below; it does not establish that another project's release builds or works on this machine. No ROM, extracted game assets or generated game code was used for the external source comparison. The subsequent Kirby integration used the locally supplied ROM after verifying its exact SHA-256. Local integration and its verification are recorded below.

## Finding

Super Mario World uses the shared **recomp-ui** launcher, built with Dear ImGui behind a C ABI. The SNESRecomp shared desktop host can reopen that launcher during gameplay, so Kirby can use the same interface without maintaining a separate UI host. An overlay drawn over the game is another integration: DKC2 supplies a custom ImGui overlay, while Super Metroid connects a shared menu model to its own pause screen. These are distinct implementations, not a launcher automatically supplied by linking the CPU runtime. [SMW build](https://github.com/mstan/SuperMarioWorldRecomp/blob/46b6d4fe21ef6c6f520823f1bdd81a22658e0750/CMakeLists.txt), [shared host design](https://github.com/RetroPortingToolKit/snesrecomp/blob/147b55e252dcb3e7680a99c7c2f50e7183891844/docs/LAUNCHER_DESIGN.md), [DKC2 overlay](https://github.com/elliotttate/DKC2Recomp/blob/3b386cb4a6b67472bc6693664ce61a11190e7b5d/runner/desktop_overlay.cpp), [Super Metroid pause integration](https://github.com/mstan/SuperMetroidRecomp/blob/d64bf010220826f246aa42a740908089c8647588/src/sm_pause_options.h).

## Inspected project revisions

| Project | Inspected game revision | Framework pin | UI pin |
| --- | --- | --- | --- |
| Super Mario World | `46b6d4fe21ef6c6f520823f1bdd81a22658e0750` | `90196539541b220de5cfe7182504ddac7e823ee3` | `01bff9477e6847b2281260f0a9526a85e45b8ba2` |
| Super Metroid | `d64bf010220826f246aa42a740908089c8647588` | `8d0b14ad4c61cae56674716cb220ed4feab9f1c3` | `ef13e3f1a00e2e03f91e4b74343fd77213f8a234` |
| Donkey Kong Country 2 | `3b386cb4a6b67472bc6693664ce61a11190e7b5d` | `3a929cd30336f2b3a077df17912719be63142299` | `ad2f3e293c6641c93ee69963dd669661f3e40290` |
| Donkey Kong Country | `3eb9a10c49cb210df2bd63addafe2a78cf9f4e22` | `de94587464524ebac8dcd62e14618bf0d3953644` | No recomp-ui submodule |

The gitlinks were read directly with `git ls-tree HEAD recomp-ui snesrecomp`. Repository trees preserve the evidence: [SMW](https://github.com/mstan/SuperMarioWorldRecomp/tree/46b6d4fe21ef6c6f520823f1bdd81a22658e0750), [Super Metroid](https://github.com/mstan/SuperMetroidRecomp/tree/d64bf010220826f246aa42a740908089c8647588), [DKC2](https://github.com/elliotttate/DKC2Recomp/tree/3b386cb4a6b67472bc6693664ce61a11190e7b5d), [DKC1](https://github.com/elliotttate/DKC1Recomp/tree/3eb9a10c49cb210df2bd63addafe2a78cf9f4e22). These exact pins differ; do not substitute one title's entire framework checkout into Kirby.

## Comparison

### Super Mario World: shared pre-boot launcher

Its CMake integration includes `recomp_ui.cmake` and calls `recomp_target_launcher_ui(... CONSOLE snes BOXART ...)`. Its game-owned `src/main.c` seeds settings and game identity, opens the launcher, and translates returned preferences into the host configuration. The launcher is skipped for scripted/headless paths. This is the clearest example of the requested startup experience. I did not find a general runtime settings overlay in this revision's game-owned main loop; the additional launcher call after teardown handles co-op rematches. Reopening settings mid-game should follow the shared host's explicit support. [Build integration](https://github.com/mstan/SuperMarioWorldRecomp/blob/46b6d4fe21ef6c6f520823f1bdd81a22658e0750/CMakeLists.txt#L246), [launcher and event loop](https://github.com/mstan/SuperMarioWorldRecomp/blob/46b6d4fe21ef6c6f520823f1bdd81a22658e0750/src/main.c#L1309).

### Donkey Kong Country 2: a full in-window overlay

DKC2 builds the shared pre-boot launcher and its own ImGui settings overlay. Its SDL host applies live settings, blocks guest controls while the overlay is open, skips guest execution, resets queued audio, and pauses the audio device. It resets pacing on transitions so time spent in the menu does not become catch-up frames. The settings UI distinguishes immediate changes from next-launch settings; copying controls without those host rules would be incomplete. Ordinary shutdown saves the returned settings. Windows also has native menus. This is useful implementation evidence if Kirby later needs an overlay that keeps gameplay visible. [Overlay UI](https://github.com/elliotttate/DKC2Recomp/blob/3b386cb4a6b67472bc6693664ce61a11190e7b5d/runner/desktop_overlay.cpp), [input routing](https://github.com/elliotttate/DKC2Recomp/blob/3b386cb4a6b67472bc6693664ce61a11190e7b5d/runner/sdl_main.c#L239), [pause/audio/pacing](https://github.com/elliotttate/DKC2Recomp/blob/3b386cb4a6b67472bc6693664ce61a11190e7b5d/runner/sdl_main.c#L1182), [settings persistence](https://github.com/elliotttate/DKC2Recomp/blob/3b386cb4a6b67472bc6693664ce61a11190e7b5d/runner/sdl_main.c#L1587).

### Super Metroid: settings integrated into the game's pause tabs

Super Metroid adds an Options tab beside Map and Equipment. The shared `RecompRuntimeUi` model owns rows and navigation; game code draws them using the framework's font primitives and intercepts the game's paused-state input. It keeps the ROM's map screen running underneath and leaves Start available to unpause. Game-specific settings persist through `SmVideoSave`; framework display setters mark the host configuration dirty for shutdown. Its guest-state hooks and snapshot handling make this more invasive than reopening a host launcher. Reusing this design for Kirby would first require evidence for Kirby's own pause-state machine. [Contract and lifecycle](https://github.com/mstan/SuperMetroidRecomp/blob/d64bf010220826f246aa42a740908089c8647588/src/sm_pause_options.h), [settings/model callbacks](https://github.com/mstan/SuperMetroidRecomp/blob/d64bf010220826f246aa42a740908089c8647588/src/sm_pause_options.c).

### Donkey Kong Country: native platform interfaces

DKC1 deliberately uses lighter host adapters rather than the larger launcher/config UI. Windows implements Win32 menus and settings dialogs; macOS has a native draggable pause panel. Its panel contract puts pause/audio ownership in the caller and requires execution after a completed guest frame. This is evidence for a native desktop alternative, but it would create more platform-specific work for Kirby. [Build boundary](https://github.com/elliotttate/DKC1Recomp/blob/3eb9a10c49cb210df2bd63addafe2a78cf9f4e22/CMakeLists.txt#L88), [Windows interface](https://github.com/elliotttate/DKC1Recomp/blob/3eb9a10c49cb210df2bd63addafe2a78cf9f4e22/runner/windows_platform.c), [pause-panel contract](https://github.com/elliotttate/DKC1Recomp/blob/3eb9a10c49cb210df2bd63addafe2a78cf9f4e22/runner/macos_pause_menu.h).

## Recommended reusable path

Keep Kirby's pinned framework revision `147b55e252dcb3e7680a99c7c2f50e7183891844`. Its shared host already supports `SnesDesktopHostGame.in_game_launcher`: default Ctrl+L or Select+L3 opens the launcher with the guest frozen and game window hidden. In-session mode presents Resume and Quit Game, and closing the launcher resumes. The host applies supported display/audio/input changes and handles restart-required changes through its own lifecycle. This provides configuration during play, although it is a separate launcher window rather than an overlay over the course. [Pinned framework behavior](https://github.com/RetroPortingToolKit/snesrecomp/blob/147b55e252dcb3e7680a99c7c2f50e7183891844/docs/LAUNCHER_DESIGN.md#in-game-launcher-opt-in), [implementation](https://github.com/RetroPortingToolKit/snesrecomp/blob/147b55e252dcb3e7680a99c7c2f50e7183891844/runner/src/desktop/host_main.c).

The compatible-era UI candidate is `3f1d6f122a58823614abce14f5b86f4d165a629c`, committed 2026-10-25: the last default-branch commit returned before the framework pin's date. The canonical repository is now `RetroPortingToolKit/recomp-ui`; the former mstan URL redirects there. Source inspection confirms its in-session fields, SDL-preservation API and result constants used by the pinned host. The Windows build and bounded runtime checks for this pairing are recorded below. [UI revision](https://github.com/RetroPortingToolKit/recomp-ui/commit/3f1d6f122a58823614abce14f5b86f4d165a629c), [C ABI](https://github.com/RetroPortingToolKit/recomp-ui/blob/3f1d6f122a58823614abce14f5b86f4d165a629c/src/recomp_launcher.h).

The integration helper enables C++17, compiles bundled Dear ImGui and UI code, selects SDL2/SDL3 from the host's backend, links OpenGL and platform libraries, defines `RECOMP_LAUNCHER`, and stages the selected console's fonts/images. The plain-C game host remains usable. A supplied box-art image is optional. [Pinned CMake helper](https://github.com/RetroPortingToolKit/recomp-ui/blob/3f1d6f122a58823614abce14f5b86f4d165a629c/recomp_ui.cmake), [asset manifests](https://github.com/RetroPortingToolKit/recomp-ui/blob/3f1d6f122a58823614abce14f5b86f4d165a629c/cmake/recomp_ui_assets.cmake).

For a later overlay, recomp-ui also provides a renderer-independent runtime model plus ImGui and ARGB8888 presentations. The host still owns event conversion, pause policy, live-setting callbacks, persistence and renderer submission. Use one shared-host adapter if needed; keep the menu out of generated game code. [Runtime UI architecture](https://github.com/RetroPortingToolKit/recomp-ui/blob/3f1d6f122a58823614abce14f5b86f4d165a629c/docs/RUNTIME_UI.md).

## Dependency notices

The selected recomp-ui revision is MIT; its vendored Dear ImGui is MIT, tinyfiledialogs is Zlib, and stb_image offers MIT/public-domain alternatives. The included fonts have separate terms: LatoLatin and Noto Sans Symbols 2 use OFL-1.1, OpenMoji uses CC BY-SA 4.0. The flag sheet notice identifies OFL-1.1 Noto provenance. Preserve the relevant license texts and notices in packaging; the asset-staging helper copies runtime files without automatically copying these notices. This UI license does not replace the SNESRecomp framework's PolyForm Noncommercial license. [UI license](https://github.com/RetroPortingToolKit/recomp-ui/blob/3f1d6f122a58823614abce14f5b86f4d165a629c/LICENSE), [ImGui license](https://github.com/RetroPortingToolKit/recomp-ui/blob/3f1d6f122a58823614abce14f5b86f4d165a629c/src/third_party/imgui/LICENSE.txt), [tinyfiledialogs](https://github.com/RetroPortingToolKit/recomp-ui/blob/3f1d6f122a58823614abce14f5b86f4d165a629c/src/third_party/tinyfiledialogs.h), [stb_image](https://github.com/RetroPortingToolKit/recomp-ui/blob/3f1d6f122a58823614abce14f5b86f4d165a629c/src/third_party/stb_image.h), [font notices](https://github.com/RetroPortingToolKit/recomp-ui/blob/3f1d6f122a58823614abce14f5b86f4d165a629c/assets/common/fonts/NOTICE.md), [image notices](https://github.com/RetroPortingToolKit/recomp-ui/blob/3f1d6f122a58823614abce14f5b86f4d165a629c/assets/common/img/NOTICE.md), [framework license](https://github.com/RetroPortingToolKit/snesrecomp/blob/147b55e252dcb3e7680a99c7c2f50e7183891844/LICENSE).

This inspection did not independently establish the provenance of every remaining UI image. Do not add game box art or ROM-derived decoration to Kirby's source tree or release. Keep the supported settings tied to host capabilities; an exposed toggle alone does not verify a renderer feature or improve game compatibility.

## Implemented Kirby integration

`CMakeLists.txt` fetches and verifies the exact UI revision above, includes its integration helper without building the standalone demos, and stages the SNES chrome, fonts and notices. An existing Git checkout at the same revision can be supplied with `-DFETCHCONTENT_SOURCE_DIR_KDC_RECOMP_UI=...` after explicitly applying this repository's `patches/recomp-ui-builtin-settings.patch`. Configuration refuses an unpatched external checkout without modifying it; the managed checkout is patched automatically. Game-derived output remains local. `src/main.c` opts into the shared host's in-game launcher and pixel-aspect controls. A new configuration explicitly selects keyboard for player one and leaves player two unassigned.

`src/launcher_settings.c` supplies one built-in feature, **Adaptive widescreen**, backed by the existing `[Graphics] Widescreen` setting. It stages the edit until Play/Resume, then updates the host configuration without restarting the guest. Its feature callback group includes no-option callbacks because recomp-ui requires those callbacks even for a feature with zero options. The current shared host does not round-trip the old launcher `widescreen` field, so simply linking the UI would not expose a functioning Kirby toggle. The existing per-game renderer remains responsible for its behavior and limits.

Two source patches complete the integration:

- `recomp-ui-builtin-settings.patch` hides external package-install and package-management controls when the provider does not implement them, and adds an optional `audio_frequency_locked` capability. Kirby locks that control in-session. CMake checks and applies the patch idempotently to the managed pinned UI source.
- `snesrecomp-launcher-with-rom.patch` adds optional `launcher_with_rom` and `validate_rom_path` host capabilities. Kirby opts in so the normal play script opens the launcher even though it supplies `--rom`, while respecting the saved SkipLauncher preference. A supplied ROM takes precedence over the cached path. The validation callback runs after every path-resolution route, including SkipLauncher and `--no-launcher`, before guest setup. `--no-launcher`, scripted/frame-dump paths and dummy video retain their UI suppression. Setup applies this patch with the existing framework patch stack; CMake checks that the validation capability is present.

Play/Resume and the host's final startup gate share one validator for the selected file's **raw** 1 MiB size and SHA-256 against the generated ROM identity. This adds Kirby's strict input contract on top of the shared picker's normalized fingerprint check: headered and modified images are refused even when the GUI is skipped. In-session ROM selection is locked so closing settings resumes the already verified running ROM. No ROM or box art is copied into the repository.

Settings are available at startup and through **Ctrl+L** or the configurable **Select+L3** gamepad gesture. The host freezes guest execution, pauses audio and hides the game window while settings are open. Resume and the window-close path apply supported live settings and restore presentation/pacing. The settings window is a separate window. Run-ahead is withheld because Kirby's snapshot round trips remain unqualified; rewind configuration is also left unexposed. Renderer and audio-rate controls are available only before boot, so changing settings during play cannot select their unqualified restart-through-snapshot path.

## Local validation, 2026-11-01

The Windows build succeeded using MinGW GCC/G++ 15.2.0, native Windows CMake 4.3, Ninja and SDL 3.4.10. The framework pin was unchanged. The build retains the interpreter fallback. The existing framework `FORCEINLINE` redefinition warnings remain; no new warning appeared in the game-owned settings module, which also compiled with `-Wall -Wextra -Werror` in its fixture.

Verified commands (replace local input/tool paths as needed):

```powershell
./tools/setup.ps1 -Rom "F:/src/kirbysdreamcourse-recomp/Kirby's Dream Course (USA).sfc" -CMake 'C:/Users/alond/AppData/Local/Programs/Python/Python313/Lib/site-packages/cmake/data/bin/cmake.exe' -SdlSource 'F:/src/kirbysdreamcourse-recomp/build/_deps/sdl3-src'
python -m unittest discover -s tests -p 'test_*.py' -v
```

The unit suite ran 21 tests: 18 passed and three optional APU/reference tests were skipped. The new ROM-free settings fixture checks staging/cancellation, the callback group required by the actual UI, apply/reopen/disable behavior, and rejection of missing files, synthetic copier-header images and same-size images with the wrong hash.

Real OpenGL launcher captures verified the dashboard, Settings, Controller and Mods views. The normal `tools/play_experimental.ps1` entrypoint was tested with the real startup GUI, without `--framedump` or guest input-script arguments (both suppress startup UI in the shared host). Fresh defaults selected keyboard for player one. A separate run verified saved SkipLauncher with an explicit ROM and no ROM-path cache. Ctrl+L resolved to the expected host command. Changing volume from 100 to 95, enabling widescreen, assigning keyboard input and rebinding Up to K through the UI persisted to `config.ini`/`keybinds.ini`. A fresh process reloaded the saved volume and widescreen values and reopened settings successfully.

For deterministic checks, separate executable directories started from zero 8192-byte SRAM, identical configuration and no guest input. In both guarded native and interpreter modes, the launcher opened at host frame 120 and closed automatically after 80 UI iterations. Across 240 simulated frames, every captured WRAM frame, CPU log, APU-state log and final SRAM matched a run without the menu. Repeating each menu run gave the same result. Native and interpreter baselines also matched each other. The GUI edit runs in both modes retained those same CPU/APU/WRAM/SRAM observations. Audio used SDL's dummy output device; this checks observed guest state, not audible output quality or sample-exact playback after resuming.

The shared host's smoke hooks make the pause/resume check repeatable, using a real Windows video driver:

```powershell
$env:SNESRECOMP_INGAME_LAUNCHER_SELFTEST = '120'
$env:LNG_SMOKE_FRAMES = '80'
$env:LNG_TEST_HIDDEN = '1'
$env:LNG_SCRIPT = 'view:settings;wait:5;view:controller;wait:1000'
./tools/play_experimental.ps1 -Rom 'C:/path/to/your-game.sfc' -Frames 240
```

Remove those four environment variables afterward. For full comparisons, copy the executable and its staged `assets` directory into a fresh `local/` case directory for each run; supply the same zero SRAM and config there. Set `KDC_FRAME_LOG` and `KDC_APU_STATE_LOG` to distinct capture paths, retain the four timing/execution variables from the play script, and invoke `game.exe --no-launcher --rom ... --framedump ...`. Keep the real video driver for a menu run; dummy video cannot create its OpenGL launcher. These are local captures, not repository fixtures.

The same 240-frame no-input scenario was captured with the pinned Snes9x 1.63 core (`72e0f16839c0298d8ce52eb2281e83feaede49af73936a39bb782b0f488e92e0`). Comparing menu/no-menu host captures against that reference retained the documented first WRAM mismatch: **host frame 15 / reference frame 16**, with 150 of the 240 selected comparisons differing. Menu integration does not establish emulator accuracy or remove the baseline divergence.

Untested: physical gamepad navigation and hotplug, held-input edge cases, fullscreen transitions, audible output-device behavior, snapshot restart round trips (withheld from the in-game settings controls), arbitrary gameplay-time openings, long sessions, and hosts other than Windows. The bounded boot checks above do not qualify save states, game completion or all configuration combinations. The existing [compatibility limits](compatibility.md) continue to apply.

## Independent-review fixes, 2026-11-01

The review's five findings were reproduced or confirmed and addressed. Renderer and sample-rate selection are now startup-only. One raw-image validator gates both launcher commits and every resolved host startup path; a headered running image can no longer originate through SkipLauncher or direct `--no-launcher` startup. Settings state now lives in the provider's context, and `<stdint.h>` is explicitly included. Externally supplied UI checkouts are left unchanged unless the user has explicitly prepared the required patch.

The C fixture uses an always-evaluated CHECK macro. Its Python wrapper builds both normal and `-DNDEBUG` variants under `-Wall -Wextra -Werror`, runs each with a valid expected hash, and verifies that a deliberately wrong expected hash fails with the actual CHECK diagnostic. Compilation and runtime failures now include captured stdout/stderr. The fixture also checks startup/in-session capability policy.

`tests/test_launcher_startup.py` adds opt-in Windows integration checks. Before the fix, headered images reached the first simulated frame through explicit SkipLauncher, cached SkipLauncher and `--no-launcher`. After the fix, both headered and same-size modified inputs are refused on all three paths before any guest frame log is produced, while the exact supported ROM still executes with SkipLauncher. Run these local checks with:

```powershell
$env:KDC_TEST_LAUNCHER = '1'
$env:KDC_LAUNCHER_ROM = 'C:/path/to/your-game.sfc'
python -m unittest discover -s tests -p 'test_*.py' -v
```

With those opt-ins, the suite ran 23 tests: 20 passed and the same three optional APU/reference tests were skipped. The Windows build succeeded. Fresh real-OpenGL captures confirmed renderer/sample-rate rows present at startup and absent in-session. New 240-frame native and interpreter menu runs again matched baseline CPU/APU logs, every WRAM frame and final SRAM. A further native check changed volume to 95 and enabled widescreen, then closed the settings window; both edits persisted, with those same observations unchanged and no restart. The baseline first reference divergence above remains unchanged; this round changes host policy and validation, without changing guest execution. CMake was also tested with a clean external UI checkout: it refused the missing patch with actionable instructions, left the checkout clean, and successfully returned to the managed configuration.
