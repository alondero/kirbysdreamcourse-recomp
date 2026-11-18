# Rolling-ball artwork recovery (#87)

The recovered **state-14 slow-roll family has two direction-independent
frames**, shared by both players. Its scripts never select a view by heading
or camera angle. Both locally decoded frames are round, featureless ball art;
the small contour change is animation phase. No phase-preserving alternate
directional view was established for this family. Keep its original artwork
through the existing `KdcApplySpriteView()` fallback. Do not substitute the
normal ball's face-bearing directional frames.

This closes the investigation for one rolling family and its player
relationship. It does not classify every rolling/spin state or power. There
is no new runtime view family, animation scheduler, uploader or interpolation
change. The added tool validates recovered metadata, and the synthetic
fixture protects the unchanged-art fallback.

## Identity and evidence boundary

Local Windows investigation, 2026-11-17:

| Input | Verified identity |
| --- | --- |
| Headerless USA ROM | 1,048,576 bytes; SHA-256 `0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b` |
| Source baseline | `4ee9b0f88123fe079f65f2b4d49262b7e33ab446` (issue discovery baseline: `e18a4f753f54ed8bedf015f7bd634d26f6fc4241`) |
| Framework | `147b55e252dcb3e7680a99c7c2f50e7183891844`, prepared guarded patches |
| Unmodified baseline executable | SHA-256 `d6172d8bee55a02e0c4121bd386c4619838344e4c880908fd623130f1b28c80c` |
| Local diagnostic executable | SHA-256 `173bcb526b6224eb05085498c4367af03e528dc7cfd79bf8991b8250658bdd31` |

The ROM hash was checked before decoding or replay. Static evidence uses the
pinned framework's `recompiler/snes65816.py` decoder, with 16-bit accumulator
and index entry modes at the cited producers. The available REA Windows
Ghidra provider admits x86/x64 PE targets, not this SNES image; these findings
are decoder/trace evidence, not REA function evidence.

A temporary read-only host observer recorded entry to `$80:8696`: frame,
input A, return address, direct-page script cursor and animation slot. A
second observer captured VRAM immediately before `$80:82CA` OAM DMA. Existing
sprite logging identified `$80:9EEC` ball pieces and their world motion. The
observer was removed from tracked runtime source after building the local
diagnostic executable. ROMs, decoded contact sheets, raw traces, snapshots,
replay scripts and executables remain in ignored `local/rolling/` and
`local/rolling-shot-search/`; none are public artifacts.

## Recovered producer and script format

All script addresses in this section are in bank `$87`; words are
little-endian. Player identifiers retain the guest byte offsets **0 and 2**.

| Field | Player one | Player two |
| --- | --- | --- |
| Animation registration record | `$9AD8` | `$B98A` |
| State word referenced by registration | `$D7D4` | `$D7D6` |
| State-to-script table | `$9ADE` | `$B990` |
| Table index 14 entry | `$A3A5` | `$C24A` |
| Mirror/layout word written by loop | `$D95E` | `$D960` |
| Phase-0 DMA list | `$AB7E` | `$C9EB` |
| Phase-1 DMA list | `$AB87` | `$C9F4` |
| VRAM destination, **word** address | `$7C00` | `$7E00` |

The registration records independently contain the player offset, state-word
address and table address. `$87:804C` registers them in eight slots at even
indices. `$87:808E` services those slots. Relevant arrays are `$6830` (player
offset), `$6840` (state-word pointer), `$6850` (last state), `$6860` (table),
`$6870` (script cursor), `$6880` (delay) and `$6890` (per-tick callback), each
indexed by the animation slot byte offset. The slot and player offsets are
distinct concepts, even when both happen to be 0/2 in these captures.

The state-14 scripts have this bounded structure:

| Command | Meaning established by producer |
| --- | --- |
| `$24` | `$87:833E` clears the slot's per-tick callback |
| `$08`, mirror word, zero | `$87:81B3` writes zero to that word |
| `$12`, phase-0 DMA-list pointer, delay 2 | `$87:8215` calls `$80:8696`, then `$87:81E5` stores the delay/cursor |
| `$08`, mirror word, zero | Keep the same mirror/layout selector for phase 1 |
| `$12`, phase-1 DMA-list pointer, delay 2 | Same uploader and delay handling |
| `$02`, entry + 1 | `$87:8179` loops to the first mirror-word write |

The loop is 22 bytes including its initial callback clear and final branch.
Phase command addresses are `$A3AB/$A3B4` and `$C250/$C259`; the back edges
are `$A3B8 -> A3A6` and `$C25D -> C24B`. No direction, facing, aim or spin word
is read anywhere in this loop. Delay is consumed on animation service ticks,
not host presentation ticks. Trace calls alternate every two simulation
frames in these replays; that cadence is an observation for these cases.

