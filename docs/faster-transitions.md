# Optional faster score and hole transitions

Investigation for #32, 2026-11-15, against project commit
`18bd9ed6bbf3d8fb5f33411367c0d359d6135af5`.

## Optional host pacing, 2026-11-15

The launcher now offers **Faster hole transitions** on its **Mods** page,
disabled by default. `kdc-transitions.cfg` stores `0` or `1` beside the
executable. It accelerates complete guest frames at up to 3x wall-clock speed
from `$80F48C` (end of hole) until `$80F256` (next-hole loader entry).
Every guest instruction, frame, input tick, tally, fade and save operation still
runs. The scorecard still needs the player's acknowledgement. No guest timers,
input bits, ROM bytes or progression branches are changed.

Eligibility requires observing ordinary new-game initialization at `$80F1DF`,
no practice/developer/CPU session, solo mode (`$17FF == 0`), normal courses
(`$EAB9 == 0`, `$6A5A < 8`) and holes 1–7 (`$6A5E < 7`). Title entry, reset
and save-state loads clear eligibility. Unknown scenes and restored sessions
stay at normal speed until another new game. A 1,800-guest-frame timeout
stops acceleration even if the player leaves a scorecard open. Extra courses,
multiplayer and course-end processing are excluded. Gameplay and the next
hole's loader retain their original pacing.

The framework patch adds a bounded `frame_speed` host callback. It changes
the simulation deadline and limits presentation work while polling events and
delivering input once per guest frame. It does not batch hidden guest frames.
Manual turbo, unpaced diagnostics and netplay use their existing paths.
The feature requires the guarded APU clock flags used by release defaults and
`play_experimental.ps1`; explicit legacy timing overrides disable it.

Accelerated output is muted under the APU mutex. The SPC and DSP continue
running; `dsp_trimSamples` discards queued output without resetting their
processing state. Audio backpressure is suspended during acceleration and
the host output buffer/stream is cleared at both edges. Playback resumes
from newly produced samples, avoiding a queue of transition audio over gameplay.
Hardware execution stays in the framework; scene eligibility stays in
[`transitions.c`](../src/transitions.c). The interpreter remains available.

### Qualification

