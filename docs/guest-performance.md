# Guest runtime profiling

The [issue #89 follow-up](#remaining-overhead-issue-89) measures the remaining
observer, bus and policy costs after the diagnostic cache. The older results
below remain historical observations.

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
with bouncing on. At that point the remaining bounce-time lookup included the
separate `SNESRECOMP_APU_HANDOFF` option; it was not yet cached because the
existing bridge tests changed that timing policy within one process. See the
later process-configuration cache result below.

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

## Remaining overhead: issue #89

Investigation date: 2026-11-17. Source is the issue's audit baseline
`e18a4f753f54ed8bedf015f7bd634d26f6fc4241`, freshly built in the issue worktree.
Production game source, generated code, bus behavior and policy settings were
unchanged during discovery. The framework pin and guarded patch order are those
in `tools/setup.py`; this includes the diagnostic cache and transition patch.
The supplied USA ROM was rehashed and matched the SHA-256 above.

### Build and measurement identities

The control executable SHA-256 is
`4f89b91ef6677503b88e66528fca54602ef96340dda3c065f11b68883cf59c1a`.
The discarded observer-filter executable is
`588c50d0ad5b57dab3e0516928b3bd3f607fe93d41ab70e12dbfb2d3ed1ac6f7`.
Both use GCC 15.2.0 MinGW x64, C Release flags `-O3 -DNDEBUG -g`, C++ Release
flags `-O3 -DNDEBUG`, static SDL 3.4.10 and the pinned launcher UI
`3f1d6f122a58823614abce14f5b86f4d165a629c`. `KDC_RELEASE_DEFAULTS=ON`;
the guarded experimental state options are enabled, APU-boundary-sync is off.
The control was preserved before changing the local framework. The candidate
recompiled only its bridge and relinked the same build. Local build sidecars
record executable, game source, patch, framework diff, CMake cache and Ninja
file hashes. Harness revision metadata is explicitly separate from binary
provenance; a retained executable without a sidecar is marked unknown.

Hardware is the Ryzen 9 3900X, 12 cores/24 logical processors, on Windows
build 28120 (`platform.platform(): Windows-10-10.0.28120-SP0`). No affinity was
imposed. The shot script, zero initial SRAM, developer start, HD/widescreen,
worker count, audio and dummy SDL settings match the method above. Guest APU
execution remains enabled. All campaigns use host frames 350–1100, including
the APU tail in `RtlRunFrame`, and separate fresh processes. No instruction
tracing, sampling, captures or concurrent benchmarks run during timings.

WPR again failed with `0xc5585011`; these are not ETW/on-CPU measurements.
The available Ubuntu WSL kernel is `6.6.87.2-microsoft-standard-WSL2`, has no
`perf` executable, and a direct userspace `perf_event_open` task-clock probe
returned `EINVAL`. Native Linux and Steam Deck on-CPU evidence remains open.
The Windows sampler was built with `gcc -O2 -Wall -Wextra -Werror` and the
libraries in the reproduction command above. It retains the wall-occupancy
limitation described there.

### Attribution after the diagnostic cache

Three separate sampled processes per bounce policy produced these exclusive
subsystem groups. These percentages partition the guest stacks, not CPU time.

| Policy | Guest samples | 65816/bridge | APU/SPC/DSP | Beam/bus/other | DMA/HDMA | Generated |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Bounce off | 4,467 | 77.48% | 16.12% | 5.44% | 0.96% | 0% |
| Bounce on | 4,736 | 79.01% | 14.63% | 5.66% | 0.51% | 0.19% |

No stacks reached the 96-frame truncation limit. All six processes sampled the
requested window. Optimized DWARF attribution resolves the suspended leaf
instruction as-is, and external-leaf callers at their return address minus one.
Inline chains remain visible. Source lines in optimized code are approximate;
the observer loop was also checked against the matching disassembly.

| Leaf or source region | Bounce off | Bounce on |
| --- | ---: | ---: |
| `_interp_run_core`, including its inline helpers and observer scan | 29.57% | 29.54% |
| Observer scan within that core (lines 2252–2256) | 4.45% | 3.95% |
| `bridge_bus_read` | 8.84% | 8.61% |
| `cpu_read8` | 7.75% | 7.69% |
| `cart_getRomPtr` | 6.81% | 6.14% |
| `RomPtr` | 3.83% | 3.61% |
| `cpu_sram_offset` | 2.31% | 2.07% |
| `interp816_doOpcode` + `interp816_runOpcode` leaves | 5.48% | 5.47% |
| CRT `getenv` leaves | 0.31% | 3.25% |

The observer row is a subset of the core row. The five mapped-read rows total
29.53%/28.12% in disjoint leaves, including inlined checks. They do not measure
removable memory-map overhead: the bridge charges bus timing and read epochs,
handles APU-port synchronization, and preserves cartridge/open-bus behavior.
Writes did not rank among these leading leaves. No bus fast path was promoted
from this evidence.

Inclusive counts overlap: `interp816_runOpcode` is 32.62%/30.45%, `cpu_read8`
18.18%/17.53%, and `bridge_bus_read` 15.31%/14.93%. These include callees and
must not be summed with each other or with their leaf counts. Beam, DMA and
APU samples nested under the interpreter belong to their innermost recognized
subsystem. Twenty/eight guest samples were outside the beam driver; the frame
boundary APU synchronization and its inlined diagnostic/policy calls remain
in the denominator. Inlining can leave that boundary work in the “other” group.

The bounce-on `getenv` samples split into 75 at `SNESRECOMP_APU_HANDOFF`
(1.58% of guest samples), 74 at `SNESRECOMP_YIELD_STACK_DIAG` (1.56%), and five
in frame-boundary APU configuration. Bounce-off has 14 frame-boundary samples.
There are no remaining sampled `SNESRECOMP_WLOG_STATE` lookups. Thus the
remaining CRT cost is not solely the handoff setting. The yield diagnostic
is now queried after its frame-5390 gate. Handoff then still changed within
one process in the real bridge suite, so static caching required an explicit
probe path rather than changing that suite's contract.

Synchronization appears in source-attributed SDL/APU lock/unlock leaves;
these are nested work, not an additional subsystem percentage. The samples
do not establish contention or a synchronization bottleneck with audio output
off. Generated entries remain too sparse to infer translated-instruction
coverage. There is no justification here for recovering or promoting guest roots.

### Observer candidate and retention decision

Before implementation, the target was declared as at least 5% lower mean guest
time with consistent paired results. The local candidate adds an 8 KiB bitmap
indexed by the low 16 bits of the guest PC. Negative lookups skip the scan;
possible matches still use the original ordered 256-slot table and full
normalized key. Bank collisions produce extra scans, never skipped callbacks.
Registration, replacement and clearing maintain the bitmap. All existing
observers and CPU synchronization/redirect handling remain in place.

Two discarded warmups per build/policy preceded six AB/BA pairs per policy.
Each number below is an individual unsampled process mean, in ms/update.

| Policy/build | Individual means | Mean | Sample SD |
| --- | --- | ---: | ---: |
| Off/control | 6.926, 6.220, 6.232, 6.859, 8.081, 7.748 | 7.011 | 0.768 |
| Off/filter | 7.326, 5.229, 6.464, 6.620, 5.499, 6.950 | 6.348 | 0.822 |
| On/control | 7.398, 6.051, 5.575, 7.189, 6.292, 8.243 | 6.791 | 0.991 |
| On/filter | 6.866, 5.399, 6.634, 6.587, 7.154, 7.452 | 6.682 | 0.708 |

The ratio of means suggests 9.46% off and 1.61% on, but both policies lose two
of six pairs. Paired changes range from a 5.78% regression to a 31.95% reduction
off, and a 19.00% regression to a 10.78% reduction on. Run-mean coefficients of
variation are 10.6–14.6%. This fails the retention gate: the observer bitmap is
**not retained**, and no production runtime patch or native-promotion change
is part of this work.

An earlier six-run control-only campaign measured 5.113–5.677 ms off and
5.525–6.042 ms on (means 5.408/5.678). Its lower values, despite the identical
executable and settings, further rule out a historical-regression claim.
Separate sampler-only control runs measured 6.051–6.156/6.394–6.694 ms;
their aggregate means are approximately 13.1%/15.6% above that earlier timing
campaign. This is a sampler-perturbation check confounded by between-campaign
variation, not a calibrated overhead correction. Sampled times are never used
to accept the candidate.

`process.json` records Windows `GetProcessTimes` CPU seconds separately from
elapsed process seconds. Those cover startup and all host stages through exit,
not just the guest window. They are not substitutes for on-CPU sampling or
guest-stage CPU time. The paired campaign's whole-process means are:

| Policy/build | CPU seconds | Elapsed seconds |
| --- | ---: | ---: |
| Off/control | 10.635 | 12.818 |
| Off/filter | 9.846 | 11.830 |
| On/control | 10.482 | 12.496 |
| On/filter | 10.234 | 12.447 |

Deadline-wait counts were zero. SDL dummy, HD off and one
worker control the renderer for these guest-stage measurements; no display,
GPU, audio-output or end-to-end speedup is claimed.

### Repeating the follow-up

Build a fresh guarded executable using `tools/setup.py --release-defaults`
with a native Windows CMake and, optionally, `--sdl-source` pointing at an
existing SDL 3.4.10 source tree. Reconfigure C Release flags to
`-O3 -DNDEBUG -g`, rebuild, then preserve the control executable before a
candidate rebuild. Do not label old binaries with the harness checkout's revision.
An optional `<executable-stem>.build.json` alongside each executable records
build provenance and must include a matching `sha256`; mismatches fail.
Without it the report labels build provenance unknown.

```powershell
# Unsampled acceptance campaign: two warmups, six AB/BA pairs per policy.
python tools/profile_guest.py --executable <candidate.exe> --baseline <control.exe> --repeat 6 --modes timing
# Separate occupancy/source attribution; use matching MinGW tools.
python tools/profile_guest.py --executable <control.exe> --repeat 3 --modes sample --sampler local/guest-sampler.exe --nm <mingw-nm.exe> --objdump <mingw-objdump.exe> --addr2line <mingw-addr2line.exe>
# Separate regression campaign, optionally including sampled state checks.
python tools/profile_guest.py --executable <candidate.exe> --baseline <control.exe> --modes sample validate --sampler local/guest-sampler.exe --nm <mingw-nm.exe> --objdump <mingw-objdump.exe> --repeat 1 --validate --full-state
python tools/profile_guest.py --executable <candidate.exe> --baseline <control.exe> --scenario orbit --modes validate --validate --full-state
```

`--bounce 0` or `--bounce 1` restricts a campaign. `--warmup` changes the
discarded count. Full-state validation adds one save command in the existing
idle tail, compares its serialized state byte-for-byte against the same bounce
policy, and compares every full WRAM dump in the timed frame interval across
all policies separately from CPU/CRC/SRAM traces.
Framedump indices are one lower than host timer indices. This validates the
serialized state format, not every unsaved internal runtime field. No snapshots
or frame dumps are produced by timing or attribution-only processes.

Local evidence directories: control timing `local/guest-profile-wuzyhgm_`,
control attribution `local/guest-profile-gdwdc30x`, paired candidate timing
`local/guest-profile-qspwndux`. Local experiment scripts, binaries and raw
captures remain ignored. The public changes improve reproducibility and
attribution; they do not claim a runtime performance improvement.

### Follow-up validation and limits

- Nine profiling-tool tests pass, including incomplete evidence, source/inline
  attribution, CPU accounting after process exit, executable provenance and
  policy-specific snapshot comparisons. The project suite ran 118 tests with
  47 optional checks skipped (including checks requiring the default local
  framework path or opt-in ROM/device settings). The real framework checks
  below were run separately with an explicit framework path.
- Framework interpreter: 89/89; bridge: 449/449; tier-2 capture: disabled,
  lifecycle and normal modes pass. The local observer-contract wrapper passes
  723 total checks before and after the filter: the existing bridge checks
  plus null/duplicate/mirrored registration, distinct-bank collisions,
  replacement, clearing and capacity checks. Existing bridge cases cover
  composed observers, CPU synchronization, redirects, nested AOT/LLE,
  interrupts and mutable handoff/APU accounting. The diagnostic-cache probe
  also passes for absent, empty and enabled `SNESRECOMP_WLOG_STATE`.
  Redirecting the upstream PowerShell runner's output caused that shell to
  stop on its expected BRK diagnostic; equivalent GCC commands were run via
  Python subprocesses with exit codes checked instead.
- Shot and orbit each pass eight separate validation processes: two builds,
  two policies and two repetitions. Per-frame CPU/WRAM CRC traces, CPU-clock
  frame logs, all 751 full WRAM dumps and final SRAM agree across policies.
  Each complete serialized checkpoint matches the control for its own policy.
  Four additional sampled shot processes match the unsampled CPU/WRAM/SRAM
  reference, establishing sampler determinism in these recorded fields.
- The first attempt to compare entire snapshots **across** policies failed
  for the unchanged control in both shot and orbit at byte 305947. In the
  shot checkpoint that is `RtlRollbackResidue.cpu.host_return_valid` (0 vs 2).
  All other differing payload bytes are also within the execution residue;
  the hardware-state bytes before it match. The trailing checksum differs as
  expected. The tool now keeps per-policy snapshot references while retaining
  cross-policy CPU/WRAM/SRAM comparisons; it does not normalize away differences
  within a policy or claim raw cross-policy snapshot equality.
- `validate_high_refresh.py --baseline-bounce 0 --rates 120` passes against
  the preserved control: CPU/CRC/frame logs, SRAM and full WRAM agree;
  default images match. The two 120 Hz runs produced 54/55 camera frames and
  29/25 object frames with distinct intermediate images, with 95/98
  interpolated object draws. These wall-scheduled image counts qualify the
  path exercised, not its performance. A later repeat in
  `local/high-refresh-validation-dhfvmlxt` passed state/SRAM/full-WRAM
  comparisons through `hz-120`, but failed that case's intermediate
  object-image assertion (zero distinct intermediate object-motion frames).
  It did not reach the second 120 Hz case. This is the same class of
  wall-scheduled presentation failure recorded in the earlier investigation;
  the passing run does not establish a consistently passing image gate.

Passing shot and orbit full-state evidence is in `local/guest-profile-cgxyyv82`
and `local/guest-profile-120wzokr`; the initial snapshot-comparison failures are
preserved in `local/guest-profile-tqb_9b13` and `local/guest-profile-hjgvi92y`.
The high-refresh result is `local/high-refresh-validation-unayjfj7`.
There is no candidate-induced first divergence in the compared fields.
This remains host-to-host regression evidence. The existing host-15/reference-16
compatibility mismatch is separate; no new independent emulator equivalence,
audio fidelity, all-hole/CPU-preview coverage, native Linux or Steam Deck
performance is established. Those paths and on-CPU qualification remain open.
The rejected candidate is preserved only under ignored `local/observer-filter`
with its local patch and contract probe. The local framework was restored by
reapplying the guarded bridge patches at the pin; its complete diff SHA-256
matches the pre-experiment control, and its build was restored as well.


## Process-configuration cache measurement

The follow-up candidate caches the process-scope `SNESRECOMP_APU_HANDOFF`
lookup for production bridge builds and caches the game-side `KDC_HD`,
`KDC_HD_SOURCE` and `KDC_HD_STATS` settings. The ordinary tier-2 bridge test
build retains live handoff lookup because that suite deliberately changes the
variable within one process; `SNESRECOMP_APU_HANDOFF_CACHE_PROBE` selects the
production caching behavior for the regression probe. The probe passes only
when `SNESRECOMP_WLOG_STATE` and `SNESRECOMP_APU_HANDOFF` are each read once,
and the HD policy probe passes only when each HD process setting is read once.
Both probes failed before the change and pass after it.

A paired bounce-on shot campaign compared the preserved pre-change executable
with the candidate using one discarded warmup, two timed repetitions and two
sampled repetitions per build. Individual unsampled guest means were
3.977/3.248 ms for the control and 3.559/3.311 ms for the candidate; process
CPU medians were 6.977 s and 6.820 s. The ordering effect is large enough
that these four runs do not establish a whole-frame timing improvement. The
attribution result is direct: guest `external:getenv` leaves fell from
61/1974 samples (3.09%) to 6/1846 samples (0.325%). Caller attribution over
all sampled stacks removed the 59 `_interp_run_core` handoff lookups and the
three `KdcHdScale` lookups seen in the control. The candidate's few remaining
samples were frame-boundary/configuration or startup diagnostic lookups, not
the repeated bridge-entry handoff scan.

All four validation processes and all four sampled processes matched CPU/WRAM
traces, frame logs and final SRAM. Raw evidence remains in ignored
`local/guest-profile-slc9fk_b`. This measurement supports removing the hot
lookup; it does not prove a statistically significant mean-frame improvement
from two repetitions.
