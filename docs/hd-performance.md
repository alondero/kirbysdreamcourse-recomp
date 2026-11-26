# Residual HD renderer costs

Issue [#88](https://github.com/alondero/kirbysdreamcourse-recomp/issues/88),
2026-11-17. Discovery baseline: `e18a4f753f54ed8bedf015f7bd634d26f6fc4241`.
The investigation leaves production rendering and the guarded patch stack
unchanged. The deliverable is cost attribution and a measured decision about
whether a small optimization is justified.

Status: the tooling and probe validation are ready. The controlled
timing matrix is waiting for unrelated capture/audit work on the workstation
to finish; no optimization decision or speedup is established yet.

## Frozen experiment

Before considering an implementation, the acceptance threshold was declared:
at least **0.5 ms and 10%** less median warm renderer elapsed time, with no
presentation-interval p99 regression above 5%, identical images, and unchanged
CPU/WRAM/SRAM traces. Renderer time means the sum of raster capture, surface
acquisition, composition and submission, excluding guest execution. This is a
wall-time gate, not an assertion that those timers measure CPU busy time.

The Windows x64 workstation has a Ryzen 9 3900X (12 cores / 24 logical
processors), RTX 3080, NVIDIA 630.20, GCC 15.2.0 MinGW and Release
`-O3 -DNDEBUG` builds. Dependencies are the framework at
`147b55e252dcb3e7680a99c7c2f50e7183891844`, the existing guarded patch stack,
recomp-ui at `3f1d6f122a58823614abce14f5b86f4d165a629c`, and SDL 3.4.10
(`SDL-release-3.4.10-0-g8e37db5e7`; archive SHA-256
`12b34280415ec8418c864408b93d008a20a6530687ee613d60bfbd20411f2785`).
Framework PolyForm Noncommercial, SDL zlib and existing launcher notices remain
applicable; no new renderer dependency is introduced.

Frozen executable SHA-256:

- Uninstrumented: `d636409405bcfa94eef539aa0d5c200a673063979c02e0db2e05279d84ed966c`.
- Diagnostic: `45d06798141c6b6c7a4fbdf44fddac8f2a70526da9716a3aa33f298b70c8abf0`.

The USA ROM was verified before building/replaying: 1,048,576 bytes, SHA-256
`0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.
ROM-derived code, screenshots, traces, saves and recordings remain in ignored
`local/`; only aggregate timing data belong in this document.

The fixed workload is `benchmark_hd.py`'s `shot` / `orbit`, developer start
`normal:1:1`, frames 350–1100 inclusive, HD off/on, 1 or 12 total rendering
threads (including the calling thread), widescreen, a 1536×896 output window,
OpenGL 3.3, no shader, VSync off, host audio output off, and unpaced fresh
processes with fresh config/SRAM. Guest APU execution remains enabled. The
harness now explicitly selects the guarded APU beam/handoff/absolute-preanchor
switches and native bouncing, rather than inheriting shell settings or relying
on a development binary's defaults. These explicit settings differ from older
versions of `benchmark_hd.py`; historical numbers are not paired controls.

Each timing matrix uses two discarded warmup replays of each case, then six
HD-off/on pairs with alternating AB/BA order. No screenshots, state tracing,
orbit diagnostics or GPU queries are enabled during those timing runs.
Individual results, executable/DLL hashes, harness hash, runtime settings,
per-frame nearest-rank p25/p50/p75/p95/p99, run medians and variation are retained
in local `metadata.json`, `results.json` and `summary.json`. Run medians use the
usual midpoint for even counts; per-frame percentiles use nearest rank.

## Attribution probes

The optional patches in `tools/diagnostics/hd-costs-{game,framework}.patch`
are **discovery-only**. They are absent from `setup.py`'s production patch list.
Apply them only to disposable copies after the ordinary guarded patches; they
do not edit generated game code. They buffer samples until the replay ends.

The CPU probes time PPU HD line composition and its nearest seed; orbit
preparation, terrain/scenery updates and banded drawing; previous-worker
retirement, calling-thread band work and completion waits; the HD callback,
native downsample, HD surface copy, overlay freeze copy and staging copy; and
SDL unlock/submission/present or OpenGL upload/submission/swap calls.

These scopes are nested and **must not be summed**: terrain/scenery are part
of preparation, pool work/waits are part of banded drawing, orbit work is part
of the callback, and seed time is part of the PPU HD line. Worker waits are
calling-thread elapsed time, not aggregate worker CPU time or a scheduler
contention measurement. The calling thread's band time is not the sum of all
workers' band work. Raster minus the HD-line scope still includes the native
PPU, tile recording/lookups and capture hooks.

OpenGL diagnostic runs issue three `glQueryCounter(GL_TIMESTAMP)` queries
around texture upload and draw submission. Results are retrieved **after**
the timed replay, with no query-result waits during play. `gpu.csv` separates
queued upload and draw intervals, excluding swap, desktop composition and
scanout. Timestamp intervals can include GPU queue idle time while the CPU
feeds commands; they are not hardware busy counters. CPU upload/swap wall
times remain separate. No Direct3D GPU timing or paced-device claim follows
from these OpenGL queries.

Orbit's first composed presentation, frame 501, is reported separately from
warm orbit at 502–1100. The 350–1100 orbit window also includes the preceding
normal PPU view; its whole-window composition mean dilutes the orbit-only cost.
Extra interpolated presentations consume completed scene records and perform
no guest update. The harness reports update/presentation counts and rates
separately; the unpaced experiment has one presentation per update and does
not measure high-refresh pacing behavior.

## Reproduction

Build an ordinary Release executable in a disposable source checkout using
`tools/setup.py`, retaining its executable and runtime DLLs as the frozen
baseline. Apply the two probes to that checkout and its patched framework:

```powershell
git apply --ignore-space-change tools/diagnostics/hd-costs-game.patch
git -C local/guarded-framework apply ../../tools/diagnostics/hd-costs-framework.patch
cmake --build local/guarded-build --parallel 8
```

The framework patch path is relative to `local/guarded-framework`; use an
absolute path if invoking from another directory. `--ignore-space-change`
handles a Windows source checkout's CRLF context. Use a native Windows CMake
(see `tools/find_native_cmake.ps1`). Retain the rebuilt diagnostic executable
separately. Do not rerun setup after applying the probes: setup reconstructs
the framework from the production patch list.

From the reporting checkout, repeat this timing command for both scenarios
and both thread counts, using the **uninstrumented** frozen executable:

```powershell
python tools/benchmark_hd.py --executable path/to/frozen.exe --rom path/to/private.sfc --scenario shot --hole normal:1:1 --frames 1100 --profile-start 350 --threads 12 --backend opengl --window-size 1536x896 --display --warmup 2 --repeat 6 --hd 0 1 --no-captures
```

Run the **diagnostic** executable separately with `--diagnostic-costs
--orbit-profile --repeat 2 --no-captures` and the same other settings. Diagnostic
timings include probe overhead and are not substitutes for unsampled timings.
For image/state validation, use separate deterministic replays:

```powershell
python tools/diagnostics/validate_hd_costs.py --baseline path/to/frozen.exe --diagnostic path/to/diagnostic.exe --rom path/to/private.sfc
python -m unittest discover -s tests -p 'test_*.py'
```

The validation helper uses the dummy/software presenter, both scenarios,
HD off/on and 1/12 threads. Each case compares a baseline and two diagnostic
replays through frame 1100, including the frame-800 PPM, per-frame CPU/WRAM-CRC
trace, CPU clock/resume-PC log and final SRAM. It identifies the first state
trace mismatch, or the first sampled image mismatch. It does not compare every
video frame or establish new reference-emulator fidelity for HD subpixels.

## Validation and open evidence

Both Windows Release builds succeed. The complete repository test suite passes:
116 tests, 23 opt-in/environment skips. This includes the existing HD tile,
cache, terrain, orbit, HUD, colour-math/window, interpolation and native-centre
fixtures, plus benchmark order, warmup exclusion, tails and diagnostic-window
tests. Both diagnostic patches apply after the normal pinned patch stack;
neither is added to the production build recipe.

A functional OpenGL diagnostic replay produced 1,100 finite, nonnegative GPU
timestamp records, including 751 profile-window records, one orbit-entry
record and 599 warm-orbit records. The CPU probe windows have the same counts.
This smoke replay ran while unrelated work was active, so its durations are
excluded from performance evidence. Controlled OpenGL cost attribution remains
pending the quiet-window matrix.

Two separate 24-replay matrices pass: the original benchmark's development
timing defaults, then the explicitly guarded defaults used by the updated
harness. Each matrix covers both scenarios, HD off/on, 1/12 threads, and a
baseline plus two diagnostic replays. Every corresponding frame-800 capture,
per-frame CPU/WRAM-CRC trace, clock/resume-PC log and final SRAM matches.
Cross-HD and cross-thread guest traces also match within each scenario;
changing thread count leaves the screenshots identical. First observed
divergence: none. Captures are 330×224 native and 1320×896 HD in the fixed
window/aspect configuration.

These comparisons use the frozen renderer, not a new emulator oracle. They
do not qualify all holes, menus/fades, mod packages, an independently forced
interpreter run, every video frame, paced/high-refresh behavior, Linux or
Steam Deck. No slower physical target is available in this session; one
worker on the 3900X is a worker-scaling control, not a substitute for one.
Direct3D GPU time, scheduler/on-CPU attribution and end-to-end display latency
remain unmeasured.

## OpenGL composition evaluation

The evaluation uses the guarded build with the GPU compositor patch. The paired
control runs the same binary with the relevant GPU path disabled. This isolates
the compositor from compiler, SDL and guest changes.
[Issue #101](https://github.com/alondero/kirbysdreamcourse-recomp/issues/101)

Normal-view desktop OpenGL composition uses the native frame, per-line
palette and colour-math state, and sparse validated Mode 1 tile records. The
PPU still checks native priority and colour before emitting a record. The
GPU samples cached indexed or RGBA tiles from an atlas, resolves priority and
colour math with integer operations, and provides the result as a texture to
the presenter. Pixels with no record use the native frame. Mode 7 lines
requiring CPU subpixel rendering and lines that exhaust the record buffer
use the existing CPU pixels. SDL renderers, shader presets, run-ahead and
the practice overlay use the CPU compositor. Frame dumps and the host OSD
can read back the composed pixels. Set `KDC_HD_GPU=0` for a CPU control.

Desktop OpenGL orbit composition keeps the entire orbit pipeline GPU-resident
and hands its completed RGBA8 texture directly to the OpenGL presenter without
per-frame readbacks or full-frame uploads. It bins projected terrain faces into
16×8 screen bins to resolve exact painter-order terrain colour, raw R32F depth
for ground decals, and a bijective `GL_DEPTH_COMPONENT32F` depth buffer for
Early-Z scenery (`GL_EQUAL` at depth 0) and world-space upright sprites
(`GL_GEQUAL`). Animated terrain/mask changes upload only their dirty row range,
scenery frames use a 48-slot GPU texture LRU cache, sprite/HUD tiles use a
persistent 32×32 GPU atlas with per-frame 8×8 fallback scratch slots, and
per-frame dynamic buffers double-buffer across two ring slots. Shaders and FBO
attachments prewarm once when the OpenGL context initializes. It requires
OpenGL 4.0 or OpenGL 3.3 with `ARB_gpu_shader_fp64` and `ARB_gpu_shader5`.
[Khronos double-precision extension](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_gpu_shader_fp64.txt),
[Khronos shader extension](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_gpu_shader5.txt)

Both normal-view and orbit GPU composition are enabled by default on desktop
OpenGL; set `KDC_HD_GPU=0` or `KDC_ORBIT_GPU=0` for CPU controls. Unsupported
contexts, non-OpenGL renderers, shader presets, run-ahead and the practice
overlay fall back to the CPU paths.

### Reproduce

Use a Windows Release build from `tools/setup.py` with the pinned framework,
launcher and SDL described above. The workload is hole `normal:1:1`, 12
rendering threads, a 1536×896 OpenGL window, VSync off and the benchmark's
deterministic inputs and guarded runtime settings. The shot profile uses
frames 350–1100. The orbit profile uses 502–1100 to exclude orbit entry.

```powershell
$exe = "local/guarded-build/KirbyDreamCourseRecomp.exe"
$paired = @("--executable", $exe, "--baseline", $exe, "--backend", "opengl",
            "--window-size", "1536x896", "--threads", "12", "--display")
python tools/benchmark_hd.py @paired --scenario shot --gpu-hd --warmup 1 --repeat 4 --hd 0 1 --no-captures
python tools/benchmark_hd.py @paired --scenario orbit --profile-start 502 --gpu-orbit --warmup 1 --repeat 4 --hd 1 --no-captures
python tools/benchmark_hd.py @paired --scenario shot --gpu-hd --hd 1 --repeat 1 --verify-presents
python tools/benchmark_hd.py @paired --scenario orbit --profile-start 502 --gpu-orbit --hd 1 --repeat 1 --verify-presents
```

The harness keeps the timed runs free of readback probes. The separate
`--verify-presents` replays compare per-presentation pixel CRCs, per-frame
CPU registers and WRAM CRCs, and a frame-800 PPM. The first mismatching trace
line is reported if a comparison fails. The ROM-free OpenGL fixtures run the
same terrain, sprite, HUD and tile scenes through both renderers and compare
whole images.

```powershell
cmake --build local/guarded-build --target kdc_hd_gpu_fixture kdc_orbit_gpu_fixture
./local/guarded-build/kdc_hd_gpu_fixture.exe
./local/guarded-build/kdc_orbit_gpu_fixture.exe
```

### Results

Windows x64 Release on a Ryzen 9 3900X and RTX 3080 with NVIDIA 630.20,
MinGW GCC 15.2.0 and SDL 3.4.10. The measured executable SHA-256 is
`1ead386d08eb6dc022472bb7874372d678cbc8f25364e8add420759322c73d63`.
Each comparison uses one discarded warmup per case and four measured
pairs in alternating order. Values are medians of run means in
milliseconds, with the range of measured run means in brackets. Renderer
time sums raster capture, surface acquisition, composition and present
submission. It measures host wall time.

| Workload | CPU renderer time | GPU renderer time | Change | Median run interval p99, CPU → GPU |
| --- | ---: | ---: | ---: | ---: |
| Normal shot, HD on | 8.02 [7.57–8.60] | 4.28 [4.20–4.82] | −46.6% | 15.03 → 10.20 |
| Normal shot, HD off control | 1.33 [1.21–1.49] | 1.33 [1.20–1.35] | Overlapping ranges | 6.98 → 6.33 |
| Warm orbit, HD on | 5.66 [5.64–5.67] | 3.36 [3.10–3.52] | −40.7% | 12.84 → 11.66 |

For normal shot with HD on, raster capture fell from 5.61 to 3.86 ms, compose
from 0.74 to 0.34 ms, and upload-present from 1.69 to 0.08 ms. For warm orbit
with HD on, compose fell from 2.07 to 1.70 ms and upload-present from 1.98 to
0.09 ms (replacing the earlier readback-based terrain trial that regressed warm
orbit from 5.02 to 9.15 ms). The HD-on renderer-time ranges did not overlap in
either scenario.

The GPU fixtures matched 27 PPU fields and 14 complete HD orbit fields,
including indexed priority, flips, native fallback, RGBA alpha, colour windows
and colour math, orbit occlusion, scenery, ground decals, world bodies,
zoomed-out supersampling, native/edge HUD and semi-transparent HD pack RGBA
blending. Separate deterministic shot and orbit replays matched every
presentation CRC and guest-state record through frame 1100, and both frame-800
PPMs matched byte for byte. The combined default-on OpenGL settings (both
normal-view and orbit GPU composition active) also matched the CPU control
through all 1,101 presentations and 1,100 guest-state records across orbit
entry and a scripted reset to normal view at frame 1000. The Python suite ran
140 tests with 110 passes and 30 opt-in or environment skips.
Benchmark JSON and ROM-dependent captures are kept in ignored `local/`.

These results use the CPU compositor as the HD oracle. Linux, Android,
other GPU drivers, external artwork packs, shader presets and paced
high-refresh performance still need qualification.