Verified the exact ROM SHA-256 in [architecture](../architecture.md#input-identity)
before all ROM-dependent work. Used the
ordinary first-hole recording from the investigation, extended to 14,000
frames with the original frame-12,301 ten-frame A acknowledgement. The local
recording's SHA-256 is
`2f082cc08927461eb2d3de96bd3be8eafafb718a7ecd4bde2a41c5c948196af8`.
All runs start with zero SRAM. The script uses existing manual turbo for
the identical first 11,000 frames, then ordinary pacing for the transition
and next-hole gameplay. No memory pokes or forced hole completion are used.

Same-build native off/on/off and interpreter-only off/on runs each completed
14,000 guest frames. Every per-frame full-WRAM CRC, CPU trace and logged APU
clock/SPC/port observation matched across all five runs: **no first divergence**.
Full WRAM, SRAM, VRAM, OAM and CGRAM dumps matched byte-for-byte at guest
frames 11,000, 11,500, 12,000, 12,500, 13,000 and 14,000. Thus this route
preserves all WRAM score/RNG fields without needing to guess their addresses.
This is not a complete machine-state or full-game proof. All final states
have `$6A5E == 1`, `$ABB2 == 1` (hole 2); accelerated pacing started at
frame 11,122 and ended at 12,318.

An additional off/on pair collected `SNESRECOMP_FRAME_TIMING` CSVs. The
presentation interval from frame 11,122 to 12,319 fell from **20.07 s to
8.96 s**, a **55% reduction (2.24x)** on this Windows host with SDL dummy
video/audio. The 3x setting is a ceiling; executing/rasterizing every guest
frame limits the achieved rate. These timed runs also matched all per-frame
CPU/APU/WRAM observations and final memory. This measurement covers the
recorded acknowledgement time; a human may leave the scorecard open longer.

The pinned Snes9x 1.63 core replayed the same 14,000-frame input. Its final
SRAM matches all host runs, SHA-256
`c00da21c076a025ddd7a1b5bad3a667de8e4769a222712e5b07950dad4ebb92e`.
The first sampled host/reference WRAM mismatch is frame 11,000 at `$7E0022`
(421 differing bytes); frames 12,000 and 14,000 differ in 426 and 449 bytes.
These differences also occur with the option off. The previously established
boot mismatch (host frame 15 versus reference frame 16) remains unresolved;
this feature does not claim to fix it or establish reference equivalence.

Windows Release build passed using the existing framework/UI pins and patch
stack. The final suite passed 106 tests (22 opt-in/unavailable checks skipped).
ROM-free tests cover eligibility, every course/hole gate, no guest
writes, timeout, reset/load policy, unsupported audio clocks, launcher
staging/persistence and developer-settings staging. The opt-in real-ROM
test can reproduce the five-run comparison with a local recording:

```powershell
$env:KDC_TEST_TRANSITIONS = '1'
$env:KDC_TRANSITIONS_RECORDING = (Resolve-Path local/transition-validation/route.json)
$env:KDC_TRANSITIONS_EXECUTABLE = (Resolve-Path local/transition-build/KirbyDreamCourseRecomp.exe)
python -m unittest discover -s tests -p test_transitions.py -v
```

The runtime test expects ordinary boot through first-hole completion and
acknowledgement, a 14,000-frame or longer recording, and idle input around its
checkpoints. Captures, recordings, generated source and the ROM remain local.
Untested end to end: other normal course/hole transitions, physical audio
devices, focus/pause/launcher changes
during acceleration, held input across the boundary, and Linux. Synthetic
tests cover excluded modes and fail-closed load/reset handling; those modes
have not been qualified for acceleration.

## PR #72 decision (historical)

Do not ship a transition skip yet. Earlier scorecard acknowledgement reaches
the same next hole and preserves SRAM in the reference experiment below, but
does not establish unchanged score, RNG or complete next-hole state. No runtime,
configuration or default pacing changes are made by this investigation.
This is an unresolved safety gate, not proof that acceleration is impossible.

The [Super Metroid precedent](faster-transitions-precedent.md) uses targeted,
game-specific changes with endpoint checks. It supplies no Kirby boundaries.

## Candidate boundaries and exclusions

These are integration-source observations, not a newly verified disassembly:

| Sequence | Evidence and decision |
| --- | --- |
| End of hole | [`practice.c`](../src/practice.c), `Observe`, intercepts `$80F48C` before scores and progression in both modes. It redirects practice to the title/picker. This identifies an observation point, **not** a safe jump to the next hole: practice deliberately avoids normal progression. |
| Versus score acknowledgement | [`cpu_opponent.c`](../src/cpu_opponent.c), `Observe`, handles `$83D495`; `$6B84 == 1` identifies a CPU winner and the hook sets `$0051` bit `$80` after 120 observations. The winner's controller owns acknowledgement. Generalizing this to P1-only auto-confirm would be wrong. The host CPU opponent's `rng` is not the guest game's RNG. |
| Hole initialization | [`dev_start.c`](../src/dev_start.c), `Observe`, passes through `$80F256`, with map indices available at `$80C168`. Its menu bypass follows new-game initialization and writes course/hole fields. Reusing that reset/start path would not establish preservation of an ongoing match. |
| Scorecard rendering | [`course_view.c`](../src/course_view.c) and [widescreen findings](widescreen.md#between-hole-scorecard-classification-2026-11-04) distinguish reused graphics. A visual signature does not establish completed tallying, persistence or input ownership. Do not use it alone as a skip trigger. |
| Course-end results, records, unlock/save processing | No safe bypass established. Exclude hole eight, course completion and record celebrations from an initial candidate. |
| Fades, loading, audio and animation waits | No proof that these waits are free of state changes. Do not zero timers, omit guest frames, or jump across them. |
| Practice, CPU matches, extra courses and multiplayer | Existing hooks have different ownership and progression rules. Exclude from a first implementation until separately tested, including wins by each seat. |

Executing all guest frames faster in wall-clock time remains a candidate that
avoids deleting guest work. It still requires a bounded host pacing mechanism,
per-guest-frame input delivery, safe audio handling and verified exit boundaries.
`tools/headless.ps1 -Unpaced` sets `DisableFrameDelay` for an entire diagnostic
run; it is not a transition feature and rejects DSP-ring capture. It cannot
by itself qualify interactive fast-forward or deterministic audio.

## Deterministic reference experiment

The local ROM was verified before use: 1,048,576 bytes, SHA-256
`0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.
`tools/reference_capture.py` also checks that identity and the Snes9x 1.63 DLL
hash `72e0f16839c0298d8ce52eb2281e83feaede49af73936a39bb782b0f488e92e0`.
The reference pin is `fae2fea08f74180759ef540ee94259213f503480`.
Runs start with zero SRAM/WRAM and ordinary boot, without developer starts,
ROM patches or forced hole completion.

The local `course-completion-12000.json` recording (SHA-256
`a665dc2e57adc58393053d315be3013970b2fe7da08ed22bb0a2c898e2dbe917`)
reaches a visible Course 1
scorecard at frame 12000, showing 12 for hole 1. Two variants keep all its
events and extend the duration to 14000 frames, adding one ten-frame A press:

| Variant | A press starts | Recording SHA-256 |
| --- | --- | --- |
| Original wait | 12301 | `f7afa9599292ce8ad59e27716285ab2579114949d04d10ae21be31a9233f3634` |
| Earlier acknowledgement | 12001 | `5c8634692591884650063c80268702c35d8a7f498df9c791100223ae31eebf04` |

Both reach visible hole 2, shot 01, by frame 14000. WRAM words `$ABB2` (map)
and `$6A5E` (zero-based hole) are both 1 in both runs. The entire SRAM hash
matches at every equal-numbered frame; the final 8192-byte image has SHA-256
`c00da21c076a025ddd7a1b5bad3a667de8e4769a222712e5b07950dad4ebb92e`.

The first differences between variants are WRAM at frame **12001**, video at
**12002**, and audio at **12019**. These are expected timeline differences
after changing input, not an isolated RNG or scoring defect. At frame 14000,
474 WRAM bytes differ, first at `$7E0022`. Comparing original frame 14000 to
earlier frame 13700 (the nominal 300-frame shift) still differs in 532 bytes,
first at `$7E0022`. A constant offset is only a diagnostic: neither this offset
nor whole-WRAM inequality identifies equivalent game-state boundaries.

Each variant was run twice from boot, comparing all per-frame video, audio,
WRAM and SRAM records. In addition, each first run saved after frame 11999,
restored after 14000 and replayed frames 12000–14000, checking those same
observations. All repeated frame records matched; both round trips passed,
with no first divergence within either repeated scenario. The three
`test_reference_state.py` tests also passed with `KDC_TEST_REFERENCE=1`.

An initial 6200-frame probe moved A from 5551 to 5521. Repeated captures
matched within each variant, but image inspection showed gameplay, not a
scorecard. Its first WRAM difference at 5521 is **not transition evidence**.
The 12000-frame capture corrected that mistaken checkpoint assumption.

## Reproduction

Recordings, images and memory dumps remain in ignored `local/transitions/`;
none belong in commits or release artifacts. With the two local recordings:

```powershell
python tools/reference_capture.py --rom "Kirby's Dream Course (USA).sfc" --core local/reference/snes9x_libretro.dll --frames 14000 --input-recording local/transitions/score-original.json --output local/transitions/score-original-a.json --wram-frame 14000 --sram-frame 14000 --image-frame 14000 --roundtrip-frame 11999
python tools/reference_capture.py --rom "Kirby's Dream Course (USA).sfc" --core local/reference/snes9x_libretro.dll --frames 14000 --input-recording local/transitions/score-earlier.json --output local/transitions/score-earlier-a.json --wram-frame 13700 --wram-frame 14000 --sram-frame 14000 --image-frame 14000 --roundtrip-frame 11999
```

Repeat with `-b.json` output names. Compare the JSON `frames` arrays, rather
than whole manifests if round-trip options differ. Compare variant WRAM dumps
byte-for-byte and report first differing offset and count. To recreate the
experiment without these private recordings, record natural first-hole
completion, identify a visible scorecard and branch that same recording into
two acknowledgement times; frame numbers and hashes will then differ.

## Remaining gates for guest skips and automatic acknowledgement

At the close of the PR #72 investigation, no guest score-table or RNG
addresses had been established. The visible
score before acknowledgement and equal SRAM do not qualify either field
after progression. No claim of safe skipping follows from this experiment.
Untested: both multiplayer winners, extra courses, hole eight, ties, CPU
opponent interactions, practice, save/load/reset, held input, native/fallback
acceleration, audio pacing and Linux. Existing host/reference boot divergence
(host frame 15 versus reference frame 16) remains documented in
[compatibility](compatibility.md); it was not remeasured here.

A follow-up should first identify score/RNG fields and their writers, then
capture entry/exit checkpoints around the original tally, acknowledgement and
loader. Compare both players' scores, full SRAM, guest RNG and next-hole
objects/player/turn state, reporting intentional timer differences separately.
Only then prototype a disabled-by-default, narrowly bounded option. Require
same-build off/on/off and native/fallback deterministic runs, reference
comparison where meaningful, unchanged defaults, and fail-closed handling of
unknown scenes. Do not treat a successful map load as the acceptance gate.
