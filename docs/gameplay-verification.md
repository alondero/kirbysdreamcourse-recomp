# Verify changes in the running game

Build the current worktree with `./tools/setup.ps1` on Windows or
`python3 tools/setup.py` on Linux. Use the supported USA ROM at the repository
root or pass `--rom PATH` to the runner. Select the executable explicitly with
`--exe PATH` when comparing builds.

```sh
python tools/run_game_scenario.py scenarios/hole-shot.json
python tools/run_game_scenario.py scenarios/menu-to-hole.json
```

`hole-shot` starts on Course 1 Hole 1, plays a shot and checks that Kirby moves
out of the starting cell. `menu-to-hole` boots into the original game, creates a
member through the guest menus and checks that the first hole is ready to play.
Both use hand-authored controller sequences.

The runner prints the fresh `local/scenarios/run-*` directory. Open
`screenshot.png` for the composed frame and `result.json` for the measured
values, build and ROM hashes, and the earliest failed checkpoint. A failed
check, missing capture, early exit or timeout returns a nonzero status. Check
`stderr.log` for host diagnostics. Replay the same staged fixtures by passing
the saved `scenario-resolved.json` with the same executable and CLI options.
Keep captures, saves, ROMs, recordings and generated game source local.

## Verify the affected path

1. Start with the closest scenario. Choose a developer hole for a short route
   to a shot, camera or terrain change. Use ordinary menu input to verify
   member creation, selection, progression and stock behavior against Snes9x.
2. Add a check at the frame where the effect should occur. Check a verified
   WRAM field for game behavior and choose `screenshot` at the frame that shows
   a visual change. A run with zero checks establishes its frame budget and
   captures only.
3. Run the same scenario on the baseline and the candidate, or with the feature
   disabled and enabled. Inspect the images and state values at the same frames.
   Use `--fallback` to check the interpreter path. For persistence, start a
   second process with `initial_sram` pointing to the first run's `saves/save.srm`
   or `saves/custom/save.srm` and check the restored behavior.
4. Report the command, executable, reached state, measured effect, first
   unexpected difference and any paths that still need coverage. The raw dumps
   under `frames/` are indexed by zero-based frame and can be compared in order.

### Author a scenario

Copy one of the examples and put local fixtures and investigations under
`local/`. The scenario fields are:

| Field | Meaning |
| --- | --- |
| `frames` | Required frame budget. |
| `events` | Hand-authored holds with `frame` (one-based), `count` and `button`. Names include `a`, `start`, `up`, and chords such as `a+p2:b`. Leave an idle frame between holds. |
| `recording` | Path to an existing local frame-numbered JSON recording. |
| `host_script` | Path to a hand-authored framework script for host actions such as saving and loading a state. Choose one of `events`, `recording`, or `host_script`. |
| `dev_hole`, `dev_two_player` | [Developer hole selector](dev-start.md) and optional two-player mode for `map:` starts. Omit these for ordinary game boot. |
| `checks` | Named WRAM assertions with `label`, `frame`, `offset`, and `equals` or `min`/`max`. Offsets are relative to `$7E0000`, e.g. `"0xabb2"` for the map ID. Values are unsigned little-endian. `bytes` accepts 1, 2 (default) or 4. |
| `capture_from` | First WRAM dump, defaulting to the earliest check or the last captured frame. Each frame through `capture_to` uses 128 KiB plus metadata. |
| `capture_to` | Last WRAM dump, defaulting to the final frame. Set this explicitly if a save-state reload moves the guest frame counter backwards. The final WRAM hash refers to this dump. |
| `screenshot` | Composed image frame. Defaults to the final frame. Set to `null` for state-only checks. |
| `initial_sram` | Path to an 8192-byte starting save. Defaults to zeroed SRAM. |
| `course_pack` | Path to a local IPS course pack. Use `pack:gdc:COURSE:HOLE` for a direct start in the first external pack. |
| `camera_script` | Path to a text file of `frame horizontal vertical reset` rows. Camera frames are zero-based, axes range from −32768 to 32767, and `reset` accepts `0` or `1`. Axes hold until the next row. |
| `widescreen` | Set to `true` to enable widescreen. Set the aspect in the `kdc-widescreen.cfg` preference. |
| `orbit_sprite_log` | Set to `true` to capture the orbit renderer's sprite positions in `orbit-sprites.log`. |
| `preferences` | Optional map of game preference filenames to file contents, for example `{"kdc-presentation.cfg": "1 120\n"}`. Use `--paced` when testing presentation timing. |

