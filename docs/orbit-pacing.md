# HD orbit pacing (#67)

The decoupled host now gives simulation priority when HD composition overruns
the native frame budget. It retains ordinary timing debt for catch-up, skips
post-simulation presentation while another simulation is due, and admits an
intermediate presentation only when its estimated cost fits before the next
simulation deadline. The estimate is a decaying peak of the entire draw/upload
call, including blocking vsync. Presentation deadlines still advance from
dispatch time, so skipped deadlines are counted without queuing stale images.

When a simulation actually waits for the audio consumer, the ordinary clock
is aligned to that frame's effective start (wall start plus measured producer
waits), and a due post-simulation presentation is allowed. That independent
consumer has already paced the frame. Otherwise a small difference between
audio and wall clocks can keep the wall-clock deadline overdue and suppress
affordable intermediate frames even in cheap scenes. Explicit loader debt
still takes precedence over this alignment. The audio queue, producer waits
and sample generation are unchanged; the host measures existing waits.

Ordinary debt is bounded to 250 ms; after a longer stall the host resumes at
the current time. The existing explicit loader-debt policy is retained. A
100 ms interval without a completed presentation permits a post-simulation
draw even under overload, keeping the display responsive. This interval is
not a promised minimum presentation rate: the draw itself and guest work
also take time. Pause/reset clear the clock and its cost estimate. Lockstep,
turbo and unpaced operation retain their existing simulation policy.

No guest frames are skipped, no physics or hardware timing is changed, and
the interpreter fallback remains available. A machine that cannot execute
the guest and its required raster/capture work at native speed can still
slow down. HD orbit composition itself remains expensive; this change does
not promise 120 presentations per second.

## Reproduction

Use a Release build and the supported ROM SHA-256
`0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`:

```powershell
python tools/benchmark_orbit_pacing.py --executable local/guarded-build/KirbyDreamCourseRecomp.exe
python tools/benchmark_orbit_pacing.py --executable local/guarded-build/KirbyDreamCourseRecomp.exe --display --backend SDL --repeat 1 --hd 1
```

The benchmark verifies the ROM hash and creates isolated executable-relative
settings and saves under ignored `local/orbit-pacing-*`. It uses the issue's
`multi-extra:3:1` start, 32:9 view, edge HUD, 120 Hz smooth presentation,
1,800 simulation frames, and maximum right orbit/zoom-in during frames
500-649. Timing begins at frame 350. Zoom-in is vertical input -32768.
The four guarded execution settings are explicit, physical controllers are
disabled, and no screenshots or state traces are taken during measurement.
Audio uses SDL dummy. By default video is also dummy and both HD-on/off
controls repeat twice. Results include simulation and presentation rates
and per-stage means; any simulation rate below 57 fps fails the command.

Use `--aspect 16:9`, `--rate 60`, `--display --backend SDL-Software`, or
`--display --backend SDL --vsync` for controls. Run them sequentially without
competing builds/tests. `--min-fps 0` records a known-failing baseline without
failing the command. These are host-performance measurements, not a
deterministic performance guarantee across machines or host loads.

## Diagnosis and validation

The pinned framework remains `147b55e252dcb3e7680a99c7c2f50e7183891844`.
The fix is an additive generic desktop-host patch, installed by both setup
paths and required by CMake. It does not edit generated game code.

The former host dispatched intermediate presentation even with insufficient
time before simulation, and re-anchored late simulation to a future deadline.
That discarded time instead of recovering by skipping presentation. A
ROM-free synthetic replay of the real clock and the host's two dispatch
sites, with 12.5 ms simulation/raster work and 11 ms presentation, executed
only 291 simulations in ten seconds. The regression requires at least 599
and continuing presentation. It also covers a 60 Hz target, inexpensive
120 Hz rendering, guest lag periods, long stalls, loader debt, starvation,
the legacy lockstep policy, waiting after an unaffordable presentation, and
an independently paced audio consumer. Without consumer alignment the cheap
audio-paced fixture produced only 598 presentations in ten seconds; it now
requires at least 1,180 while retaining at least 595 simulations.

```powershell
python -m unittest discover -s tests -p test_host_presentation_budget.py -v
```

### Windows measurements, 2026-11-14

