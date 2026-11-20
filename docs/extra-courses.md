# Extra 2P courses and Grump's Dream Course IPS support

Enable **Extra 2P courses** on the launcher's **Mods** page (saved in
`kdc-courses.cfg` beside the executable; off by default). When enabled:

- Both **2P GAME** and **1P VS CPU** append **MORE COURSES** below
  **PLAY COURSE** (and **EXTRA COURSE**, when unlocked) on the two-player
  submenu. Regular two-player courses and their saves remain unchanged.
- **MORE COURSES** opens a host-rendered picker over the animated menu
  background offering:
  - **1P NORMAL** (Courses 1–8 from your verified USA ROM)
  - **1P EXTRA** (Extra Courses 1–8 from your verified USA ROM)
  - Any valid external IPS course pack placed in `courses/` beside the
    executable (or supplied via `--course-pack PATH`).
- After choosing a course, the original **Order Roll** and **Handicap Grid**
  follow, then all eight holes and the two-player finale play through the
  original game engine (including widescreen, orbit camera, HD textures,
  smooth presentation, and the 1P vs CPU opponent). Finishing a course or
  backing out of the handicap grid returns to the extra-course picker; B exits
  to the title menu.
- Loaded external IPS packs also appear as 1P and 2P modes in **PRACTICE**
  (with starting ability selection, restart and completion return-to-picker)
  and via `--dev-hole pack:ID:COURSE:HOLE` / `pack-2p:ID:COURSE:HOLE`.

## Activating Grump's Dream Course

This repository does not ship ROMs, IPS patches or extracted level data. To
enable the 19 custom courses (152 holes) from **Grump's Dream Course**:

1. Supply your verified headerless 1 MiB USA ROM as usual.
2. Obtain `GrumpsDreamCourse[1.01].ips` (for example from
   [archive.org/details/grumps-dream-course](https://archive.org/details/grumps-dream-course)).
3. Place the `.ips` file in a `courses/` directory beside the executable (for
   example `local/guarded-build/courses/GrumpsDreamCourse[1.01].ips`), or pass
   `--course-pack <path>` (or `-CoursePack <path>` to `tools/play_experimental.ps1`).
4. Enable **Extra 2P courses** on the launcher's **Mods** page (passing
   `--course-pack` enables it automatically for that launch).

### Technical notes and limits

- Although `GrumpsDreamCourse[1.01].ips` originally targets the Japanese
  *Kirby Bowl* ROM, inspecting the IPS shows that all 11 map-stream pointer
  tables (`0x052670..0x054770`, 152 entries each) point exclusively into the
  expanded ROM banks `$A8..$B5` (`0x140000..0x1AAE3B`), where the IPS supplies
  100% of the bytes. Course names (`0x04652A`), palette indices (`0x05425`),
  animated palette indices (`0x204AF`), theme permutation (`0x050AF`) and
  per-hole music indices (`0x007440`, mapped from JP to USA track IDs) are also
  self-contained in the IPS. **No Japanese ROM is required.**
- Because the IPS's modified sprites (Arin/Dan), title/credits graphics,
  custom BG3 scenery CHR tiles and custom SPC tracks are binary deltas against
  the Japanese ROM, those cosmetic assets are not applied. The 19 courses (152
  holes) play with the USA ROM's Kirby/Keeby sprites, vanilla scenery themes,
  authentic vanilla palette assignments and mapped vanilla hole music.
- Custom 2P matches and direct `--dev-hole pack:*` / `pack-2p:*` starts
  isolate battery writes into `saves/custom/save.srm` (or `saves/cpu/save.srm`
  for 1P vs CPU) and leave ordinary `saves/save.srm` untouched. Save states and
  rewind remain disabled during custom sessions and custom developer starts.
- Built-in 1P courses in 2P read the USA solo music table (`$80:C9D5..$80:CA14`)
  from the verified runtime ROM rather than storing ROM tables in source.
- Drop-in packs are discovered from `courses/` beside the executable (independent
  of the caller's working directory; `--course-pack-dir PATH` overrides this
  directory, which `tools/play_experimental.ps1` uses to preserve the selected
  install's catalog while isolating `local/dev-play/saves`), while explicit
  `--course-pack PATH` arguments resolve relative to the caller's working
  directory. Non-regular files (directories, named pipes/FIFOs, devices) are
  skipped before opening. All regular `.ips` candidates are sorted and
  validated structurally (including exact `width × height` lengths for gameplay
  planes 1–4 and a tee within the grid); rejected files do not consume catalog
  capacity.

## USA integration evidence

ROM identity verified before investigation: SHA-256
`0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.

- 2P submenu (`$83:B824`): `$83:BBD8` with stack return `$BB46/$B835` appends
  `MORE COURSES` (result `2`) when enabled; `$83:B836` opens the host picker.
- Custom 2P boot: `RtlReset(1)` -> `$80:F0FF` sets `$17FF = 1`, `$EAB9 = 0`,
  redirects to `$80:F1DB`. `$83:D518` sets proxy course `$6A5A = 16`,
  `$6A5C = 32`, `$D7A4 = 2`, `$D7A2 = 0` and redirects to `$83:D6D6` (`RTL`),
  proceeding into `$83:DB41` (Order Roll & Handicap Grid). Returning to
  `$83:D518` after Hole 8 or B-cancel reopens the picker via `$80:F120`.
- Hole loading (`src/course_pack_runtime.c`, disabled outside custom courses):
  - Built-in 1P courses in 2P: `$80:C168` sets `$ABB2/$ABB4/$ABB6` to the
    selected USA 1P map (`0..127`) so the original loader reads all 11 streams.
  - External IPS packs: `$82:B1CA` (redirect `$82:B270`) and `$82:B14B`
    (redirect `$82:B1C9`) decompress all 11 HAL-LZSS streams (`$80:9F18`
    format) into WRAM `$7E`/`$7F`.
  - Per-hole music (`$80:C988` -> `$80:C999`), course palettes
    (`$80:D843`/`$80:D85B`), animated palettes (`$84:82B2`), scenery/color
    ramp/weather (`$80:D6E8`, `$80:D703`, `$80:D718`, `$80:D733`, `$80:D4FD`,
    `$84:CD38`), and skipping the vanilla 2P intro banner (`$86:FC95` ->
    `$86:FCFF`).

## Validation

Run ROM-free unit tests (with `SNESRECOMP_ROOT` and `RECOMP_UI_ROOT` set):

```powershell
python -m unittest discover -s tests -p 'test_course_pack.py' -v
python -m unittest discover -s tests -p 'test_practice.py' -v
python -m unittest discover -s tests -p 'test_launcher_settings.py' -v
python -m unittest discover -s tests -p 'test_dev_start.py' -v
```

Run opt-in real-ROM checks (including live `GrumpsDreamCourse[1.01].ips`
verification when `KDC_TEST_GDC_IPS` points to a local copy):

```powershell
$env:KDC_TEST_COURSE_PACK = '1'
$env:KDC_TEST_GDC_IPS = 'C:/path/to/GrumpsDreamCourse[1.01].ips'
python -m unittest discover -s tests -p 'test_course_pack.py' -v
```

### Reference comparability and untested paths

- **Comparable reference scenario:** With **Extra 2P courses** disabled
  (the default), ordinary zero-input boot remains byte-identical in WRAM and
  SRAM to the pre-change executable, preserving the pinned Snes9x 1.63 baseline
  first divergence (host frame 15 versus reference frame 16, recorded in
  [compatibility.md](compatibility.md)).
- **Custom-course verification:** Because custom 2P menus, single-player
  courses in 2P, and Grump's Dream Course IPS injection intentionally diverge
  from the unmodified USA ROM flow, those paths are verified by comparing the
  host's guarded native path against its interpreter fallback (`SNESRECOMP_LLE_BOUNCE=0`),
  plus a deterministic 8-hole progression, 2P finale, return-to-picker, and
  B-to-title replay (`test_custom_2p_full_eight_hole_progression_and_finale`).
  That 8-hole replay drives hole-clear state transitions rather than natural
  shot-by-shot play across all 152 holes.
- **Untested paths:** Linux and Android builds (including POSIX `opendir`
  directory scanning and Android external-storage `courses/` discovery),
  natural shot-by-shot completion of all 152 holes, and physical two-controller
  play were not executed in the Windows verification runs.