Both players use **identical ROM sources**, established from their actual
records rather than an assumed table offset:

| Phase | Source | ROM byte offset | Length |
| --- | --- | --- | --- |
| 0 | `$93:F7D6` | `$09F7D6` | `$0120` bytes |
| 1 | `$93:F8F6` | `$09F8F6` | `$0120` bytes |

Each DMA list is one eight-byte mode-0 record followed by `$FF`, with the
length in bytes, 24-bit ROM source, and destination in VRAM words. It fits
the current transfer reader without any new mode, bound or stride rule.
Each upload holds nine 8x8 tiles used by the ball's 3x3 piece layout. Player
one uses tiles `$1C0..1C8`; player two uses `$1E0..1E8`. Palette, OBJ flags,
piece placement, ownership and motion continue to come from the guest draw.

### What the captures establish

At P1 frame 777, `$80:8696` receives A=`$AB7E` from return address
`$87:821E`, with direct page `$1E00`, script cursor `$A3AE` and slot 0. At
779 it receives `$AB87` with cursor `$A3B7`. The slot's saved cursor after
delay setup is respectively `$A3AF/$A3B8`. P2 independently receives
`$C9EB/$C9F4` at frames 1431/1433, with input cursors `$C253/$C25C`, saved
cursors `$C254/$C25D` and slot 2. Its registration/state words confirm player
offset 2. Source pointers alone would be insufficient: the same DMA lists
also occur in the pre-shot scripts.

The upload and displayed phase lag the live script. In the P1 latch captures,
frames 777/778 match neither rolling frame; 779/780 contain phase 0,
781/782 phase 1, then repeat. The live saved cursor at 779 already describes
phase 1's queued work. P2 has the same pattern at 1431/1432 (neither),
1433/1434 (phase 0), 1435/1436 (phase 1). A current state/cursor must never
override unmatched resident art.

Across sampled state-14 frames, P1 heading 0 has 81 phase-0 and 80 phase-1
resident matches plus two initial nonmatches (777–939). Headings 16 and 106
each have 80/80 plus two initial nonmatches (778–939). P2 heading 45 has
16/16 plus two initial nonmatches (1431–1464). The same source bytes and
zero mirror/layout words recur at these headings and at positive/negative
camera quarter turns. Locally decoded art has no face, feet or other
directional landmark. These observations support direction-independent art
for this loop, not a search claim about the entire ROM.

Calling the unchanged `KdcApplySpriteView()` against those 521 snapshots,
their player/direction/facing/aim metadata, all nine ball tiles and yaw
multiples of a quarter turn from -2pi to +2pi returned unchanged fallback
in all 42,201 calls. The normal-ball table cannot explain this art. Mapping
it onto normal-ball phases would invent correspondence and change the
animation's appearance. No runtime rolling-view descriptor is justified.

## Reproduction inputs and comparisons

Use isolated executable/config/save directories, `--no-launcher`, the
verified `--rom`, and `--dev-hole normal:1:1`. The successful P1 script is:

```text
wait 600
press a 2
wait 57
press a 2
wait 27
press a 2
wait 60
press a 2
```

Shot observations: heading `$D9B4=0`, loft `$D9B6=0`, spin
`$D9B8/$D9BA=6/6`, power `$6B3A=58`, ability `$D9C4=0`. Rolling state 14
starts at 777; initial X velocity is 4477 in guest units. Fractional position
is essential: the older three-tap recipe also rolls, but its integer cell
coordinates stay unchanged throughout 690–899.

For heading 16, replace the initial wait with `wait 400`, `press left 90`,
`wait 110`. For heading 106 use `wait 400`, `press left 180`, `wait 20`.
Retain all four A presses and their waits. The hold durations are not
degrees. Both begin rolling at 778 with neutral spin, zero loft and no
ability; initial velocities are respectively (4304,1234) and (-1234,4304).

For genuine P2 input use `--dev-hole multi:1:1`: execute the four-tap P1
script with its final wait reduced from 60 to 20, then append:

```text
wait 538
press p2:a 2
wait 57
press p2:a 2
wait 27
press p2:a 2
wait 60
press p2:a 2
```

Normal gameplay switches `$1803/$180B` to 2 at 975. P2 rolls at 1431 with
heading 45, loft 0, spin 6/6, power 58, ability 0 and velocity (3158,3158).
Qualify only 1431–1464: P2 encounters P1 after 1465. There was no guest
state injection to select P2 or its animation.

