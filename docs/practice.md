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

During practice, Start opens **RESUME**, **RESTART HOLE**, **CHOOSE A HOLE** and
**EXIT PRACTICE**. B resumes from that menu. Ctrl+R also restarts the selected
hole. Exiting practice resets to the ordinary title screen. The selection is
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
prevent guest state from crossing the ordinary/practice save boundary. Hole
completion and course progression remain the original game's behavior inside
the practice save directory. Restart always uses the selected hole, even after
a transition. Choosing another hole replaces that target.

The CLI `--dev-hole` and PowerShell `-DevHole` retain their documented developer
semantics. The new menu reuses their selection parser, boot redirects and reset
lifecycle; it does not introduce another hole loader.

## Implementation evidence

The exact USA SHA-256 in `rom_identity.txt` is required. No ROM, font bitmap,
generated game source or recording is distributed. The picker decodes the font
already loaded into title-menu BG3 VRAM at runtime.

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
  to `$83B720` on the original stack, rebuild the menu and freeze at the next
  simulation boundary. Results 0–5 continue through the original code.
- The framework's `frame_begin` callback handles mapped controller input and
  pauses simulation while the picker or practice pause menu is open. Input
  that closes a menu is withheld until released.
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

The Windows Release build passed. The full Python suite ran 79 tests, passing
with 14 optional checks skipped. With `KDC_TEST_PRACTICE=1`, the four practice
tests also passed against the verified ROM:

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
intentionally different practice flow. Exhaustive hole completion, every multiplayer turn, every
unlocked/debug menu combination, physical controller hotplug and non-Windows
hosts are not qualified by these bounded checks.

Run-ahead remains unsupported, as elsewhere in this project. Practice UI state
is host-owned and is not rolled back during speculative execution; do not
enable run-ahead through configuration or environment overrides.
