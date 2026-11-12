# One player versus CPU

Choose **1P VS CPU** below **PRACTICE** on the title menu. Up/Down selects
a row; Left/Right changes the difficulty, course or Normal/Extra selection.
Choose **PLAY COURSE** to start an eight-hole two-player course with the CPU
in the second seat. The original game owns turns, collisions, scoring and
hole progression. The CPU acknowledges its winning hole scorecards after two
seconds; the human acknowledges their own. At the final course totals, press
P1 A to continue through the winner animation and return to the match picker.

Start opens Resume, Restart Course, Choose Match and Exit Match. B resumes
from the pause menu or exits an active match from the match picker. Ordinary
battery progress is retained in memory and restored on exit. CPU-session
battery data uses `saves/cpu/save.srm`; practice retains its separate directory.
Save states and rewind are disabled during the match, as they are in practice.
Run-ahead remains unsupported. Difficulty is not saved across launches.

## Difficulty policy

| Difficulty | Search and execution |
| --- | --- |
| Easy | Ground shots with neutral spin. Only the initial straight, forward-moving part of a predicted path can earn target rewards; no deliberate banks. Coarser angle refinement and a 45% chance of a small execution error. |
| Moderate | Ground and lofted shots, including top/backspin. Every fourth turn also considers bank directions and additional spin combinations. A 22% chance of a small execution error. |
| Expert | Ground and low/high loft, spin in both directions, lateral spin and bank directions. One-degree angle refinement and an 8% chance of a small execution error. |

Errors change aim by up to four degrees and reduce power slightly. They use
a deterministic host PRNG, independent of the game's RNG and wall clock.
Restarting repeats the sequence. Errors are applied after planning; they are
not secretly corrected during the shot. These are policy settings, not measured
win rates. Incidental collisions can still occur on Easy.

The CPU compares trajectories produced by the original game's aiming preview.
It considers nearby enemies, opposing stars and the cup, ignores its own stars,
rewards close approaches and hits, penalizes fast cup passes and paths ending
outside the map, and respects the live energy-dependent power limit. A bounded
search is followed by local angle/power refinement. Refinement extends the
guest's ground preview to 240 simulation ticks to assess more of the rollout.
The final shot uses the original spin and power meters. Searching takes several
seconds, especially on Expert.

## USA integration evidence

ROM identity was checked before investigation: SHA-256
`0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.
REA rejected the SNES image format; addresses were inspected with the local
65816 disassembler, then exercised in the guarded runtime. ROM-derived
disassembly, captures and recordings remain under ignored `local/`.

- The existing root-menu extension appends return value 7 after practice's 6.
  Root stack checks and the original choices are retained. The CPU picker
  reuses the validated two-player developer start and isolated-save lifecycle.
- `$81:A619/$A680` delimit aiming entry and its input loop. `$1803=2` is the
  second player's word offset; `$17FF=1` gates multiplayer. Demo `$ED07` is
  excluded. `$D9B4/$D9B6` are aim and loft; `$D9B8/$D9BA` are spin selectors.
- `$81:A9B8` computes the guest preview, ending at `$81:AA3D`. The power-table
  entry `$81:C2E9` receives the trial's energy-limited power, only during a
  scheduled CPU preview. `$81:AA0E` follows initialization of its tick limit.
  The host reads the resulting bounded `$6AEE` trajectory array. It does not
  run speculative host snapshots or replace the game's physics.
- Registry `$B4B8` identifies the cup as 8 and collectible enemies/stars in
  `$40..$57`; `$B4F8` maps them to type-$49 tasks. `$539C + task` records star
  ownership (0/1, with 2 unclaimed). `$B538/$B578` hold cell coordinates.
- `$81:BFA9/$BFB5` handle spin input and confirmation; `$81:C005` handles
  power confirmation. The live power cap is `$6B3C`. `$81:C04C` starts shot
  conversion through the original code.
- `$83:D495` reads the hole winner's controller at `$004F + 2*$6B84`.
  CPU wins need an acknowledgement in `$0051`; blocking physical P2 input
  without this hook leaves the game on the scorecard.
- `$80:F558` follows the automatic multiplayer result animation. CPU matches
  redirect from there to `$80:F120` and the CPU picker, retaining the isolated
  save session instead of re-entering the two-controller course setup.
  The preceding final totals screen deliberately waits for P1's newly pressed
  A bit at `$005F` (`$83:CAE5`), regardless of who won.

These hooks are game-owned and disabled by default. Hardware behavior,
generated source and the interpreter fallback are unchanged. Future native
translation of these sites must preserve equivalent interception.

## Validation and limits

ROM-free tests cover controller ownership, difficulty/menu selection, bounded
search, selection of both spin signs, low-energy power limits, shot confirmation, scorecard ownership,
reset/exit and save isolation. Real-ROM checks are opt-in:

```powershell
$env:SNESRECOMP_ROOT = '<patched pinned framework>'
$env:KDC_TEST_CPU = '1'
$env:KDC_PRACTICE_ROM = '<verified USA ROM>'
$env:KDC_PRACTICE_EXECUTABLE = '<new executable>'
python -m unittest discover -s tests -p test_cpu_opponent.py -v
```

The live checks start through the new menu, play a deterministic human shot,
then require the CPU to collect a star naturally and return control to P1.
They compare repeated final WRAM for all difficulties and native/interpreter
WRAM for Expert. No WRAM pokes supply the CPU's scoring results.

The Windows bounded match check additionally observed Expert collecting a
star, choosing a high loft for its second shot, sinking the cup and advancing
to map 129 through the original scorecard. Extra multiplayer map 208 also
completed a CPU turn. The presented difficulty picker was visually inspected.
An additional transition fixture marked the live hole as hole eight
(`$6A5E/$6A60/$6A62 = 7/14/1`), let the CPU finish naturally, pressed P1 A
on final totals, and observed the automatic winner animation return to the
CPU picker. This checks the final-course branch, not eight naturally completed
holes.

Windows verification on 2026-11-12 used native CMake/Ninja with the patched
framework pin and existing generated inputs, building Release with
`cmake --build local/cpu-build -j 8`. The final standard suite passed 90 tests
(19 optional skips). All seven real-ROM practice checks passed, including
ordinary-boot WRAM/SRAM equality against the pre-change guarded executable.
Both CPU tests passed with ROM checks enabled, including repeated runs of all
three difficulties and Expert native/interpreter equality. No final-WRAM
divergence was found in those comparisons; they do not establish every-frame
equivalence.

This is a heuristic opponent. Ability activation tactics, moving-target
prediction, water-specific strategy, exhaustive course completion, the final
match-results flow after eight natural holes, Linux play and measured difficulty win rates are not
qualified. The preview does not model all effects of hitting an enemy or the
other player, so a predicted path is not a guarantee of the final lie. CPU
search state is host-owned, which is why snapshots remain disabled.

CPU behavior intentionally differs from the reference game, which has no CPU
opponent. Ordinary boot is compared against the pre-change executable; the
existing reference mismatch at host frame 15/reference frame 16 remains as
documented in [compatibility](compatibility.md). No new reference-emulator
equivalence claim is made for CPU-controlled shots.
