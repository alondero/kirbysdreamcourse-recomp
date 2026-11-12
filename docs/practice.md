# Hole practice

Press Start at the title screen and choose **PRACTICE**, below **DEMO PLAY**.
The original menu supplies the cursor, navigation sounds and lettering. In the
picker, Up/Down chooses a row and Left/Right changes its selection. A advances
to the next row; select **PLAY THIS HOLE** to begin. B goes back. Keyboard
defaults are arrows, X for A, Z for B and Return for Start.

**1P NORMAL** and **1P EXTRA** offer eight courses each. **2P NORMAL** and
**2P EXTRA** offer four each. Every course offers eight holes. Configure the
second player's controller through Ctrl+L. Raw map slots and attract-mode
shortcuts remain developer-only options.

The **ABILITY** row cycles through the original conditions portraits and names:
None, Stone, Parasol, Fireball, Needle, Tornado, Wheel, Spark, Hi-Jump, Freeze and
UFO. Both players start with that ability in two-player practice. Restart
reapplies the selection; abilities earned or spent during play behave normally.
The picker keeps the title background and music running while consuming both
players' inputs. The in-hole pause menu still freezes gameplay.

During practice, Start opens **RESUME**, **RESTART HOLE**, **CHOOSE A HOLE** and
**EXIT PRACTICE**. B resumes from that menu. Ctrl+R also restarts the selected
hole. **CHOOSE A HOLE** leaves the current hole and opens the picker over the
title background. Finishing the hole also returns directly to the picker,
before score screens, course results or another hole. **PLAY THIS HOLE** replays
the selection; B exits practice from this returned picker.
Exiting practice resets to the ordinary title screen. The selection is
not remembered across application launches.

## Saves and transitions

Practice battery saves live in `saves/practice/save.srm`, beside the executable.
Entering practice saves ordinary progress first and retains its exact SRAM
image in memory. Leaving practice saves its separate battery image, restores
ordinary SRAM and restores the ordinary save directory before resetting.
A failed battery write leaves the current session and directory in place and
shows an error. Quitting during practice writes only practice SRAM; the next
launch uses ordinary saves again.

Save states and rewind are unavailable in the picker and during practice, to
prevent guest state from crossing the ordinary/practice save boundary. The
ordinary SRAM image stays retained when a completed hole returns to the picker.
Restart always uses the selected hole and starting ability. Choosing another
hole replaces that target.

The CLI `--dev-hole` and PowerShell `-DevHole` retain their documented developer
semantics. The new menu reuses their selection parser, boot redirects and reset
lifecycle; it does not introduce another hole loader.

## Implementation evidence

The exact USA SHA-256 in `rom_identity.txt` is required. No ROM, font bitmap,
generated game source or recording is distributed. The picker decodes the font
already loaded into title-menu BG3 VRAM at runtime. Ability portraits are decoded
from the user's ROM at runtime; no extracted artwork is shipped.

That font sheet is not ASCII-ordered. Reading it out of the live title VRAM
(BG3, character base 5, VRAM word `0x5000`) gives digits at 16..25, the
hyphen at 29, the space at 31, `<` at 10, `>` at 12 and letters at 33..58.
ASCII-32 happens to agree with the digits and the letters, but it points at
the question mark for the hyphen, at `+` for `<` and at a solid block for `>`,
so the overlay addresses the sheet by its own indices
(`KdcPracticeGuestGlyph`). Before this correction the ability row read
`HI?JUMP`, with a cross for the left carousel arrow and a white block where
the right-hand arrow belonged.

The sheet does carry chevrons, at tiles 10 and 12. The ability carousel still
draws its two arrows as solid triangles, because that is what the mode row
already uses and the two arrow pairs should look alike. Tiles 0, 5, 8, 26, 30,
32 and 59..63 hold blanks, blocks and icons the glyph table does not name.

Local 65816 disassembly identifies these game-specific sites:

