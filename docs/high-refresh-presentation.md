# Smooth high-refresh presentation

Implemented for [issue #34](https://github.com/alondero/kirbysdreamcourse-recomp/issues/34),
2026-11-07. **Smooth presentation** generates intermediate camera and world-object
positions while leaving the guest's native simulation clock unchanged. It is
optional and off by default. This supersedes the initial investigation-only
conclusion at commit `4584a1c`.

## Using it

Open the launcher's presentation features, enable **Smooth presentation**, and
choose **Display** or a fixed rate: 60, 90, 120, 144, 165, 240 or 360 Hz. Display
uses the monitor's reported refresh rate, bounded by the framework. The choice
is saved in executable-relative `kdc-presentation.cfg`; missing or invalid
settings leave the feature off. The original native presentation remains the
default. Settings can be changed in the in-game launcher.

Smooth presentation uses the reconstructed course renderer even at the
original camera angle. Orbit controls remain independently optional; disabling
them restores the original angle without disabling smoothing. Menus, fades,
forced blank, mosaic and unsupported scenes retain native field presentation.
Turbo/unlimited execution uses the current frame rather than fractional
presentation. Interpolation adds one simulation tick of visual history, about
16.6 ms. Animation artwork, palettes and HUD updates retain native cadence.

Rates are targets, not a guarantee of monitor delivery or locked FPS. The
guest runs on the host thread; the course renderer splits each present into
row bands drawn by a small worker pool (`src/host_parallel.c`). A busy machine
may miss presentation deadlines; choosing 240 Hz does not make physics run at
240 Hz.
The inherited [orbit renderer limitations](orbit-camera.md#limits), including
reconstructed cliff artwork and native priority-mask limitations, also apply.

## Implementation and continuity

The [upstream assessment](high-refresh-upstream.md) records exact framework and
Super Metroid revisions and licenses. The existing framework at
`147b55e252dcb3e7680a99c7c2f50e7183891844` already supplies independent simulation
and presentation clocks, fractional alpha, completed-raster capture and reset
hooks. No dependency upgrade or copied upstream interpolation code was needed.

[main.c](../src/main.c) connects the rate and capture hooks. The original raster
still runs once per simulation tick, including its guest IRQ work. Extra
presentations never run the raster or guest again.

[course_orbit.c](../src/course_orbit.c) retains two completed presentation
records: camera transforms and sprite geometry. Input and focus easing advance
on simulation ticks. Each draw uses a temporary camera shared by terrain,
billboards and decals; it cannot advance or overwrite the simulation camera.
Textures, palette, terrain and the native HUD field use the latest completed
scene. There is no cross-fade or optical-flow blend. Every present redraws
terrain, scenery and sprites from scratch: the terrain and scenery images are
kept as textures that change only where the game's tile maps, characters or
palette change ([HD textures](hd-textures.md#orbit-terrain-texture-2026-11-11)),
so a redraw costs a textured triangle pass rather than a reconstruction of the
artwork, and no occluder from an earlier scene can survive into a new one.

[course_sprites.c](../src/course_sprites.c) carries verified ball/guide identity
and task allocation generations into the sprite records. Task creation and
cloning change the generation, so a reused slot cannot inherit a departed
actor's motion. Layout addresses identify individual pieces. Whole draw-request
movement is retained separately from animation-piece offsets, allowing an
actor to move smoothly when its animation changes layout. Native and interpreter
execution share the existing observer sites; generated source was not edited.

Unknown identities, unmatched spawns and ambiguous previous anchors use current
geometry. Duplicate/changed pieces can still interpolate a verified common
actor origin while keeping current artwork offsets. Large world/offset jumps
snap. Adjacent records must agree on frame sequence, map, active player and
viewport, and have bounded target, look-ahead, yaw and zoom changes. Map/player
transitions, timeline gaps, resets, mode changes, fades/blank/mosaic and invalid
geometry discard history. Turning orbit controls off resets the angle and its
history. No interpolation is attempted across rejected boundaries.

## Reproducible validation

The ROM-free probe compiles the actual renderer and pinned clock with GCC
`-O2 -std=c11 -Wall -Wextra -Werror`. It reuses synthetic geometry/artwork from
the renderer fixture and advances camera input only at simulation deadlines:

```powershell
python tools/probe_high_refresh.py --framework local/smooth-framework
$env:SNESRECOMP_ROOT=(Resolve-Path local/smooth-framework).Path
$env:RECOMP_UI_ROOT=(Resolve-Path local/smooth-build/_deps/kdc_recomp_ui-src).Path
python -m unittest discover -s tests -p 'test_*.py' -v
```

Pixel fixtures verify intermediate object and camera positions, read-only
RAM/PPU behavior, animation-layout changes, allocation reuse/clone identity,
unknown-object fallback, timeline gaps, same-map scroll cuts, transition
fallback and restoring the camera when controls are disabled. Existing orbit
fixtures cover HUD, ground decals, priority, occlusion, filtering, zoom and
widescreen bounds. Settings tests cover staging, validation and persistence.

The real-ROM validator needs the supported hash, a new executable and the
baseline executable built from `4584a1c`. Both builds use the same current
pinned framework patches and generated input configuration. For example:

```powershell
python tools/validate_high_refresh.py --rom "F:/src/kirbysdreamcourse-recomp/Kirby's Dream Course (USA).sfc" --executable local/smooth-build/KirbyDreamCourseRecomp.exe --baseline local/baseline-build/KirbyDreamCourseRecomp.exe
```

It creates a fresh ignored `local/high-refresh-validation-*` directory, starts
with absent SRAM, and runs a 900-frame hole 1-1 replay. The camera turns at frame
500, stops at 560 and resets at 800; three A-button presses initiate a shot.
It compares all 900 rows of the host's CPU-register/WRAM-CRC trace
(A/X/Y/S/D/DB/PB/P, not a complete machine snapshot), exact WRAM dumps at
frames 790-899, and final SRAM. Default-off images are compared to baseline.
Each enabled rate is run twice. Wall-clock presentation alpha/counts are not
expected to be deterministic. Failures identify the first differing trace row
or capture frame and the input script.

Per-present CRCs must change within simulation frames during camera motion and
the shot. Separately, `KDC_PRESENTATION_DRAW_LOG` verifies that actual real-game
sprite coordinates passed to drawing differ from the latest simulation record;
a following camera alone cannot satisfy this check. `KDC_PRESENTATION_LOG`
records identity/match coverage. `--pictures` retains presented images for the
shot interval instead of logging the whole session. Private ROM-derived data
stays under `local/` and is not committed or packaged.

## Results and limits

Windows/MinGW-w64 GCC 15.2.0 Release build passed. The full suite ran 61 tests:
54 passed, seven optional checks skipped, with the existing real-ROM
native/interpreter camera replay enabled. The new repeated validator passed
for 120, 144 and 240 Hz. Both default-off replays matched all 110 sampled baseline images.
Every enabled replay matched the baseline CPU-register/WRAM trace, all 110
sampled WRAM dumps and final SRAM. First divergence in these comparisons: none.
These are host-to-host checks, not a new reference-emulator qualification; the
existing [reference divergences](compatibility.md) remain unresolved.

The enabled runs recorded 74-93 draws with genuinely interpolated object
coordinates during frames 835-899, and changed images within simulation frames
at every tested rate. One inspected 144 Hz capture contains different pixels
at frame 858 for alpha 0 and 0.595, frame 863 for alpha 0 and 0.5486, and frame
866 for alpha 0 and 0.5587. Kirby and the course remain coherent and the HUD
stays fixed in those pairs; no cross-fade ghosts were observed. This is a
sampled inspection, not proof that every effect is artifact-free.

The instrumented replay runs had variable host load (even default-off runs
varied substantially), so they are correctness evidence rather than a stable
hardware benchmark. The software renderer did not sustain every requested
rate. A separate profile-only pass removed state/image tracing and measured
frames 500-900. Reusing unchanged-camera terrain improved the observed rates:

| Requested Hz | Before terrain cache | With terrain cache | Simulation Hz with cache |
| --- | ---: | ---: | ---: |
| 120 | 100.4 | 113.4 | 60.06 |
| 144 | 110.6 | 123.0 | 60.09 |
| 240 | 115.4 | 140.1 | 60.08 |

These are sequential local observations, not controlled statistical benchmarks
or physical monitor delivery measurements. They show actual presentation above
native cadence without increasing simulation speed. A later 144 Hz profile-only
repeat measured 108 presentations/sec and 60.04 simulation ticks/sec, illustrating
the host-load variation; the table is not a promised minimum. Reproduce the profile-only
procedure with the validator's `--benchmark-only` flag (omit `--baseline`);
it intentionally makes no fidelity comparison. Profile logs and per-present
timing remain with the private validation output.

The small ROM-free timing probe likewise is not a complete-course
performance prediction; its CRT `clock()` timing must not be interpreted as
portable CPU usage. [Windows CRT clock semantics](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/clock)

Unverified paths include physical-monitor/VRR behavior, Linux/Steam Deck,
full-course completion with smoothing, every power/reward/particle animation,
two-player turn changes, and runtime save/load through all effects. Map,
player, load/reset and unsupported-transition policies have code/fixture
coverage rather than a full-game replay for every combination. Audio remains
on the original simulation/consumer path and was enabled through the dummy
backend in replay; no new waveform or listening comparison was performed.
Unknown/unmatched actors deliberately retain native motion rather than
inventing correspondence. These limits do not change the default renderer or
the interpreter fallback.