Ryzen 9 3900X, RTX 3080, Release build, 12 rendering workers. Baseline is
`a98a3a2`. Each run used 1,800 frames; rates below cover the profiled interval
350-1800, excluding startup. Performance runs were sequential, without
competing builds or tests. Host load still varied, especially guest time;
these observations are not statistical guarantees.

| Configuration | Baseline simulation fps | Candidate simulation fps | Candidate presentation fps |
| --- | ---: | ---: | ---: |
| 32:9 HD, dummy software, 120 Hz | 30.82 | 59.49 / 58.06 | 12.55 / 11.88 |
| 32:9 HD off, dummy software, 120 Hz | — | 59.26 / 60.06 | 71.22 / 64.29 |
| 32:9 HD, physical Direct3D 11, 120 Hz | 33.06 | 59.99 | 21.50 |
| 16:9 HD, dummy software, 120 Hz | — | 60.05 | 42.46 |
| 32:9 HD, dummy software, 60 Hz | — | 60.12 | 13.30 |

The physical SDL renderer identified itself as `direct3d11` with a 119.880 Hz
display. Requesting vsync does not exercise blocking vsync: the existing
decoupled host explicitly disables driver vsync and reports `vsync=0`.
This is **not** a vsync/VRR qualification. The physical
software baseline also reproduced the slowdown (27.52 simulation fps).
Monitor scanout latency, tearing and frame delivery were not measured.

The baseline dummy run spent 6.43 ms in guest work, 6.63 ms in raster/capture,
and 9.79 ms composing each presentation. Later orbit profile groups had zero
full terrain rebuilds and zero scenery enlargements, around 0.88 ms preparing
and 7.84-7.90 ms drawing. This supports sustained HD drawing cost rather than
cache reconstruction or screenshot encoding as the dominant renderer cost.
The physical Direct3D baseline similarly spent 9.12 ms composing and 1.12 ms
uploading/presenting. Its candidate spent 11.72 ms composing each of far fewer
presentations: this is a scheduling fix, **not a composition speedup**.
Animation changes can accumulate between dropped presentations. Further HD
renderer optimization remains part of [#44](https://github.com/alondero/kirbysdreamcourse-recomp/issues/44).

The low presentation rates in this stress case are a material limitation.
Game time stays near native speed at the cost of visibly less frequent
updates. A requested 120 Hz is a ceiling, not a guarantee. The synthetic
cheap-render test separately requires at least 1,190 presentations in ten
seconds, so the budget must not disable high refresh when work fits.

All 59 setup patches applied in order to a clean checkout of the pin; the
three patched desktop files were byte-identical to the built checkout. The
full suite completed 96 tests: 77 passed and 19 optional checks skipped.
The Windows build used:

```powershell
python tools/setup.py --cmake <native-cmake.exe> --sdl-source <pinned-local-sdl-source>
python -m unittest discover -s tests -p 'test_*.py' -v
```

Two separate untimed replays of the exact 1,800-frame issue scenario matched
the baseline's entire CPU-register/WRAM-CRC trace, all 100 full-WRAM dumps
from frames 1700-1799, and final SRAM. State tracing and WRAM dumps were
enabled only for these fidelity runs, never the performance measurements.
First observed guest-state divergence: none. Inputs were the benchmark's
same camera script with no shot input.

The existing 900-frame shot/camera validator passed two default-off and two
120 Hz replays. All CPU-register/WRAM-CRC rows, 110 sampled full-WRAM dumps
and final SRAM matched baseline; all 110 sampled default-off images matched
in both repeats. The enabled runs retained intermediate camera images in
46/59 sampled frames, intermediate object-motion images in 13/14 frames,
and 81/74 draws with interpolated object coordinates. First observed guest
state divergence: none.

```powershell
python tools/validate_high_refresh.py --rom "Kirby's Dream Course (USA).sfc" --executable local/guarded-build/KirbyDreamCourseRecomp.exe --baseline <a98a3a2-executable> --rates 120
```

These are host-to-host fidelity checks, not a new reference-emulator
qualification. A reference emulator does not render the optional orbit view;
the existing [reference divergences](compatibility.md) remain unresolved.
Untested paths include Linux/Steam Deck, physical audio listening/waveform
comparison, VRR/scanout delivery, full-course completion, all multiplayer
turn changes, and interactive pause/load or device stalls under this HD
load. Clock reset, lag periods and long-stall recovery have synthetic
coverage. ROMs, generated code, captures, traces and saves remain local.
