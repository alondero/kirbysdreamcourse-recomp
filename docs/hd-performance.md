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