WRAM checks and screenshots use **zero-based** completed frame indices.
Use input frame `N + 1` to affect completed frame `N`. Fixture paths are relative
to the scenario file. The runner stages a fresh game directory, disables
physical controller seats, and clears inherited game and SDL overrides.
Defaults use SDL dummy video and audio, with unpaced execution. `--paced` keeps
dummy video with normal pacing. `--windowed` opens an actual game window with
pacing. Use `--windowed --renderer OpenGL` to exercise the desktop OpenGL
presenter and allow additional wall time with `--timeout` for long routes.

### Course transition regressions

- Run `python tools/run_game_scenario.py scenarios/solo-finale.json --renderer SDL-Software`
  for Course 1 Hole 8. Inspect `screenshot.png` at frame 7500. Course terrain
  should continue across both 4:3 boundaries over the stars.
- Run `python tools/run_game_scenario.py scenarios/gordo-reload.json` for
  Course 4 Hole 5. The host script saves and reloads just before the sink.
  Pass the printed run directory to
  `python tools/diagnostics/check_gordo_anchor.py RUN_DIRECTORY` to check
  both the stationary Gordo and its shadow through the cup animation.
- Repeat with `--fallback` to verify interpreter execution. For evidence
  needed after worktree cleanup, copy the inspected screenshot, `result.json`,
  and relevant log excerpt to `local/verification/pr-<number>/<commit>/`
  in the main checkout. Keep saves, recordings, and raw dumps local.

### Reference comparison

For an ordinary game boot, pass the run's `input.json` and `initial.srm` to
the pinned reference. Host scripts require the recomp host. `--wram-frame`
and `--image-frame` use the reference's one-based indices. The core identity
is listed in [compatibility](compatibility.md).

```powershell
$run = 'local/scenarios/run-REPLACE_ME'
python tools/reference_capture.py --rom "Kirby's Dream Course (USA).sfc" `
  --core local/reference/snes9x_libretro.dll --frames 3101 `
  --input-recording "$run/input.json" --initial-sram "$run/initial.srm" `
  --wram-frame 3101 --image-frame 3101 --output "$run/reference.json"
```

Compare the relevant observed state and check alignment at the first mismatch.
For an automated full-WRAM comparison with input provenance checks, use
`tools/headless.ps1 -InputRecording` for the host captures and
`tools/compare_wram.py --recording` for comparison. The stock reference can
replay the ordinary route. Developer hole starts change boot control flow and
are intended for same-scenario host comparisons.

### Desktop UI and renderer checks

For launcher, settings or physical input changes, open
`./tools/play.ps1` on Windows, or start the Linux executable with
`--rom PATH` from its build directory. Use a private copy of the install when
changing settings or saves. Exercise the affected UI and inspect the result
on screen. Windows `-DevHole normal:3:7` is an interactive shortcut into a hole.
See [developer starts](dev-start.md) and [launcher controls](../README.md#build-and-play-on-windows).

Scenario captures use the composed game frame before desktop viewport scaling
and bypass the startup launcher. Frame-dump diagnostics request GPU readback.
For the normal GPU presentation path, window scaling, launcher UI or physical
input, launch the game interactively with the relevant settings and capture
the actual window. Use the audio and timing tools for sample or pacing claims.

## Verified paths

The guarded Windows build passed both checked-in scenarios in native and
interpreter modes. The shot moved from U cell 6 at frame 599 to cell 8 at
frame 810. Removing the input failed the movement check. The captured shot
WRAM matched across native, interpreter and the paced OpenGL window run.
A separate camera replay changed the composed view at frame 799 and kept
final WRAM identical. The menu route reproduced its final WRAM, SRAM and PNG
when replayed from `scenario-resolved.json`.

Two pinned Snes9x captures reproduced their per-frame records. Snes9x reached
the same playable hole and matched the four menu checks and final SRAM at
reference frame 3101. Full WRAM first differs at host frame 15 versus reference
frame 16 and differs at the final checkpoint. See [compatibility](compatibility.md).

These runs cover entry into the first hole and one shot. Full course completion,
Linux execution, launcher interaction, physical controllers and the GPU path
without diagnostic readback were outside this verification.
