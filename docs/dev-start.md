# Developer hole starts

For the controller-operated game menu, see [hole practice](practice.md). Its
save isolation is automatic; the developer CLI behavior below is unchanged.

Build with `tools/setup.ps1`, then launch with `tools/play_experimental.ps1 -DevHole normal:3:7`. This is an opt-in testing shortcut. The original menu flow remains the default.

| Selection | Courses | Holes | ROM map slots |
| --- | --- | --- | --- |
| `normal:COURSE:HOLE` | 1-8 | 1-8 | 0-63 |
| `extra:COURSE:HOLE` | 1-8 | 1-8 | 64-127 |
| `multi:COURSE:HOLE` | 1-4 | 1-8 | 128-159 |
| `multi-extra:COURSE:HOLE` | 1-4 | 1-8 | 192-223 |
| `demo:NUMBER` | Attract selections 1-4 | Human controlled | 0, 42, 44, 77 |
| `map:ID` | Valid course context with an independent map override | Starts at hole 1 | Any decimal slot 0-255 |

Raw ranges 160-191 and 224-255 are outside the ordinary course selections. A slot is an index into the ROM's tables, not a guarantee of a distinct or complete hole. Several entries share map headers or placeholder terrain. Slot 184 is explicitly selected by the game's opening-sequence routine at `$87E572`; loading its data through the playable-hole loader does not reproduce that cinematic's special setup. Use raw slots to inspect/test the layouts, without assuming normal completion is possible.

The [hole ID reference](hole-ids.md) lists the known correspondence for every slot and explains how to generate a local screenshot gallery.

The demo selections come from the four attract records referenced at `$83DB10`: normal Course 1 Hole 1, normal Course 6 Hole 3, normal Course 6 Hole 5, and extra Course 2 Hole 6. They load those holes for a human player; they do not replay the attract input or its recorded health, RNG and shot state.

## Invocation and saves

```powershell
./tools/play_experimental.ps1 -DevHole extra:3:7
./tools/play_experimental.ps1 -DevHole map:160 -DevTwoPlayer
./tools/play_experimental.ps1 -DevHoleList
```

`-Executable` and `-Rom` retain their existing meanings. `-Fallback` selects interpreter execution. The shortcut bypasses the startup launcher, copies the executable and launcher resources into `local/dev-play`, and uses that directory's saves and settings. Existing controls/settings are copied on its first launch when available. Configure both controller seats via Ctrl+L for multiplayer. Ctrl+R resets and starts on the requested hole again. Loading a save state disarms the pending shortcut; it does not replace the restored game with a new hole.

The executable itself accepts `--dev-hole SELECTOR`, `--dev-two-player` (raw slots only), and `--dev-hole-list`. Direct executable invocation uses the usual executable-relative saves directory; the PowerShell script provides save isolation. Invalid selectors fail with exit code 2 before boot. There is no persistent setting or ROM modification.

For captures, use the same selector with `tools/headless.ps1 -DevHole SELECTOR` and optionally `-DevTwoPlayer`, alongside the ROM, executable, frame count and output arguments. `run.json` records the selection. Initial save states cannot be combined with a developer start. `compare_wram.py --recording` rejects these captures as ordinary reference boot scenarios because the control flow deliberately differs.

## Implementation evidence

The verified USA ROM is required (identity in `rom_identity.txt`). These are game-specific addresses, independent of SNES hardware behavior:

- `$80F0FF`: boot, SRAM validation and APU setup have completed. Set `$17FF` (player mode) and `$EAB9` (extra flag), then redirect on the main-loop stack to `$80F1DB`, retaining the original new-game initialization at `$80C036`.
- `$80F1DF`: set `$6A5A/$6A5C` (course and twice course), and `$6A5E/$6A60` (hole and twice hole). Continue at `$80F256`, which runs the original player and hole initialization calls.
- `$82B0A4` computes `course * 8 + hole`. After it returns, `$80C168` starts the shared hole loader. A raw selection replaces `$ABB2/$ABB4/$ABB6` with map ID, twice ID and three times ID before the loader reads the ROM's 256-entry tables (including `$88A770`). HUD/scoring retain a valid course context.

Three interpreter hooks are registered only when requested. They compose with the sprite observers and disarm after the first load. The pinned generated configuration currently interprets these sites in both native-dispatch and fallback modes. Future translation of the containing routines must preserve these hooks or provide an equivalent entry mechanism; an interpreter hook alone cannot intercept a compiled block. No generated code is edited, and the interpreter fallback remains available.

## Validation and limits

The Windows Release build and ROM-free selection/lifecycle tests pass. The full Python suite passes with optional local-runtime checks skipped unless explicitly enabled. Set `SNESRECOMP_ROOT` to the patched pinned framework for the C fixture, then run `python -m unittest discover -s tests -p test_dev_start.py -v`.

To include real-ROM starts, set `KDC_TEST_DEV_START=1` and optionally `KDC_DEV_EXECUTABLE` to the built executable, then run that same command. The runtime check verifies the exact ROM hash, uses fresh isolated saves and disabled physical inputs, and captures only its final frame under `local/`.

Local zero-input 600-frame starts reached the requested map for normal 1:1 and 8:8, extra 8:8, multiplayer 4:8, extra multiplayer 4:8, demo 2 and 4, and raw slots 160, 180, 184 and 255. Captured images show the playable course and HUD. Repeated normal 1:1 runs had identical final WRAM; native and fallback final WRAM matched for normal 1:1 and extra multiplayer 4:8.

For those 11 maps, loaded terrain bytes `$7E6B90-$7E838F` matched the pinned Snes9x 1.63 core. This was a local loader diagnostic: after 260 ordinary reference boot frames, an in-memory reference snapshot redirected execution through a small WRAM trampoline calling the same initialization/loader routines with the selected indices. It did not patch the ROM, modify the reference binary or establish matching frame timing, RNG, CPU, object, video or audio state. Captures and diagnostic snapshots remain local.

Without a developer selector, all 60 compared boot frames had identical WRAM before and after this change. Full WRAM already differs from the reference on ordinary boot (first known mismatch: host frame 15 versus reference frame 16; see [compatibility](compatibility.md)). Developer control-flow changes intentionally introduce additional differences. Terrain equality is a limited loader check, not gameplay equivalence. All 256 raw slots, hole completion, later course transitions, every multiplayer turn, cinematic-only behavior and save-state round trips are not exhaustively qualified.
