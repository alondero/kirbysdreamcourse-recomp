# Guest runtime profiling

Issue [#63](https://github.com/alondero/kirbysdreamcourse-recomp/issues/63),
2026-11-15. Source baseline: `18bd9ed`. Framework:
`147b55e252dcb3e7680a99c7c2f50e7183891844` with the guarded patch stack.
The verified 1 MiB USA ROM has SHA-256
`0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.

## Scope and method

The host's `stage=guest` timer includes `before_run_frame`, `RtlRunFrame`,
and frame bookkeeping. `RtlRunFrame` contains the beam driver, interpreted
and bounced 65816 execution, and APU synchronization, including a final
APU catch-up **after** `GameRunOneFrame` returns. Sampling only the beam
driver would omit that tail. Raster capture, composition and presentation
have separate host timers.

Tests use Windows x64, Ryzen 9 3900X, GCC 15.2.0 MinGW (POSIX/SEH), SDL dummy video/audio,
HD off, widescreen on, one rendering worker, audio output disabled,
unpaced fresh processes, zero initial SRAM and developer start `normal:1:1`.
Disabling host audio output does not disable guest APU/SPC/DSP execution.
The `shot` input from `benchmark_hd.py` runs through frame 1100; the timed
window is frames 350–1100 inclusive (751 frames). All three guarded APU
switches are explicitly enabled. The harness removes inherited game/SDL
environment settings and creates a new config and save directory per run.

Timing runs have host profiling enabled, with no sampler, state tracing,
screenshots or bounce logging. Three repetitions alternate build and
bounce-policy order. Sampling and validation use separate processes;
sampled-run times are not performance measurements. The executable's
SHA-256, source revision, platform, scenario, window, individual means,
maxima and counts are recorded in local `results.json` files.

WPR failed with error `0xc5585011` (system performance profiling policy).
The fallback, `tools/windows_guest_sampler.c`, attaches to the host's
oldest/main thread, suspends it, obtains its x64 context, unwinds through
`StackWalk64`, and resumes it before writing output. The sampling interval
jitters from 1 to 3 ms. PE unwind tables and the executable's retained COFF
symbols resolve game/framework frames; DbgHelp resolves external exports.
Baseline flags are `-O3 -DNDEBUG`; candidate C flags add `-g` without changing
optimization. No source or generated-code instrumentation is used for samples.

These are **main-thread wall-occupancy samples**, not ETW on-CPU events or
hardware counters. They can include descheduling or waits. The unpaced,
dummy-backend workload bounds that limitation but does not remove it.
Percentages are statistical observations, not exact CPU percentages.
Inline functions remain attributed to their containing function. A stack
counts as guest work only if it contains `RtlRunFrame`; percentages use that
denominator. The first recognized subsystem from leaf toward root owns the
sample, so APU/DMA/beam work nested in the interpreter is not counted twice.
Inclusive function counts overlap and must not be added.

## Results

Each cell lists the three unsampled run means, followed by their mean.

| Build | Bounce off (ms/frame) | Bounce on (ms/frame) |
| --- | --- | --- |
| Baseline | 5.442, 5.430, 5.151 → **5.341** | 5.300, 5.786, 5.540 → **5.542** |
| Diagnostic cache | 4.777, 5.445, 4.904 → **5.042** | 4.832, 4.963, 5.044 → **4.946** |

That is a 5.6% reduction in interpreter mode and 10.8% with bouncing enabled.
One interpreter pair regressed slightly; these three-run observations are
not a confidence interval or a portable speed guarantee. Individual-frame
maxima were noisy: baseline off/on 16.774/45.751 ms, candidate off/on
12.276/9.542 ms. No tail-latency improvement is established by those maxima.
The issue's earlier 5.6–5.9 ms figure is historical, not an invariant target.

The following mutually exclusive groups aggregate all three sampled runs per
build/policy. Beam clock/refresh helpers are separated even when called from
the bridge. All samples use the same host-frame window as the timing runs.

| Build/policy | Guest samples | 65816/bridge | APU/SPC/DSP | Beam/bus/other | DMA/HDMA | Generated entry/body |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Baseline, off | 4,748 | 80.67% | 12.83% | 5.98% | 0.53% | 0% |
| Baseline, on | 4,826 | 80.85% | 12.99% | 5.26% | 0.79% | 0.10% |
| Cache, off | 4,275 | 79.56% | 13.85% | 5.92% | 0.68% | 0% |
| Cache, on | 4,308 | 78.46% | 14.62% | 6.04% | 0.72% | 0.16% |

Baseline interpreter leaf samples include `_interp_run_core` (25.1%),
`getenv` (11.4%), `bridge_bus_read` (8.7%), `cpu_read8` (7.4%) and
`cart_getRomPtr` (5.6%). `interp816_runOpcode` is 29.6% **inclusive**, which
also includes memory/hardware work. With bouncing enabled, baseline `getenv`
leaf occupancy is 14.1%. After caching, it is 0% with bouncing off and 3.7%
with bouncing on. The remaining bounce-time lookup includes the separate
`SNESRECOMP_APU_HANDOFF` option; it is deliberately not cached here because
the existing bridge tests change that timing policy within one process.

The generated bucket includes entry guards and bookkeeping; its handful of
samples cannot establish how many translated game instructions executed.
The before/after data supports removing repeated diagnostic lookups, not a
claim that new AOT roots would recover the entire bridge share.

## Findings and bounded change

The baseline is dominated by the 65816 bridge and its memory-access/runtime
work, rather than SPC/DSP execution. Opcode execution alone is a smaller
part of that broad bucket: `_interp_run_core`, bridge bus reads,
`cpu_read8`, ROM mapping and runtime helpers also consume time.

An unexpected hot path is the CRT's `getenv`. Its sampled caller is the
`SNESRECOMP_WLOG_STATE` lookup at `_interp_run_core` entry. Beam scheduling
reenters the bridge many times per guest frame, so even a disabled diagnostic
repeatedly scans the Windows environment.

`patches/snesrecomp-bridge-diagnostic-cache.patch` caches this diagnostic
on first use, following the other cached bridge diagnostics. Both absence
and presence retain their meaning; an empty value still enables register
synchronization before interpreted instructions. This is process-scoped
configuration: changing this environment variable after the first bridge
entry is no longer supported. The game host has no live toggle for it.
The patch changes no guest instruction, timing, mapper, APU policy or
pre-opcode observer, and applies directly to the pinned framework revision.
Both ordinary setup and the native-experiment builder apply it.

## Interpreter versus AOT

`SNESRECOMP_LLE_BOUNCE=0` and `1` select policies in the same executable;
dispatch attempts do not measure translated instructions. Generated entry
guards can return to interpretation, and directly called generated routines
may also tier down. The current 52 dispatch entries and 49 compiled variants
therefore do not establish runtime coverage.

The current configuration still forces reset and NMI into LLE and leaves
the three indirect dispatch sites unresolved. Larger native roots might
avoid opcode dispatch, but would still need the mapped bus, beam deadlines,
interrupt timing, APU synchronization and game observers. The sampled bridge
share is not an estimate of removable AOT overhead. In particular,
`interp816_runOpcode` inclusive time includes bus and hardware calls.

This investigation takes the cheaper measured bridge optimization first.
There is no evidence here to remove guards or promote reset/NMI/indirect
dispatchers. Any later promotion needs instruction-level coverage, retained
`interp_bridge_add_pre_opcode_hook` observers from `course_sprites.c`,
`cpu_opponent.c` and `dev_start.c`, and deterministic parity before claiming
a speedup. Do not hand-edit generated code.

## Reproduction

Build with `tools/setup.ps1 -ReleaseDefaults` (or `tools/setup.py` with a
native Windows CMake). To retain line information with the same Release
optimization, reconfigure the build with
`-DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG -g"`, then rebuild. Preserve a baseline
executable from before the cache patch in a separate local directory.

```powershell
gcc -O2 -Wall -Wextra -Werror tools/windows_guest_sampler.c -o local/guest-sampler.exe -ldbghelp -lwinmm
python tools/profile_guest.py --executable local/guarded-build/KirbyDreamCourseRecomp.exe --baseline local/guest-baseline-build/KirbyDreamCourseRecomp.exe --repeat 3 --sampler local/guest-sampler.exe --validate
```

The `nm` and `objdump` tools must match the target; use `--nm` and
`--objdump` to select the MinGW binaries if MSYS tools shadow them.
Omit `--sampler` for portable timing/state comparisons. `--scenario still`
or `orbit` reuses the other deterministic benchmark workloads. A budget such
as `--max-guest-ms 5` makes the benchmark fail if any unsampled guest mean
exceeds that threshold; this is hardware-specific, not a universal CI gate.
All raw stacks, generated inputs, recordings, logs, saves and traces stay
under ignored `local/`.

```powershell
$env:SNESRECOMP_ROOT = (Resolve-Path local/guarded-framework).Path
python -m unittest discover -s tests -p test_bridge_diagnostic_cache.py -v
python -m unittest discover -s tests -p test_profile_guest.py -v
python tools/validate_high_refresh.py --rom "Kirby's Dream Course (USA).sfc" --executable local/guarded-build/KirbyDreamCourseRecomp.exe --baseline local/guest-baseline-build/KirbyDreamCourseRecomp.exe --baseline-bounce 0 --rates 120
```

The bridge regression links the framework's real bridge suite with a counter
around `getenv`. Against the original framework it fails on the second bridge
entry (exit 91); with the cache it passes for an absent, empty and enabled
flag. That suite also exercises nested interpreter/AOT transitions and
composed observers/redirects.

The private measurement directory is `local/guest-profile-id3_n0l3`.
An earlier sampler smoke test (`local/guest-profile-7hcq2rft`) established
CPU/WRAM/SRAM parity with and without sampling. Raw stack files are sufficient
to recompute the attribution with `summarize_samples`; no ROM or generated
source is needed for that step, but the matching local executable and its
symbols are required. The public report contains only aggregate timings,
counts and function names.

## Validation results

- Project suite: 104 tests, 22 skipped (opt-in ROM/device/runtime suites).
  The separate real-ROM replays below were actually run.
- Framework suite: 89/89 interpreter checks and 449/449 bridge checks pass,
  along with its tier-2 capture tests. The lookup-counter regression passes
  all three diagnostic settings against the patched real bridge suite.
- Three repetitions for each build and bounce policy: all 12 sampled runs
  covered frames 350–1100, with no truncated stacks or sampler failures.
- Eight separate validation processes (two repetitions × two builds × two
  policies) and all 12 sampled runs match the original bounce-off reference:
  per-frame WRAM CRC and CPU-register trace, CPU clocks/resume-PC frame log,
  and final SRAM. There is no first divergence in these recorded fields.
  These are frame checkpoints, not instruction-by-instruction traces or full
  hardware snapshots.
- `validate_high_refresh.py --baseline-bounce 0 --rates 120` passes on a fresh
  repeat (`local/high-refresh-validation-vduoxpcq`). Default and repeated
  120 Hz runs match CPU logs, CRCs, full WRAM dumps and SRAM against the
  original interpreter executable; default frame images also match. The two
  120 Hz runs have 56/57 camera frames and 45/33 object frames with distinct
  intermediate images, plus 108/99 interpolated object draws.

The first high-refresh attempt (`local/high-refresh-validation-5udfyvst`)
passed state/SRAM/WRAM comparisons but failed the intermediate object-image
assertion: zero frames in the checked interval had multiple distinct images.
The original baseline then passed the same validator against itself, and the
candidate repeat passed without a code change. These wall-scheduled image
counts depend on capture/presentation headroom; they are not deterministic
timing metrics. The initial failure remains recorded rather than being
treated as a clean first-pass presentation qualification.

Independent standards review found no findings. Spec review found no
implementation blocker, but identified the on-CPU and Steam Deck evidence
below as incomplete. This PR addresses the measured local overhead and
leaves issue #63 open for that remaining qualification.

## Limits

The parity reference for this change is the existing guarded interpreter,
not a new independent-emulator capture. Known reference discrepancies remain
as recorded in [compatibility](compatibility.md). No new reference-emulator
correctness, audio fidelity or full-game compatibility is claimed. Steam
Deck, Linux sampling, CPU-opponent previews, all holes and long recordings
remain unmeasured by this experiment. The Windows sampler is optional and
is not part of a release build or package.

API references: Microsoft's [thread context requirements](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadcontext)
and [StackWalk64 contract](https://learn.microsoft.com/windows/win32/api/dbghelp/nf-dbghelp-stackwalk64).