Headless environment: `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`,
`SNESRECOMP_APU_BEAM_ELAPSED=1`, `SNESRECOMP_APU_HANDOFF=1`,
`SNESRECOMP_APU_ABSOLUTE_PREANCHOR=1`, autosave off, frame delay disabled,
new renderer on, widescreen off. Each matrix case starts in a fresh isolated
directory. Diagnostic reruns retained the same guest-initialized SRAM; their
sampled output still matched the independent fresh runs.
P1 runs last 950 frames and compare captures 760–939; P2 runs last 1470
and compare 1410–1464. The camera script uses full horizontal input at
740, zero at 800 (a quarter turn), reset at 900; the reverse case negates
horizontal input. P2 turns at 1360 and stops at 1420.

| Comparison | Result in compared observations |
| --- | --- |
| Two original-view P1 runs | All 180 WRAM snapshots and BMPs identical |
| Baseline vs diagnostic executable, original view | All 180 WRAM snapshots and BMPs identical |
| Original vs turned-camera P1 | All 180 WRAM snapshots identical; pixels differ as intended during orbit, match again at 900–939 |
| Repeated positive-turn P1 | All 180 WRAM snapshots and BMPs identical |
| Compiled bounce enabled vs disabled, P1 orbit | All 180 WRAM snapshots and BMPs identical |
| Compiled bounce enabled vs disabled, P2 orbit | All 55 WRAM snapshots and BMPs identical |
| Smoothing disabled vs enabled (`kdc-presentation.cfg`: `0 120` / `1 120`) | All 180 WRAM snapshots and resident phases identical; BMPs agree at 760–899 |

Bounce logs verify actual compiled execution: 20,425 calls in the P1 native
case and 35,667 in P2, versus zero with `SNESRECOMP_LLE_BOUNCE=0`. These are
native/interpreter comparisons of this guarded host, not reference-emulator
equivalence. First WRAM divergence in the comparisons above: **none**.

First smoothing on/off pixel difference: frame **900**, at camera reset.
The existing high-refresh path retains the orbit renderer at original yaw;
the disabled path returns to the native framebuffer. All 39 post-reset
images (901–939) match a separate smoothing-enabled original-view run.
This is an existing renderer-path distinction, not rolling phase advancement.
Headless captures exercise simulation presentations; intermediate presentation
ticks, layout changes, allocation generations and reset/map/player history
rejection additionally have the existing ROM-free high-refresh fixture.

## Repeatable metadata check and tests

`tools/inspect_rolling_artwork.py` checks ROM identity before parsing either
table, validates the exact bounded loop/transfer shape, and prints metadata
only. An optional local 64 KiB VRAM snapshot reports the uniquely resident
phase per player, or null for unmatched/ambiguous art:

```text
python -B tools/inspect_rolling_artwork.py --rom "Kirby's Dream Course (USA).sfc"
python -B tools/inspect_rolling_artwork.py --rom "Kirby's Dream Course (USA).sfc" --vram local/rolling/observed/vram_0780.bin
```

It does not infer live phase from a cursor, advance animation or expose a
render-time descriptor. Output metadata is not extracted artwork.

All 15 selected ROM-free tests passed on Windows/GCC:

```text
python -B -m unittest discover -s tests -p test_inspect_rolling_artwork.py -v
python -B -m unittest discover -s tests -p test_course_sprite_view.py -v
python -B -m unittest discover -s tests -p test_course_sprites.py -v
python -B -m unittest discover -s tests -p test_course_orbit_render.py -v
python -B -m unittest discover -s tests -p test_course_camera_host.py -v
python -B -m unittest discover -s tests -p test_course_camera.py -v
```

Set `SNESRECOMP_ROOT` to the prepared pinned framework for the sprite,
renderer and input fixtures. These selected runs had no skips. Synthetic
tests cover distinct player tables, both phases, partial/ambiguous resident
art, malformed script/mirror/delay/loop fields, unsupported DMA and bank
crossings. The view fixture verifies unchanged rolling-art fallback for
both players, both mirrors, yaw wrap and reverse turns. This fallback
already passed before the additional fixture; there is no claimed failing
runtime extension case. The opt-in `test_course_camera_runtime.py` and SDL
controller suites were not rerun; the live evidence here is the separate
stronger-shot matrix above.

## Remaining limits

No independently aligned Snes9x rolling comparison was performed. The
available pinned 1.63 core (`fae2fea08f74180759ef540ee94259213f503480`) DLL
hash was verified as
`72e0f16839c0298d8ce52eb2281e83feaede49af73936a39bb782b0f488e92e0`.
These developer-hole host replays do not supply an identically aligned
reference startup; they do not remove the existing host/reference divergence
in [compatibility.md](compatibility.md).

Non-neutral spin, lofted shots, other headings, other animation states,
abilities, detached effects, P2 after collision, Linux/Steam Deck, physical
controllers and live save/load/reset discontinuities remain unqualified by
this investigation. Synthetic lifecycle coverage is not a real save/load
rolling replay. No raw art, ROM, generated game source or capture belongs
in commits, issue attachments, CI artifacts or release packages.