- `$83BBD8` finishes constructing a menu, before the tilemap upload. Stack
  returns `$BB46` and `$B744` identify the root mode menu. Append a choice to
  its `$EA8E`/`$EA97` coordinates and `$EAA0` return-value arrays, and write
  PRACTICE into the `$39DA` tilemap using the game's glyph indices. Other
  callers of the shared builder are unaffected. Unlocked root choices are
  retained, with practice appended after them. The count is at direct-page
  offset `$4A`, with observed D=`$1E00`; reading absolute `$004A` incorrectly
  prevented the entry from appearing in the first runtime probe.
- `$83B74E` receives the root menu result. Result 6 selects practice; redirect
  to `$83B720` on the original stack and rebuild the menu. Results 0–5 continue
  through the original code.
- The framework's `frame_begin` callback handles mapped controller input and
  consumes both seats' input while running the title underneath the picker.
  The practice pause menu holds simulation. Input that closes a menu is
  withheld until released.
- `$80F266` follows hole initialization, before player object creation at
  `$80C342`. Set `$D852/$D854` once to the selected ability (0–10). The guest
  copies the active player's ability into `$D9C4`; its activation routines and
  HUD continue to own subsequent changes.
- `$849E07 + ability * 4` selects the conditions portrait's DMA script. Its two
  256-byte strips supply the 32×32 4bpp portrait; `$94A849` supplies its palette.
  These are read-only host presentation sources, independent of title VRAM.
  Reviewing all eleven portraits against the verified USA ROM established this
  shared portrait/player ID order:

  | ID | Ability |
  | --- | --- |
  | 0 | None |
  | 1 | Stone |
  | 2 | Parasol |
  | 3 | Fireball |
  | 4 | Needle |
  | 5 | Tornado |
  | 6 | Wheel |
  | 7 | Spark |
  | 8 | Hi-Jump |
  | 9 | Freeze |
  | 10 | UFO |

  PR 53's labels differed from this order at IDs 4 through 10, while its portrait
  selection and starting-ability fields already used the guest IDs. The label
  table now follows the guest order, and the ROM-free carousel fixture checks
  every selection's name before starting and restarting with UFO (ID 10).
- `$80F48C` is the shared solo/multiplayer end-of-hole entry, before score
  screens and progression (including hole eight). Practice redirects to the
  ordinary title entry `$80F120` on the same main-loop stack. `$83B64B` skips
  waiting for title Start when the picker owns that backdrop. Choosing another
  hole uses a reset with the developer selector temporarily disabled.
