# Optional faster score and hole transitions

Investigation for #32, 2026-11-15, against project commit
`18bd9ed6bbf3d8fb5f33411367c0d359d6135af5`.

## Decision

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

## Remaining gates

No guest score-table or RNG addresses were established here. The visible
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
