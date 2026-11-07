# High-refresh presentation investigation

Decision for [issue #34](https://github.com/alondero/kirbysdreamcourse-recomp/issues/34),
2026-11-07: retain the original presentation. The framework clock is reusable,
but a rate switch alone does not smooth Kirby's output. General interpolation
is not practical as a small addition to the current renderer: it needs scene
history and stable motion identities first. No runtime setting, simulation
timing, generated code or renderer behavior is changed by this investigation.

## Source assessment

The starting Kirby revision is `0ce99d3b461063560fed885bfa3fe1aad7c9eb32`.
[Upstream findings](high-refresh-upstream.md) record exact framework and
Super Metroid pins, licenses and primary-source links. The pinned framework
already provides independent clocks, interpolation alpha, completed-raster
capture hooks and history-reset notifications. There is no need to update it
merely to obtain those interfaces.

Kirby's [host descriptor](../src/main.c) registers `KdcOrbitDraw` but no
`presentation_hz`, `begin_sim_frame` or `end_sim_frame` hook. Its default is
therefore one presentation per simulated frame. The native raster contains
the final composition, not separate moving objects; repeating it cannot
create motion, while blending it would also blend HUD text, flashes and
disoccluded edges. Re-running the raster between ticks is unsafe because
the host's raster capture executes guest IRQ work.

The [orbit renderer](../src/course_orbit.c) has enough geometry to redraw a
camera view, but currently ignores alpha. It reads current RAM, PPU palette,
render VRAM, terrain and sprites on every draw. Its input hook advances yaw,
zoom and ground-focus easing once per simulation tick; the draw hook derives
the pivot and look-ahead from the ball and current scroll, including a
viewport-dependent clamp for high balls. Interpolating yaw alone would leave
focus, scroll and moving objects stepping at the native rate. Interpolating
the whole pivot against current sprites can make the player slide relative
to the camera.

[Sprite sidecars](../src/course_sprites.c) reconstruct world anchors and
retain producer PCs, layout pieces, priority buckets and projected offsets.
They distinguish unresolved pieces, but the public `KdcOrbitSprite` does not
carry a persistent actor identity or allocation generation. A producer PC,
piece index, graphics tile or task slot alone cannot distinguish two actors
or reuse of a slot after despawn. Existing allocation/reset handling protects
the current anchor; it does not establish correspondence between two lists.
Interpolating particles by nearest position or draw order would be a guess.

## Bounded experiment and measurements

The committed [probe](../tools/probe_high_refresh.py) uses the existing
ROM-free renderer fixture and the pinned host clock. It drives identical
camera input at simulation deadlines and calls the actual orbit renderer at
synthetic 60, 120, 144 and 240 Hz presentation deadlines. Each presentation
is compared with an alpha=1 baseline for that simulation tick. This is an
experiment with repeated presentation, **not an interpolation implementation**.

Run from the repository root (GCC, Python and prepared framework required):

```powershell
python tools/probe_high_refresh.py --framework F:/src/kirbysdreamcourse-recomp/local/guarded-framework
$env:SNESRECOMP_ROOT='F:/src/kirbysdreamcourse-recomp/local/guarded-framework'
python -m unittest discover -s tests -p 'test_*.py' -v
```

The probe compiles with `-O2 -std=c11 -Wall -Wextra -Werror`, runs the existing
orbit fixture first, and leaves its executable in a temporary directory that
is removed afterwards. It uses synthetic graphics/geometry, never a game ROM.

Windows local result, approximately two seconds of virtual time per rate:

| Requested Hz | Camera ticks | Draws | Images different from current tick | RAM/PPU changes | Mean draw clock ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 60 | 121 | 121 | 0 | 0 | 1.760 |
| 120 | 121 | 241 | 0 | 0 | 1.697 |
| 144 | 121 | 289 | 0 | 0 | 1.668 |
| 240 | 121 | 481 | 0 | 0 | 1.680 |

Deadlines start at zero; floating-point accumulation can include a deadline
just below the two-second endpoint. These are event counts, not measured
monitor refresh rates. Every rate produced simulation-image sequence hash
`ca475b60fc5355c9` (the probe's 64-bit diagnostic hash, not a cryptographic
identity). First synthetic RAM/PPU or current-tick orbit-image divergence:
none. This activates the orbit renderer; it does not compare default native
output against a baseline executable. More draws
produced no intermediate motion images. The byte comparisons cover RAM and
the fixture PPU, not CPU/APU/SRAM or a full running machine.

Timing uses CRT `clock()` around presentation draws only; its platform-specific
semantics must not be interpreted as portable CPU time. The Windows CRT
reports elapsed wall time. [Microsoft CRT documentation](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/clock)
The fixture has a small
4-by-4 synthetic course at 256-by-224 pixels, with the real software terrain,
sprite and scenery renderer. About 1.7 ms per draw is roughly 40% of the
4.17 ms presentation interval at 240 Hz, before simulation, upload and swap.
This is a coarse capacity warning, not CPU utilization or a real-game frame-rate prediction. Wider
views, complete courses, filtering, heavy effects, GPU/driver timing and
missed deadlines were not benchmarked. The virtual clock never charges
rendering cost to its deadlines.

The fixture checks native fallback for fades, forced blank, mosaic, reset,
map changes and non-course screens; also sprite priority, decals, HUD,
elevated focus, filtering and read-only rendering. Those checks passed before
the rate sweep. They establish current behavior, not history invalidation
for a new interpolator.

The ROM-free test command ran 60 tests: 51 passed and nine optional tests
were skipped. GCC was MinGW-w64 15.2.0. The renderer and clock probe compiled
without warnings; no full game executable was rebuilt for this tooling/docs
change.

## What would make a prototype safe

A camera-only prototype is technically plausible, but should be explicitly
scoped to camera movement; it cannot meet moving-object smoothness by itself.
Before a player-facing option, capture a complete presentation camera at a
completed simulation boundary and draw with a temporary interpolated camera.
Keep input and focus easing on the simulation clock. All terrain, billboards,
decals and scenery must use that same temporary camera; drawing must not
overwrite the next simulation camera. Retain native presentation as default.

For moving objects, retain adjacent scene records and carry actor identity
plus allocation generation through the observer to each piece. Interpolate
only verified matching world anchors, keeping animation/palette changes at
defined tick boundaries. Unknown, spawned, despawned or ambiguous pieces must
use current-frame presentation. Define whether one native tick of visual
delay is acceptable: interpolation between completed frames adds that delay,
whereas prediction introduces errors at bounces and sudden changes.

Drop history on non-adjacent frames, map/player changes, reset/load/rewind,
orbit enable/disable, camera reset, incompatible geometry/viewport changes,
fades/blank/mosaic and unsupported scene changes. Return to the native field
where the existing orbit renderer does. A reset must seed both records from
the new scene so the first frame cannot smear from the old hole. Same-map
teleports and task reuse require explicit continuity rules, not just a map-ID
check. This investigation does not establish those rules, so no motion
interpolation was enabled.

## Evidence limits and next gate

There was no new default-output executable comparison, full-game deterministic replay, reference-emulator run,
monitor inspection or physical refresh-rate test. No new visual artifact was
observed on a display. Double edges from frame blending, sliding from partial
camera interpolation, and particle streaks from incorrect identity matching
are predicted failure modes, not measured results. The unchanged runtime
does not resolve existing [reference divergences](compatibility.md).

A future prototype needs the same frame-numbered input and initial SRAM with
the verified supported ROM: compare default-off images and guest state
against the pre-change executable, repeat each run for determinism, and then
compare on/off guest state at simulation boundaries. Record the first
divergence separately from intended presentation differences. Inspect aiming,
shots/bounces, powers, rewards/particles, fades, hole changes, two-player turns,
pause/focus loss and reset/load at 60/120/144/240 Hz. Measure full-frame median
and tail costs, dropped presents and audio continuity on Windows and Linux.
All of these full-game high-refresh paths remain untested here.