- The existing [developer start hooks](dev-start.md#implementation-evidence)
  perform the start. Hooks are registered at initialization so selection can
  happen later. The pinned native configuration interprets these sites; future
  translation must preserve equivalent interception.

The REA provider refused the `.sfc` format. The disassembly above came from the
local 65816 tool, not REA analysis.

## Validation and limits

`tests/test_practice.py` checks root-menu targeting, all four mode families,
restart, held-confirm suppression, save isolation, exit and failed writes with
a ROM-free C fixture. Set `SNESRECOMP_ROOT` to the patched pinned framework.

The original PR 52 Windows Release build passed. Its Python suite ran 79 tests,
passing with 14 optional checks skipped. Its practice checks established:

- Controller-scripted starts: normal 8:8 (map 63), extra 8:8 (127), multiplayer
  4:8 (159), and extra multiplayer 4:8 (223), each after 600 settling frames.
- Native/interpreter final WRAM matched for normal 8:8 and extra multiplayer
  4:8. Two independent native normal 8:8 runs also matched exactly.
- Normal 1:1 start, restart, changing to extra multiplayer 4:8, and exiting to
  ordinary play passed. Ordinary SRAM and its file matched the pre-practice
  image byte-for-byte, including when quitting directly from practice.
- WRAM stayed identical across 30 paused menu ticks; B resumed play. Ordinary
  held input still passes through reset and state-load callbacks.
- A 240-frame zero-input boot ended with the same WRAM/SRAM as the pre-change
  executable. The original title menu, picker and HD practice pause menu were
  visually checked with local SDL-software captures. Captures remain local.

The ability/single-hole extension was checked on Windows on 2026-11-12:

- Release build passed using the pinned guarded framework and generated inputs.
  The full Python suite ran 82 tests, passing with 18 optional checks skipped.
- All eleven starting abilities reached the expected player fields and active
  ability after 600 settling frames. Both player slots receive the selection.
- The new animated-picker regression failed against the PR 52 executable and
  passed after the fix: framebuffer/WRAM changed across 90 idle picker frames
  and the APU clock advanced. A presented SDL-software capture showed the
  selected portrait, carousel arrows and five-row picker without overlapping labels.
- Completion fixtures covered normal 1:1 and 8:8, and extra multiplayer 1:1 and
  4:8. Each fixture first discovers the live shot task, repeats identical input
  and verifies matching WRAM, then marks that shot and player completion state
  (`$DA4A/$D956/$D958`). The original main loop returns to the picker, and A
  replays the same map with the selected ability restored. These are transition
  checks; they do not establish natural hole completion or shot physics equivalence.
- Restart, choosing another hole, pause/resume, save isolation, native/fallback
  starts and the pre-change ordinary-boot comparison passed again. The ROM-free
  fixture also covers both seats' held-confirm suppression and ability wrapping.

The label-order follow-up reviewed all eleven portraits from the verified ROM.
The carousel regression failed before the fix at ID 4 (Needle labeled Spark),
and a focused UFO check failed at ID 10 (labeled Hi-Jump). Both passed after
correcting the table. A fresh Windows Release build and the 82-test standard
suite passed (18 optional skips). All seven practice checks then passed with
the real ROM, including every starting ability, UFO completion/replay, and
ordinary-boot WRAM/SRAM equality against the PR 53 executable.

The font-sheet follow-up was checked on Windows on 2026-11-12 with a fresh
Release build of the same pinned framework:

- The guest's title-menu font sheet was decoded out of the live title VRAM,
  all 64 tiles, and rendered as an image so each glyph could be read rather
  than guessed. Its own indices are digits 16..25, hyphen 29, space 31,
  `<` 10, `>` 12 and letters 33..58, which is why the ability row read
  `HI?JUMP` and drew a cross and a solid block for the carousel arrows.
  The sheet region is byte-identical at the title screen, where the picker
  captures it, and at the picker.
- Headless SDL-software captures of the presented frame (the host's own
  screenshot path, not the bare guest field) show `HI?JUMP` with the cross and
  the block before the change, and `HI-JUMP` with two triangular arrows after
  it. Other labels, the course and hole digits and the `FIREBALL` entry were
  captured too and are unchanged.
- The ROM-free fixture now pins the sheet's indices for space, hyphen,
  question mark, `<`, `>`, `=`, `+`, the digits and the letters, and that
  characters the sheet does not carry are reported rather than guessed.
- The standard suite passed (88 tests) and all six real-ROM practice checks
  passed with the new executable: every starting ability, the completion and
  replay transition for all four mode families, restart, hole change, exit,
  save isolation and the animated picker. The ordinary-boot comparison stayed
  skipped because no pre-change executable was supplied.

Run `python -m unittest discover -s tests -p test_practice.py -v` with
`SNESRECOMP_ROOT`, `KDC_TEST_PRACTICE=1`, `KDC_PRACTICE_ROM` and
`KDC_PRACTICE_EXECUTABLE` set to local inputs. Set `KDC_PRACTICE_BASELINE` to a
pre-change executable to include the ordinary-boot comparison. The runtime
tests use temporary isolated saves and disable physical controller input.

Practice intentionally diverges from the reference menu and boot flow. The
existing ordinary-boot reference divergence remains documented in
[compatibility](compatibility.md); this feature is not evidence of improved
emulation accuracy. The first previously observed mismatch is host frame 15
versus reference frame 16; this work did not rerun reference alignment for the
intentionally different practice flow. Natural/exhaustive hole completion, every multiplayer turn, every
unlocked/debug menu combination, physical controller hotplug and non-Windows
hosts are not qualified by these bounded checks.

Run-ahead remains unsupported, as elsewhere in this project. Practice UI state
is host-owned and is not rolled back during speculative execution; do not
enable run-ahead through configuration or environment overrides.
