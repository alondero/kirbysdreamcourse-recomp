# Root mode-menu contract

Investigation for [#82](https://github.com/alondero/kirbysdreamcourse-recomp/issues/82).
Keep the root mode menu interpreted and retain the existing enhancement hooks.
Stock ROM traces identify the construction and dispatch boundaries. The builder
owns resumable input and upload work, and this checkout uses its pre-upload hook
for the separate two-player MORE COURSES option. A root-only replacement would
still depend on the builder and would need to preserve mod callbacks across
interpreted and native dispatch. The remaining verification costs exceed the
hook coupling this extraction would remove.

## Pins and initial conditions

| Input | Identity |
| --- | --- |
| Project under test | `141bfbe6972c88b45a4e51b332c0d0cb258806ec` |
| Issue audit baseline | `e18a4f753f54ed8bedf015f7bd634d26f6fc4241` |
| Enhancement source Git blob | `b8d98d5913f4d185a8ab22f76af89965d7767a87` (`src/practice.c` at the project pin) |
| Framework | `147b55e252dcb3e7680a99c7c2f50e7183891844` plus guarded setup patches |
| Headerless USA ROM SHA-256 | `0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b` |
| Snes9x 1.63 source | `fae2fea08f74180759ef540ee94259213f503480` |
| Baseline Snes9x DLL SHA-256 | `72e0f16839c0298d8ce52eb2281e83feaede49af73936a39bb782b0f488e92e0` |
| Diagnostic Snes9x DLL SHA-256 | `34dfe26e3fdc75150931c7e64c6aa548b3323a051961525b38d0c67b3253f4b7` |
| Windows guarded executable SHA-256 | `ad6459f040f0c8c67ff013dfa034bc1a7b78567e904c4388636a7fc0b2ec1035` |

The root table, WRAM arrays, direct page and tilemap use the original guest
addresses in the reference and host. The tracked `recomp/` and `mods/preloaded/`
trees match the audit baseline. These addresses have identity relocation.
Physical LoROM mirroring and bridge callback matching follow different rules.
Host runs used fresh staged installs, default preferences, the stated initial
SRAM and an empty mod selection, with runtime mod support enabled.

## Owning decision and stack

The title's call at `$80:F141` enters `$83:B557`. Its title setup reaches
`$83:B702` via `$83:B6B7`. `$83:B71C` calls `$81:C7AF` to rebuild unlock words
`$EAA9..$EAB3` from progress data without writing SRAM. `$83:B720` copies
`$EA88` to `$EA86`, clears `$EA8A`, and ORs `$EAAD/$EAAF/$EAB1/$EAB3` to select
the descriptor at `$83:B78E` or `$83:B7A7`. The longer descriptor separately
gates MAP SELECT, SOUND ROOM and DANCE SELECT on the first three words.
`$EAB3` alone would select the longer descriptor by this branch, but the
unlock routine's threshold for `$EAB3` also implies `$EAAF` and `$EAB1`.

At `$83:B742`, `A` is the descriptor offset and `Y` is its bank. The short
call to `$83:BB44` returns to `$83:B745` with the selection in `A`. The caller
pushes it, invokes `$80:B542` with `A=$0034` and pulls the selection at
`$83:B74D`. `$83:B74E` is the next instruction in this flow. It copies `$EA86`
to `$EA88`, clears `$EA86`, increments `$EA8A`, and rejects unsigned results
of 6 or more at `$83:B75D`. The jump at `$83:B761` indexes `$83:B764`. At the
jump `A=X=2*result`, C and N are clear and Z is set for result 0. Remaining
register and flag liveness needs caller-by-caller qualification.

Let `S_R` be S after the title's long call. The observed paths have `S_R=$1FFC`
at entry, rebuild, builder return and root dispatch, with saved return word
`$F144` and bank `$80`. The short call to `$BB44` pushes `$B744`. Its own call
to `$BB4A` pushes `$BB46`. At `$BBD8`, `S=S_R-4`, and those two words are above
the original long frame. `$BBEB` pops `$BB46`, then `$BB47` tail-jumps to
`$BBEC`. `$BC10` returns the selected byte zero-extended in A. When B is enabled,
`$BC23` returns `$FFFF`. Each pops `$B744`. The root result path ends in a tail
dispatch. Original paths into `$80:90C1` consume the long return to `$80:F145`.
Map select can instead transfer onward to `$82:F903` on that frame.

## Caller and entry ledger

Addresses without a bank below are in `$83`. Supported means decoded and
observed on the stated route. Excluded means outside a root-only extraction
and left on the original guest path.

| Edge or entry | Classification and evidence |
| --- | --- |
| `B6B7 → B702 → B71C → B720` | Supported by stock boot, unlock return and root setup. |
| `B7F8`, `B847`, `B895`, `B98A`, `BA09` → `B720` | Supported by stock one-player, two-player, dance type, map-select and sound-room cancellation. These rebuilds skip the unlock call. |
| `B742 → BB44 → BB4A` | Supported for both root descriptors. One shared builder call per root build. |
| `B74E`, `B761` | Supported as sequential continuation and internal dispatch. Independent entry remains unresolved. |
| `B7E7`, `B833`, `B87F`, `B89E`, `B8B2`, `B961` → `BB44` | Excluded. Stock routes reached the one-player course, two-player course, dance type, one-player dance, two-player dance and map course type builders, respectively. |
| `B9AF → BB4A`, `B9C2 → BC51` | Excluded. Sound room calls construction directly and has its own input loop and cursor calls. |
| `B873` demo continuation | Excluded. Its first continuation has no shared builder call. |
| Host redirects at `B74E` → `B720` | Supported on current host. They use the title's stack. Frozen replay of these interventions in the independent core remains open. |
| Other indirect, mirrored or interior entries | Unresolved. Keep the interpreter. |

| Result at `B74E` | Destination | Stock coverage |
| --- | --- | --- |
| 0 | `B7D8` | One-player course submenu and B back to root |
| 1 | `B824` | Two-player course submenu and B back to root |
| 2 | `B873` | Demo selection continuation |
| 3 | `B95B` | Map-select course type and B back to root |
| 4 | `B9A5` | Sound room and B back to root |
| 5 | `B879` | Dance type, both player-count branches and B back to root |

The verified 1 MiB image's long-immediate JSL/JML byte scan found no direct
far encodings into `B702..B763` or `BB44..BC87`, including physical aliases.
Same-bank direct candidates in the scoped regions resolved to the decoded
edges or to bytes within instruction operands or data. A heuristic traversal
from the vectors reached the known callers and left ten unresolved
single-indirect sites across the ROM. Their destinations, bank-changing
paths and possible interior entries preclude a closed caller set. The local
decoder came from [sp00nznet/snesrecomp at `2583dd0`](https://github.com/sp00nznet/snesrecomp/blob/2583dd0b30d149e44315c4bf4915979b53389e42/tools/disasm65816.py).
The static pass set LoROM mapping and decoded the observed root entry with
`m8=False, x8=False`. It combined the vector-based `callgraph.py` crawl with
full-image JSL/JML immediate scans and bank-slot-3 JSR/JMP and relative-target
scans. The unresolved single-indirect sites in that crawl were
`$80:8417/9B0D/9D98/E434`, `$81:8133/844E`, `$87:80B5/81AD/966C` and `$DA:54B8`.
The last address may be a traversal false positive.

The bank-3 upper ROM bytes have physical aliases at banks
`$03/$23/$43/$63/$83/$A3/$C3/$E3`. The full-ROM banks `$43/$63/$C3/$E3`
also expose those bytes in the lower half. Same-bank branches, PB-based
addressing and direct calls make physical aliases insufficient evidence of
equivalent behavior. The stock runs entered `$83` only. Bridge callback keys
clear bit 23 and callbacks receive the original PC. For these sites that
pairs `$03` with `$83`. Guarded native dispatch checks that pair against the
mapped ROM backing. New native entries would require separate tests.

## Shared builder, memory and suspension

The observed entry state is native mode with M=X=0, `D=$1E00` and `DB=$7E`.
The builder consumes `A` and `Y`, stores the descriptor pointer at
`D+$46/$48`, zeroes `D+$4A` and clears `$7E:39DA..41DB` through `$83:BACC`.
Descriptors initialize coordinates at `D+$40/$42`. Rows gated by a long
predicate can be omitted while their peers keep their assigned result bytes.
X, Y and result bytes begin at `$EA8E/$EA97/$EAA0`. Construction uses
overlapping word stores indexed by row. Drawing and cursor code use the
hardware multiplier at `$4202/$4203` and `$4216/$4217`.

`$BBD8` is reached after row construction and before the X-array sentinel.
Its first read is the 16-bit count at `D+$4A`. The builder writes a single
`$FF` at `$EA8E+count` and calls `$80:8696` with `A=$BB3B`. This is the
narrowest observed place to change the row count, arrays and tilemap before
the upload. The sound-room path has only one short call frame here.

The upload puts `$580` bytes from `$7E:39DA` into VRAM word address `$5800`.
The usual path enqueues an eight-byte record via the ring at `$0200`. Its
tail, head and byte budget are `$0022/$0024/$0026`. In the locked run the
head and byte budget advance from zero to `$0008/$0580` between `$BBD8` and
`$BBEB`. Frame 502's NMI then issues channel 0 DMA to `$2118/$2119` in mode 1.
Static code also has queue-full and budget waits and an immediate channel 1
DMA path selected by a negative word at `$007D`. The upload temporarily
changes D and DB and restores them. Only the normal queue path was observed
for the root upload in these runs.

`$BBEC` draws the cursor via `$BC51`. The poll at `$BBEF` calls `$80:8F5B`.
Its wait is paced by `$004D` in the NMI path, with a vblank-status path via
`$4212` when NMI is disabled. The waiting guest keeps the builder caller's
stack frame over many frontend frames. NMI saves A/X/Y/D/DB and drains
the queue before restoring interrupted execution. In the idle trace the
locked menu builds at frame 501, processes B at frame 659 and resumes
polling on the same frame with S still `$1FFA`. The stock root returns only
after a valid selection on the covered path. These traces establish guest
suspension through the stock loop. The host already declares idle-poll
sites at `$80:8471` and `$80:86FC`. A native replacement's equivalent yield,
resume and pending-queue obligations remain unqualified.

The identified read-before-write inputs include `$EA88`, retained unlock
words on rebuild, slot progress on the unlock path, descriptor ROM data and
predicate words, controller state, `$007D`, the NMI counter and the live queue
head, tail and budget. Persistent writes include the unlock words, cursor
and cancellation state, row arrays, tilemap, cursor-rendering data and live
queue records. This is a bounded input/write inventory. Whole-WRAM snapshots
include the stack and direct page. Dead scratch exclusions would require
read-before-overwrite proof across every supported return and resume.

## Enhancement ownership and runtime ordering

At `$BBD8` the current host requires returned PCs `$BB46/$B744` on the stack
and `S <= $1FFB` for the root. It reads `D+$4A`, validates the count and
coordinates, then appends PRACTICE and 1P VS CPU or replaces the rows with
difficulties 8–10 and enables B. The `$BB46/$B835` case appends MORE COURSES to
the distinct two-player menu when configured. That separate path continues
through its result hook at `$B836`.

At `$B74E`, the host consumes PRACTICE, CPU selection and difficulty
cancellation with redirects to `$B720`, or sets `A=1` for accepted
difficulties before the guest executes the normal two-player continuation.
Those redirects precede the stock cursor persistence. In particular, the
CPU-selection intervention zeros `$EA86` and `$B720` then reloads it from
the pre-existing `$EA88`. A replay that always starts the difficulty cursor
at zero can miss this state dependence. Practice input suppression keeps the
guest waiting underneath the host picker while the title continues animating.

An extraction could remove the root's stack fingerprint and concentrate the
`$B74E` accumulator override and `$B720` restart policy at an explicit result
interface. It would still need a root-specific construction callback before
the shared upload. The two-player `$BBD8/$B836` hooks, save activation at
`$B83E`, practice hole/title hooks, CPU control and host picker remain adjacent
owners. SRAM isolation, ordinary-save preservation, picker state and input
suppression belong to the host. Guest navigation, unlock handling, glyphs,
rendering, DMA and scheduler behavior belong to the guest and shared runtime.
The current production registration and interpreter paths are retained.

The tracked catalog is empty. The package activation surface at this pin is
the compiled-in plugin registry, and this project has no activation plugin
registered at these boundaries.
The first-frame game initialization registers sprites, course packs,
developer start, practice, CPU and transitions in that order. A trusted
activation callback could already be installed before this initialization.
The bridge invokes pre-opcode callbacks in registration order, synchronizes
their CPU state changes and stops the sequence when one redirects. A local
synthetic JSR test used conflicting and non-conflicting callbacks. Interpreted
dispatch preserved order, register mutation, redirect and stack balance.
Dispatch from the same interpreted JSR into a paired native body bypassed
all target callbacks. An interpreter target exclusion restored their order
for that bounce. Direct native dispatch and generated C transfers would each
need a compatibility mechanism. The existing bridge fixture also exercises
the registration order, mirror deduplication and redirect short-circuit.
[Pinned package format](https://github.com/RetroPortingToolKit/snesrecomp/blob/147b55e252dcb3e7680a99c7c2f50e7183891844/docs/MOD_PACKAGES.md), [bridge fixture](../tests/bridge_diagnostic_cache_probe.c).

## Independent stock verification

The [diagnostic patch](../tools/diagnostics/root-menu-reference.patch) applies
to the [libretro Snes9x source pin](https://github.com/libretro/snes9x/commit/fae2fea08f74180759ef540ee94259213f503480).
Patch SHA-256 is `0d9f203b5d4190aa600863e362ad1ebd0bbcf48d1f402e7da3198a4b624a8adf`.
It records PC, predecessor, A/X/Y/S/D/DB/P and stack words around the root
and shared builder, plus selected external PC ranges. Numbered snapshots
contain all 128 KiB WRAM followed by 8 KiB SRAM. MMIO handler writes and DMA
descriptors are recorded in execution order from frontend frame 500 onwards.
The DMA fast paths can bypass `S9xSetPPU`. Trace coverage is therefore DMA
descriptors and handler-visible writes. Use one process per capture.

The local diagnostic build used MinGW-w64 GCC 15.2.0 with GNU Make 4.4.1.
From the project root, after checking out the exact source in ignored `local/`:

```powershell
$probe = 'local/root-menu/snes9x'
$patch = (Resolve-Path 'tools/diagnostics/root-menu-reference.patch').Path
git -C $probe apply --check --index --unidiff-zero $patch
git -C $probe apply --unidiff-zero $patch
mingw32-make -C "$probe/libretro" platform=win -j8
$compilerDir = Split-Path (Get-Command gcc).Source
Copy-Item "$compilerDir/libwinpthread-1.dll" "$probe/libretro"
```

Example locked root and B rejection, with a fresh output prefix per run:

```powershell
$core = "$probe/libretro/snes9x_libretro.dll"
$sha = (Get-FileHash $core).Hash.ToLowerInvariant()
$prefix = Join-Path (Resolve-Path local/root-menu).Path 'idle-a'
$env:KDC_ROOT_TRACE_FILE = "$prefix.log"
$env:KDC_ROOT_TRACE_MEMORY = "$prefix.bin"
python tools/reference_capture.py --rom "Kirby's Dream Course (USA).sfc" `
  --core $core --core-sha256 $sha --frames 850 --press 501:2:start `
  --press 659:2:b --image-frame 658 --image-frame 700 --output "$prefix.json"
```

Repeat with an `idle-b` prefix. Use `local/reference/snes9x_libretro.dll` with
the default SHA-256 check for stock and clear both trace variables. Compare
the two diagnostics' logs and binary snapshots byte for byte. Compare the
`frames` arrays and PPM captures across all three processes. The JSON outer
metadata records the core identity and can differ between binaries.

All selection routes ran for 850 frames. For result `n`, hold Start for two
frames at 501, then Down for two frames at `561+20*i` for `i=0..n-1`. Hold A
for two frames at 679 and B for two frames at 782. The dance branch runs
replace B at 782 with A, then hold B at 815 and 834. Add Down at 760 for
the second dance branch. The reset run uses Start at 501 and `--reset-frame
700`. For results 3–5, pass the controlled SRAM seed. The other routes use
zeroed SRAM.

To reconstruct the controlled seed, boot the baseline from zeroed SRAM
through frame 650 with `--press 320:2:start` and `--sram-frame 650`.
Its SHA-256 is `127799c0e43047fb31267f4c06060457b01e56eabf1757acaa7a998d69aa5794`.
For member slot 4, write little-endian word 1 at `$0018` and set the low two
bits of each of the 16 words at `$031A+4*i`. Store the checksum at `$0216`.
For slot `s`, the checksum is `$1357` plus the words at `$0010+2*s` and
`$001A+2*s`, all 32 words starting at `$0218+$40*s`, and all seven words of
each of 400 fourteen-byte records starting at `$0358+14*i` whose first byte
equals `s`. Reduce modulo 65536. The controlled seed SHA-256 is
`231b49d5bf46c4b93f2dcabd8ebff434836351b71212d306f78a90049a60e034`.
The reference preserved those 16 progress words and exposed all six rows.

Ten stock scenarios ran twice on the diagnostic build. Every pair matched
byte for byte across the ordered trace and full boundary memory captures.
For each of the ten, all 850 per-frame video, audio, WRAM and SRAM records
also matched the baseline DLL. The locked and unlocked root screenshots
showed three and six stock rows, respectively. This establishes that the
observer preserved the stock paths covered here. The enhanced parity lane
would still need the pinned host interventions and their side effects
replayed against this original-ROM core via an independently checked adapter.

The current host and stock reference still diverge during the no-input boot
with identical zero SRAM. The first WRAM mismatch is at host frame 15
(zero-based) and Snes9x frame 16 (one-based) in both host modes. There are six
differing bytes at `$7E:00A7` and `$7E:1FEE..1FF2`. The first is `$0A` on the
host and `$0B` on Snes9x. No controller input preceded it. This comparison
uses every WRAM byte. The first differing operation was outside this menu
probe's scope, and crossing the menu boundary later does not explain it.

## Current Windows host verification

`./tools/setup.ps1` built the current worktree. The normal
`tools/run_game_scenario.py` entry captured locked and controlled unlocked
root menus, difficulty B-cancel, practice picker with animated backdrop,
and the Hard CPU two-player submenu and return. Native and interpreter
runs matched WRAM across the retained intervals and matched final SRAM and
composed PNGs. A native difficulty-cancel repeat matched all 111 captured
WRAM frames. The locked root had three stock rows plus the two additions.
The unlocked root had six stock rows plus the two additions. The difficulty
view produced result bytes 8, 9 and 10 before B restored the locked root.
A scripted nonzero-cursor round trip selected 2P and cancelled its submenu.
Entering 1P VS CPU then presented MEDIUM (row 1) because the rebuild reloaded
the retained `$EA88`; B restored the five-row root with row 1 selected and
byte-identical SRAM in native and interpreter modes. Separate native inputs
and screenshots confirmed each stock destination.
A natural first Hard CPU turn followed by pause EXIT MATCH reset to the
root and restored the ordinary SRAM byte for byte.

Run the existing live fixtures and ordinary menu route from the project root:

```powershell
$env:SNESRECOMP_ROOT = (Resolve-Path local/guarded-framework).Path
$env:KDC_PRACTICE_EXECUTABLE = (Resolve-Path local/guarded-build/KirbyDreamCourseRecomp.exe).Path
$env:KDC_TEST_PRACTICE = '1'
$env:KDC_TEST_CPU = '1'
$env:KDC_TEST_DEV_START = '1'
foreach ($suite in 'practice', 'cpu_opponent', 'dev_start', 'bridge_diagnostic_cache') {
    python -m unittest discover -s tests -p "test_$suite.py" -v
}
python tools/run_game_scenario.py scenarios/menu-to-hole.json --exe $env:KDC_PRACTICE_EXECUTABLE
python tools/run_game_scenario.py scenarios/menu-to-hole.json --exe $env:KDC_PRACTICE_EXECUTABLE --fallback
```

Practice passed six tests with one pre-change executable comparison skipped.
CPU passed seven, developer starts passed two, and the bridge fixture passed.
The pinned reference state suite passed three with `KDC_TEST_REFERENCE=1`.
Both menu-to-hole runs passed four checks and matched final WRAM, SRAM and
PNG. Focused ROM-free practice, CPU, sprites, developer start and transition
fixtures passed twelve tests before opt-ins. The live transition recording,
optional 2P MORE COURSES, installed external mods and non-Windows hosts
were outside these host runs.

## Extraction gates

| Gate | Status |
| --- | --- |
| Root owner and pre-upload seam | Established for the observed title caller, six result continuations and five original rebuilds. |
| Shared-builder callers | Seven wrapper paths and the direct sound-room path observed. Complete indirect and interior-entry closure remains open. |
| Entry and suspension compatibility | Stock stack, registers and normal NMI upload observed. Mirrored execution, all live outputs, full-queue waits and candidate bridge resumption remain open. |
| Independent reference | Repeatable original-ROM lane. A frozen enhanced adapter and cross-implementation boundary comparison remain open. |
| Persistent state and rendering | Full stock checkpoints and ordered observed commands retained locally. End-to-end DMA/APU payload and frame equivalence for a replacement remain open. |
| Mods | Interpreted order and a native bypass reproduced synthetically. Replacement compatibility across dispatch routes remains open. |

The observed seam supports keeping the existing scoped callbacks. A native
replacement would require both a reviewed complete caller and liveness
contract and a reference comparison preserving the full machine state and
the current enhancement effects. Neither gate passed in this investigation.
The ROM, save fixtures, generated code, captures and traces stay under ignored
local paths.
