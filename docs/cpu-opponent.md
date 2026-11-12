# One player versus CPU

Choose **1P VS CPU** below **PRACTICE** on the title menu. The same menu then
offers **EASY**, **MEDIUM** and **HARD**; B returns to the title menu. After a
difficulty, the game's own two-player screens follow: **PLAY COURSE** (and
**EXTRA COURSE** once the ordinary save has unlocked it), course select, the
order roll and the handicap grid. Player 1 drives every menu. The CPU's
handicap column is fixed by its difficulty:

| Difficulty | Handicap column | Shot power cap | Lofted guide | Spin marker |
| --- | --- | --- | --- | --- |
| Easy | DIFFICULT end | 50 | 1 bounce | fastest |
| Medium | middle | 58 | 2 bounces | middle |
| Hard | EASY end | 63 | 3 bounces | slowest |

Choose **EXIT** on the handicap grid to see the course introduction and play.
The original game owns turns, collisions, scoring, hole progression, results
and the return to course select. In player 2's HUD name box the game's
"PLAYER 2" artwork is replaced with "CPU" over the difficulty, drawn with this
project's own lettering. The CPU acknowledges its winning hole scorecards after
two seconds; the human acknowledges their own.

Start opens **RESUME** and **EXIT MATCH** during a hole; in menus and results
Start keeps its original meaning. Backing out of the two-player menus to the
title menu also ends the match. Ordinary battery progress is retained in memory
and restored, byte for byte, when the match ends. The match starts from a copy
of the ordinary battery data, so unlocks match, and its own writes go to
`saves/cpu/save.srm`. The course-select win/loss tallies therefore start from
the ordinary save's two-player tallies in every new CPU session. Save states
and rewind are disabled during the match. Run-ahead remains unsupported.

## How the CPU plays

When the CPU's aiming turn starts it keeps still while it plans, then turns
the aim cursor with held and tapped Left/Right, waits briefly, and plays the
spin and power meters through the original controls.

Planning is off-screen. Once per frame, at the start of the frame, the CPU
takes a private rollback snapshot, copies a fixed planning image of work RAM
into place, writes one candidate shot's angle, loft, spin and power, runs the
game's own aiming preview (`$81:A9B8`) in the interpreter, reads the predicted
path and restores the snapshot. The frame then runs as if nothing happened:
end-of-turn work RAM, video RAM and audio RAM were byte-identical with and
without planning in the prototype comparison. A preview costs about as much
host time as a frame, so the budget is a fixed count of one preview per frame,
never a clock, which keeps runs repeatable.

The handicap's guide level also sets how far ahead the CPU can judge a roll:
the whole roll (240 preview ticks) for Hard, about half (120) for Medium and
only the on-screen guide (40) for Easy. Past that point the CPU continues the
path in a straight line at the slowdown the guide shows. That guess cannot see
walls or slopes, which is what makes the shorter guides weaker. Lofted previews
stop after the handicap's bounce count, as the on-screen guide does.

| Difficulty | Candidates | Typical planning |
| --- | --- | --- |
| Easy | Direct ground shots at up to three nearest targets, neutral spin. Only the initial straight, forward part of a path counts. Coarse 4° refinement. | about 0.4 s |
| Medium | Up to four targets, ground and 30° loft, then side spin (and top/backspin for lofts). 2° refinement. | about 0.5-1 s |
| Hard | Up to four targets plus banks 45° either side, ground, 30° and 60° loft, spin. 1° refinement. | about 1-2 s |

Each candidate path is scored against nearby enemies, opposing stars and the
cup: hits and close approaches score, fast cup passes and paths ending outside
the map are penalized, and modest power wins ties. Five powers between a fifth
of the live cap and the cap are tried for each direction and loft, then the
best is refined over angle and power.

There is no random aim error. The CPU's execution slips by at most one frame:
the power meter moves one unit a frame, so the shot's power can be one unit
off; on lofted shots the vertical spin marker can stop one step off, more often
with the faster marker of a more handicapped column. Slips come from a
deterministic host generator, independent of the game's generator and the
clock, so restarting repeats them.

## USA integration evidence

