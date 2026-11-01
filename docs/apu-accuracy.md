# APU timing and audio checks

Issue [#3](https://github.com/alondero/kirbysdreamcourse-recomp/issues/3)
tracks CPU/SPC ordering, instruction and timer phases, and DSP output. These
checks distinguish a queue contract from reference-emulator observations.
They do not certify hardware timing or sample-exact game audio.

## Pins and source provenance

The framework remains at `147b55e252dcb3e7680a99c7c2f50e7183891844`
(PolyForm Noncommercial). The differential reference is Snes9x 1.63 commit
`fae2fea08f74180759ef540ee94259213f503480`. Its SMP source carries the
Snes9x notices; `apu/bapu/dsp/SPC_DSP.cpp` carries Shay Green's LGPL-2.1-or-later
notice. The test links those components locally; it does not vendor their
source, replace the production runtime, or distribute a reference binary.
Existing notices and runtime licenses still apply.

The reference is exported with `git archive` at the exact pin before compiling,
so local instrumentation in a source checkout cannot affect this test.
The selected framework's working files are intentionally compiled, since they
include the project's runtime patches. The tool checks its base commit and
prints an APU source fingerprint. GCC/G++ 15.2.0 on Windows were used here.

## Reproduce the synthetic checks

After ordinary `tools/setup.ps1`, obtain the reference source locally:

```powershell
git clone --no-checkout https://github.com/snes9xgit/snes9x.git local/snes9x-source
git -C local/snes9x-source checkout --detach fae2fea08f74180759ef540ee94259213f503480
```

Build an isolated candidate using the same native Windows CMake and optional
SDL source arguments as setup; choose a fresh `-Name`:

```powershell
./tools/prepare_guarded.ps1 -Rom "Kirby's Dream Course (USA).sfc" `
  -CMake 'C:/path/to/native/cmake.exe' -Name apu-boundary -ApuBoundarySync
python tools/check_apu.py --framework local/apu-boundary-framework
```

The script builds temporary executables using `gcc -std=c11 -O2 -UNDEBUG`
and `g++ -std=c++17 -O2 -UNDEBUG -D__LIBRETRO__`. It tests both
`SNESRECOMP_APU_NATIVE=0` and `=1` with assertions enabled. No ROM is read.
The complete command lines are in `tools/check_apu.py`; compilation errors and
the first differing output record fail the command. To include it in unittest:

```powershell
$env:KDC_TEST_APU = '1'
$env:KDC_APU_FRAMEWORK = 'local/apu-boundary-framework'
python -m unittest discover -s tests -p 'test_*.py' -v
```

`KDC_APU_REFERENCE` optionally selects another checkout of the same reference
pin. Without `KDC_TEST_APU`, the dependency-free Python tests run and this
integration test is explicitly skipped.

## What the probes establish

| Probe | Coverage and observation |
| --- | --- |
| Timer reads | 144 combinations: three timers, eight tick positions, initial counters 0/3/15, and `MOV A,dp` / `MOV Y,dp`; two successive reads include clear-on-read and counter rollover. |
| Indexed port race | Change CPU input port 0 at phases 0 through 12 of `MOV A,dp+X; CMP A,dp+X; BNE`; both implementations return `$ff` before phase 5 and zero afterward. |
| Reference pause | Requested phases 5/6/7 already reach PC `$0204`, with no pending opcode phase, and overshoot by 3/2/1 SPC cycles. |
| Indexed store | `MOV dp+X,A` leaves the output port unchanged for four cycles and publishes on cycle five. |
| DSP | 360 scenarios of 256 stereo samples, including ENDX and ENVX: all eight voices, four BRR filters, zero/fractional/unit/max pitches, and noise-rate changes. |
| Resume | 17 cuts through timer reads, port reads, indexed writes and a branch; serialized APU/DSP/SPC and separately saved port scheduler replay identically. |
| Queue boundary | Due writes consume no SPC/DSP time, equal-time writes retain FIFO order, cycle budgets stop before future writes, and an earlier request cannot expose a later queued write. |

The reference repeats every matrix record across fresh executions. Host output
matches it both in full-span calls and one-cycle calls, in scalar and batched
APU modes. Every DSP scenario contains nonzero output. This isolates these
register writes and arithmetic from the desktop resampler and CPU scheduler.
It does not test simultaneous voices, echo, all ADSR/GAIN modes, arbitrary
within-sample writes, all SPC opcodes, physical hardware, or concurrent audio.

The indexed-race result is specifically an **emulator-model observation**.
The pinned reference implements `$74` in one uninterrupted `op_step`; its
`enter()` loop can overshoot a requested boundary. The probe records that
overshoot directly. A final-cycle comparison experiment may disagree with
this fixture without the fixture establishing the physical read phase.
No indexed-load or indexed-compare timing change is enabled by this work.

An independent source inspection of ares commit
`dc4f4659d49693ac91705389e6c37734c016bf16` provides a useful cross-check:
its opcode table maps both `$74` and `$f4` to the same indexed-read sequence
(operand fetch, idle, data load), and `$d4` includes a dummy load before its
store. Its SMP port read places the IO observation between two half waits.
That finer-grained model reinforces why an uninterrupted Snes9x comparison
cannot resolve where a CPU write falls within the physical port-read cycle.
This is source evidence only: ares was not built, run, or integrated, and the
different emulators' shared lineage limits their independence as hardware
evidence. See the pinned
[opcode table](https://github.com/ares-emulator/ares/blob/dc4f4659d49693ac91705389e6c37734c016bf16/ares/component/processor/spc700/instruction.cpp),
[instruction sequences](https://github.com/ares-emulator/ares/blob/dc4f4659d49693ac91705389e6c37734c016bf16/ares/component/processor/spc700/instructions.cpp),
and [port reads](https://github.com/ares-emulator/ares/blob/dc4f4659d49693ac91705389e6c37734c016bf16/ares/sfc/smp/memory.cpp).

Relevant reference code:
[SMP entry](https://github.com/snes9xgit/snes9x/blob/fae2fea08f74180759ef540ee94259213f503480/apu/bapu/smp/smp.cpp),
[indexed comparison](https://github.com/snes9xgit/snes9x/blob/fae2fea08f74180759ef540ee94259213f503480/apu/bapu/smp/core/oppseudo_read.cpp),
[indexed load/store](https://github.com/snes9xgit/snes9x/blob/fae2fea08f74180759ef540ee94259213f503480/apu/bapu/smp/core/oppseudo_mov.cpp),
[timer clock](https://github.com/snes9xgit/snes9x/blob/fae2fea08f74180759ef540ee94259213f503480/apu/bapu/smp/timing.cpp),
[DSP](https://github.com/snes9xgit/snes9x/blob/fae2fea08f74180759ef540ee94259213f503480/apu/bapu/dsp/SPC_DSP.cpp).

## Queue synchronization candidate

Before the patch, the real APU fixture fails its first
`apu_runToGuestCycle(apu, 100, 0)` after scheduling a write at guest time 100.
With a positive budget, the old synchronization loop executes an extra SPC
cycle to drain the due event. Two CPU writes synchronized at the same time
can consequently execute an instruction, tick a timer, or produce a DSP sample
between them. This is a scheduler boundary defect independent of any chosen
physical SPC instruction phase.

`snesrecomp-apu-boundary-sync.patch` publishes queued events through the
requested boundary when that time has already been reached. It leaves later
events pending and consumes a cycle budget only when time must advance.
The regression passes in both APU modes. Save/resume tests compare the full
serialized guest state, including DSP output, and the port scheduler.

The candidate is **opt-in** through `-ApuBoundarySync`. CMake checks both
directions of patch/option agreement and requires the existing SPC-D4 guarded
stack. Its `kirby-apu-boundary-v1` snapshot identity prevents loading ordinary
guarded save states into the candidate or vice versa. SRAM is unaffected.
Ordinary setup retains the previous runtime behavior and interpreter fallback.
Running `check_apu.py` against an ordinary guarded framework intentionally
fails at the queue-boundary regression after the reference comparisons.

## Paced replay evidence

The supplied USA ROM was reverified as SHA-256
`0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.
The reference DLL retains the hash in [compatibility](compatibility.md).
All captures, input recordings, generated game code and executables remain
under ignored `local/` paths.

For an 850-frame menu replay from zero SRAM, with recorded Start presses,
two candidate native runs and one interpreter-fallback run have identical
full WRAM at all 850 boundaries and identical 452,621-sample DSP-ring captures.
The unpatched control matches those WRAM boundaries and PCM exactly. Two
reference captures repeat their frame records and all 453,078 audio samples.
The candidate fixes the synthetic queue contract without improving this
recorded game scenario; it therefore remains opt-in.

A longer 2,000-frame replay adds menu selections, directional holds and action
inputs. Two candidate native runs and one fallback run again have identical
full WRAM at every boundary and identical 1,065,711-sample PCM captures. Two
reference runs repeat all frame records and their 1,066,168 audio samples.
The local input recording hash is
`67201832706ba7429090b4c8afdebb27f6431d4b9c77d9443acb14c2585ab7e4`.
The first audio and WRAM divergences remain the same as below; 1,249 of 2,000
host/reference WRAM pairs differ. This checks determinism for these inputs;
it does not establish reference-equivalent gameplay or full-course completion.

The first host nonzero stereo sample remains 156932, versus reference 157388.
With the explicit observational offset of 456 samples, the first mismatch
remains host 156991 `(4722, 4781)` versus reference 157447 `(4723, 4782)`.
At the existing frame offset of one, full WRAM first differs at host 15 versus
reference 16; 506 of 850 selected pairs differ. This does not measure hardware
latency: host capture skips its first DSP interval and reference audio comes
from libretro. No resampling or inferred alignment is applied.

The local 850-frame recording hash is
`ec5fd4dd49bb8f8ba5a17153b35ee82416003903e03f00dd7fc7055c8413cace`.
The tested control executable hash is
`0a9d2a94a258ab156f534e5e910b6962e79e1fc3c87a8c23c139a3cfa21f6b38`;
candidate hash is
`33d6b10a6dda1e7bf00c9e1a4c8563bebf2e6c1f616406bab450653c2c30890a`.
These identify local builds, not distributed artifacts.

To replay a local recording, use fresh output and capture paths for every run:

```powershell
$env:SNESRECOMP_APU_BEAM_ELAPSED = '1'
$env:SNESRECOMP_APU_HANDOFF = '1'
$env:SNESRECOMP_APU_ABSOLUTE_PREANCHOR = '1'
$env:SNESRECOMP_LLE_BOUNCE = '1' # use '0' for interpreter fallback
$env:SNESRECOMP_DSPOUT = Join-Path (Get-Location) 'local/native-a.pcm'
./tools/headless.ps1 -Rom "Kirby's Dream Course (USA).sfc" `
  -Executable local/apu-boundary-build/KirbyDreamCourseRecomp.exe `
  -Output local/native-a -Frames 850 -InputRecording local/input.json
python tools/reference_capture.py --rom "Kirby's Dream Course (USA).sfc" `
  --core local/reference/snes9x_libretro.dll --frames 850 `
  --output local/reference-a.json --input-recording local/input.json --audio-raw
python tools/compare_audio.py local/native-a.pcm local/native-b.pcm --offset 0
python tools/compare_audio.py local/native-a.pcm local/reference-a.audio.s16le `
  --offset 456 --allow-partial
```

Use `compare_wram.py --recording` to check recording, executable, initial SRAM
and runtime-option identities as well as WRAM. Add `--allow-bounce-difference`
only for native-versus-fallback runs from the same executable. A returned
reference mismatch is diagnostic evidence, not a passing fidelity gate.
Use paced runs for DSP-ring captures; `headless.ps1` rejects `-Unpaced` when
`SNESRECOMP_DSPOUT` is set. These checks do not validate physical audio-device
playback, the complete game, or all save and multi-player paths.