ROM identity was checked before investigation: SHA-256
`0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.
Addresses were inspected with the local 65816 disassembler, then exercised in
the guarded runtime. ROM-derived disassembly, captures and recordings remain
under ignored `local/`.

- Root menu: `$83:B71C` recomputes unlock flags `$EAA9..$EAB3` and builds the
  menu; `$83:B720` rebuilds it without that; `$83:BB44` is the shared menu
  routine and `$83:BBD8` runs after its entries are laid out, before the
  tilemap upload. The extension appends PRACTICE (6) and 1P VS CPU (7) there,
  or, after 1P VS CPU, relabels the entries EASY/MEDIUM/HARD (8..10) and sets
  `$EA8A` so B cancels as it does in the guest's submenus. At `$83:B74E` a
  difficulty is turned into result 1, the original two-player entry.
- `$83:B824` is the two-player submenu (EXTRA COURSE only when `$EAA9`);
  `$83:B83E` stores `$17FF = 1` after a choice. The CPU save session starts
  there. The next root-menu build ends it.
- Handicap words (0 = EASY end, 2 = DIFFICULT end): power `$ECFB/$ECFD`,
  guide `$ECFF/$ED01`, marker `$ED03/$ED05` (player 1/player 2). `$83:DD22`
  first draws the grid; the CPU's column is set there and again at `$83:DFC0`,
  which copies the active player's column into play each turn: power cap
  `$6B3C` (63/58/50), guide `$6ACC` (3/2/1 lofted bounces), marker `$6B5E`
  (3/2/1 frames per step).
- `$81:A619/$A680` delimit aiming entry and its input loop. `$1803 = 2` is the
  second player's word offset; `$17FF = 1` gates multiplayer; demo `$ED07` is
  excluded. `$D9B4/$D9B6` are aim and loft; `$D9B8/$D9BA` are spin selectors.
  `$81:B555` turns the aim: `$0067` bit `$0200` (Left) raises the angle,
  `$0100` (Right) lowers it.
- `$81:A9B8` computes the preview; `$81:AA0E` follows initialization of its
  tick limit `$6AD2`, where planning previews set the look-ahead. The path is
  `$6AEE` points in the `$B746..$CB46` arrays.
- Registry `$B4B8` identifies the cup as 8 and collectible enemies/stars in
  `$40..$57`; `$B4F8` maps them to type-$49 tasks. `$539C + task` records star
  ownership. `$B538/$B578` hold cell coordinates.
- `$81:BFA9/$BFB5` handle spin input and confirmation; `$81:C005` handles
  power confirmation. `$81:C04C` starts shot conversion.
- `$83:D495` reads the hole winner's controller at `$004F + 2*$6B84`. CPU wins
  need an acknowledgement in `$0051`.
- Player 2's HUD name box is BG3 tiles `$108..$10F` (player 1's `$100..$107`).
  When player 2's top row matches player 1's (the game has just uploaded its
  own artwork), the host replaces the eight tiles once.

These hooks are game-owned and disabled outside CPU matches. Hardware
behavior, generated source and the interpreter fallback are unchanged. Future
native translation of these sites must preserve equivalent interception.

## Validation and limits

ROM-free tests cover the difficulty menu, entry into the original two-player
path, save isolation and both ways of ending a match, the planner's
one-preview-per-frame budget and invisibility, look-ahead per guide level,
the candidates each difficulty considers, the aim glide through Left/Right,
spin and power confirmation, scorecard ownership and the HUD name box.
Real-ROM checks are opt-in:

```powershell
$env:SNESRECOMP_ROOT = '<patched pinned framework>'
$env:KDC_TEST_CPU = '1'
$env:KDC_PRACTICE_ROM = '<verified USA ROM>'
$env:KDC_PRACTICE_EXECUTABLE = '<new executable>'
python -m unittest discover -s tests -p test_cpu_opponent.py -v
```

The live checks start through the new menu and the original two-player
screens, then require the CPU to collect a star naturally, the handicap to be
in play, and control to return to player 1. They compare repeated final work
RAM for all difficulties and native/interpreter work RAM for Hard.

This is a heuristic opponent. Ability activation tactics, moving-target
prediction, water-specific strategy and measured win rates are not qualified.
The preview does not model all effects of hitting an enemy or the other
player. CPU planning state is host-owned, which is why ordinary snapshots stay
disabled during a match.

CPU behavior intentionally differs from the reference game, which has no CPU
opponent. No reference-emulator equivalence claim is made for CPU-controlled
shots.
