# Widescreen sprite investigation

Status: horizontal-margin implementation, 2026-11-01. The investigation below
motivates the implementation and validation recorded at the end of this file.
Whole-hole viewing and complete object/effect coverage remain unqualified.
See [the current widescreen implementation](widescreen.md) and
[the architecture's validation contract](../architecture.md).

## Finding

Extending the PPU's visible rectangle is insufficient. The game rejects draw
requests before OAM construction, then clips and packs individual sprite pieces
into hardware coordinates. The current course view also deliberately preserves
native OAM interpretation and left-edge clipping. Removing that policy alone
cannot recover requests the game never emitted.

The following addresses were inspected in the locally supplied USA ROM after
verifying SHA-256
`0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.
These are game-specific findings, not generic SNES behavior.

| ROM routine | Observed behavior |
| --- | --- |
| `$80:E55A` | Updates object position, subtracts camera words `$0086/$0088`, and rejects origins outside signed X `[-48,296)` or Y `[-32,280)` before queueing a sprite layout. |
| `$80:E5A2` | Updates object position, then tests screen coordinates directly against X `[-48,296)` and Y `[-32,280)`. |
| `$80:E60B`, `$80:E635` | Helpers applying the same bounds to supplied coordinates, with and without camera subtraction respectively. |
| `$80:9E8E` | Actor path with X `[-48,296)` and Y `[-32,280)`, matching `$80:E55A`. Enters the queue at `$80:8B02`. |
| `$80:9EBD` | Actor path with X `[-24,296)` and Y `[-24,280)`. Enters the queue at `$80:8B02`. |
| `$80:8B02`, `$80:8B09`, `$80:8C08` | `$8B02` stores its inputs and falls through to `$8B09`; the queue and consumer use ten ordered sprite-layout buckets beginning at WRAM `$2884`, spaced `$182` bytes apart. |
| `$80:8D89` | Expands layouts into OAM. `$8D8B` checks the allocation limit with `CPX $32`; `$8DD1-$8DD9` accepts only final piece Y (origin plus piece offset) in `[-32,224)`. The routine packs X into hardware coordinates. |

Do not globally remove comparisons against viewport-like constants. For example,
`$82:E26B` also performs viewport tests in a separate routine whose gameplay role
has not been qualified for modification.

## Local evidence

Two 8,200-frame native runs used the course-three recording and initial SRAM
identified in [widescreen validation](widescreen.md). The recording SHA-256 is
`50b521dbdf8ce53e62d9db37a5a73a079f683163838ba977cb51ce55560e5a1b`;
initial SRAM SHA-256 is
`278e7bb6bc4c789319fe7b4d8202102c88971e28258be23610fb0c500b44c354`.
The executable SHA-256 was
`3a062c076dc566dc3c374aced81d31dfd4099333033ff906325c2712206b3aea`.
Both runs enabled the three APU timing switches documented in
`tools/play_experimental.ps1`, native bounce, and 272 extra columns per side.

All ten retained WRAM dumps and complete images at host frames 8190-8199 matched
byte for byte. No first divergence occurred within that sampled interval. This
checks repeatability, not emulator accuracy or every preceding frame. No new
reference-emulator comparison was performed during this investigation.

At frame 8199, camera staging was `(460,196)` in WRAM words `$7E:0086/$7E:0088`.
Live task entries using the `$80:E55A` draw callback retained these values:

| Task byte offset | World origin | Camera-relative origin | Layout pointer | Coarse culling |
| --- | --- | --- | --- | --- |
| 4 | `(640,360)` | `(180,164)` | `$D242` | Accepted |
| 6 | `(384,168)` | `(-76,-28)` | `$145D` | Rejected |
| 8 | `(320,136)` | `(-140,-60)` | `$143F` | Rejected |

Task byte offsets are added to each parallel WRAM table base: `$7E:5D9C`
(animation index), `$7E:5E1C` (layout-pointer table address), `$7E:5E9C`
(queue bucket), and `$7E:5F1C/$7E:5F9C` (world X/Y). These fields and the
camera words are recorded in `local/sprite-before/frames/frame_008199_wram.bin`
and its identical counterpart in `local/sprite-repeat/frames`. For example,
`$7E:5DA2` and `$7E:5DA4` contain animation indices 17 and 16.
The bank word at `$7E:009E` is `$89`. Read the layout pointer at
`$89:(table_address + 2 * animation_index)`: offsets 4, 6 and 8 use tables
`$C865`, `$C833`, `$C833` and indices 2, 17, 16 respectively. This yields
`$D242`, `$145D`, `$143F` from the verified ROM's bank `$89` pointer tables.

The accepted layout was present in the draw queue; the two rejected layouts were
absent. Their layouts decoded into six pieces each from low WRAM mirrored into
bank `$89`, rather than from ROM. The accepted layout decoded from ROM. Its
`$89:D242` layout header selects WRAM word `$7E:697A` for the priority override;
the dump contains 3, giving priority bits `$3000`, computed at
`$80:8D90-$80:8DA9`. In this sample the override equals the static priority bits
already present in ROM attribute word `$3384`; it does not demonstrate the
override changing the output.
`local/sprite-proof.json` records the resulting coordinates, layout pointers and
piece attributes; it does not record the source animation or priority fields.

The proof file's `pieces` arrays are decoded data **before** the native
piece-level Y and allocation gates, not pieces successfully emitted to OAM.
Three of slot 6's six pieces have Y -40 or -48, and all six of slot 8's pieces
have Y between -80 and -64. Thus nine of the twelve pieces fail `[-32,224)`.
Even after removing origin culling, the native expander would emit zero pieces
for slot 8. Widening alone cannot recover these pieces or bring them into the
current vertical raster. This establishes recoverable layout data for these
examples, not complete graphics residency or all object types.

Evidence remains ignored under `local/sprite-before`, `local/sprite-repeat`, and
`local/sprite-proof.json`. ROM bytes, layout dumps, images, recordings and saves
must not enter commits or release assets.

### Replay command

The runs used `tools/headless.ps1` with the following settings, in a disposable
PowerShell session. Replace the three placeholders with locally supplied files
matching these SHA-256 identities; the files are not distributed with this repo:

| Input | Required SHA-256 |
| --- | --- |
| Executable | `3a062c076dc566dc3c374aced81d31dfd4099333033ff906325c2712206b3aea` |
| Recording | `50b521dbdf8ce53e62d9db37a5a73a079f683163838ba977cb51ce55560e5a1b` |
| Initial SRAM | `278e7bb6bc4c789319fe7b4d8202102c88971e28258be23610fb0c500b44c354` |

```powershell
$env:SNESRECOMP_APU_BEAM_ELAPSED = '1'
$env:SNESRECOMP_APU_HANDOFF = '1'
$env:SNESRECOMP_APU_ABSOLUTE_PREANCHOR = '1'
$env:SNESRECOMP_LLE_BOUNCE = '1'
$env:SNESRECOMP_FRAMEDUMP_PIXELS = '1'
$env:SNESRECOMP_FRAMEDUMP_START = '8190'
$env:SNESRECOMP_FRAMEDUMP_END = '8199'
Remove-Item Env:SNESRECOMP_FRAMEDUMP_VIDEO -ErrorAction SilentlyContinue
$spriteReplay = @{
    Rom = "Kirby's Dream Course (USA).sfc"
    Executable = '<local-executable>'
    InputRecording = '<local-course3-recording.json>'
    InitialSram = '<local-initial.srm>'
    Frames = 8200
    ExpectedUniqueFrames = 10
    Unpaced = $true
    Widescreen = $true
    WideExtra = 272
}
./tools/headless.ps1 @spriteReplay -Output local/sprite-before
$env:SNESRECOMP_FRAMEDUMP_VIDEO = '1'
./tools/headless.ps1 @spriteReplay -Output local/sprite-repeat
```

Use fresh output names if these directories already exist. The second run also
captures VRAM, CGRAM and PPU metadata; compare the shared WRAM/BMP files for
frames 8190-8199. Each output's `run.json` records input identities and selected
runtime options, but omits the `SNESRECOMP_FRAMEDUMP_*` settings. The two retained
manifests are identical: their file lists, not their manifests, show the extra
VRAM, CGRAM and PPU captures in the second run. The command above records the
different `SNESRECOMP_FRAMEDUMP_VIDEO` setting explicitly.

## Proposed implementation

1. Capture presentation draw requests before viewport rejection, retaining full
   coordinates, layout identity and ordering. Cover the actor paths, generic task
   helpers and special drawing callbacks. Do not invoke stateful callbacks a
   second time against live gameplay memory: both `$80:E55A` and `$80:E5A2`
   begin by calling the position updater `$80:E5E2`.
2. Expand layouts from the verified ROM and the corresponding live RAM snapshot.
   Preserve linked layouts, generated WRAM layouts, dynamic priority, tile
   attributes, flips, sizes, shadows and intentional hidden/deleted states.
   The separate presentation expander must not inherit the native final-piece Y
   gate `[-32,224)` or the OAM allocation limit checked against `$32`. Retain
   full signed coordinates and clip against the actual presentation view after
   its camera/zoom transform. Bound host storage explicitly without silently
   imposing the original OAM capacity on the expanded list.
3. Decode graphics and submit a separate presentation list through the pinned
   runtime's `PpuSetExtraObjects` API. `PpuExtraObject` has signed 32-bit X/Y,
   priority, color-math classification and ordering fields. This is a promising
   compositor seam, not proof its current ordering model can represent every
   whole-hole draw request. Audit OAM-slot ordering and graphics residency first.
   The pinned `runner/src/snes/ppu.h` contract specifies RGB15 pixels with bit 15
   marking opacity, including opaque black. The caller owns the object array
   and pixels until the next setter/reset; retain their storage accordingly.
4. Synchronize the list with the frame's actual OAM/VRAM/palette presentation
   boundary. Apply the same camera transform as terrain. Next-frame WRAM camera
   staging has already caused divergence in the terrain work. Rebuild or clear
   presentation state correctly on reset, load, rewind and scene transitions.
5. Initially add only margin rendering while preserving the original center and
   default behavior. Whole-hole viewing subsequently requires a shared terrain
   and sprite zoom transform, vertical coverage beyond 224 lines, and a policy
   for the existing 800-column raster capacity. Keep the HUD in screen space.

Game-specific request recovery belongs in the game integration. Generic extra
object composition belongs in the runtime. Preserve the interpreter fallback
and support both execution modes; do not edit generated C or patch the input ROM.

## Original acceptance gates

- Demonstrate objects in both horizontal margins, then above and below the native
  field, without requiring them to have previously entered the viewport.
- Establish all relevant emitters and special effect/spawn paths; a scan of the
  generic task table alone cannot claim every sprite in a level.
- Verify graphics/palette availability for offscreen animations and correct
  terrain occlusion, priority, shadows, transparency and color math.
- Compare deterministic native and fallback runs, including moving cameras and
  transitions. Report the first center-image or gameplay-state divergence.
- Check default mode against the pinned reference emulator and existing known
  divergences. The reference supplies no whole-hole rendering oracle.
- Cover reset/load/rewind, camera rotations, multiple courses and two-player
  scenes before claiming general support.

## Implemented horizontal-margin path

`src/course_sprites.c` now records presentation requests without changing guest
registers, RAM, clocks, ROM, or native OAM. The `entry_hook` directives in
`recomp/bank00.cfg` apply to both ROM mirrors; matching interpreter pre-opcode
observers cover fallback execution. Regenerate through setup after changing
the configuration; never add hooks by editing generated C.

| Observation | Purpose |
| --- | --- |
| `$8823`, `$8841`, `$8AE1` | Track OAM/queue reset separately from both queue-only clears; retain already expanded pieces when only queues are cleared. |
| `$E55E`, `$E5A6` | Observe task draws **after** the position updater; recover only requests rejected by origin bounds and retain the negative-animation hidden state. |
| `$E60B`, `$E635` | Recover rejected helper requests with world/screen coordinates. |
| `$9E9E`, `$9EEC` | Observe actor paths after their eligibility/animation checks, before viewport comparisons. |
| `$8B09` | Record accepted and other directly queued requests once. |
| `$8C12` | Resolve queued layouts after the optional `$8ED8` dynamic-layout updater, at the native consumer's boundary, then drain pending presentation requests. |
| `$8A3D` | Observe the separate direct expander, including its `$7F` animation selector. |
| `$82CA` | Latch the prepared list immediately before the game's fixed `$0600`, `$220`-byte OAM DMA. |

The expander handles five-byte pieces, signed offsets, size and flip bits,
linked layouts, the queue format's `$7E` dynamic priority, and direct-format
animation links. Absolute operands (camera, task/actor tables and layout-bank
selection) are read through the live data bank register; DP operands use bank
zero. The side-effect-free reader supports WRAM banks `$7E/$7F`, low WRAM
mirrors and the supported LoROM image. Unmapped/I/O reads invalidate the list
rather than guessing WRAM or touching MMIO. Low WRAM mirrors are read as RAM
even in bank `$89`. The expander retains
full signed world positions and does not apply the native Y or allocation
gates. Host limits are 1,024 requests, 4,096 pieces, and 1,024 decoding steps per
layout. Invalid pointers, cycles or exceeded limits reject the complete list;
stderr reports the first rejection and the optional diagnostic records every
invalid displayed list. This avoids presenting a silently truncated list.

`patches/snesrecomp-extra-object-provider.patch` adds a generic raster-time
provider to the existing pinned PPU, under its existing license. It contains
no game addresses. The callback runs immediately before OBJ evaluation, with
the current raster's scroll, VRAM and palette. It supplies one decoded RGB15 row
per intersecting piece through `PpuSetExtraObjects`, preserving opaque black,
palette-based OBJ color-math classification, background priority and windows.
Storage remains owned by the integration through composition. Setup applies
the patch and CMake rejects a runtime without it; it was also checked against
the clean framework pin.

Opaque host pixels replace horizontal-margin pixels, ordered by queue bucket
and insertion order. Native OAM continues to supply every original-center
pixel and retains its evaluation/allocation limits. Native margin pixels remain
where the host list is empty or transparent, including sprites from unobserved
emitters. This is the explicit exception to requiring every native pixel to be
reproduced by the recovered list; incomplete coverage no longer removes those
native pixels. The compositor changes margin ordering rather than discarding
native pixels, so native slot zero cannot defeat an opaque host replacement.
Direct draws precede queued bucket-zero pieces as in the
observed producer/consumer path. The enhancement activates only for the course
signature already used by terrain and does not support interlaced OBJ.

Reset, session changes, and successful state loads/rewinds clear the presentation
lists and unbind the provider, including builds without experimental execution
state. Lists rebuild from subsequent guest draws/OAM uploads. An immediate
restored frame retains native OAM margins immediately; additional recovered
pixels wait until the next upload. Presentation buffers are not added to guest
saves or snapshot identities. Extended margins do not apply the hardware's
32-sprite/34-tile per-line limits, although the original center retains them.

For a diagnostic terrain-only comparison, set `KDC_SPRITE_VIEW=0` while leaving
widescreen enabled. `tools/headless.ps1` records that switch in `run.json`.
With `KDC_SPRITE_LOG=<local-path>`, each draw logs the internal frame counter
(one greater than its zero-based host dump number), building piece count,
displayed piece count, invalid-list flag, left/right opaque candidate-pixel
counts, and left/right candidate counts from origin-rejected requests. Counts
precede background occlusion, windows, and object-overlap composition; they are
not final visible-pixel counts. Leave both variables unset for normal play.

## Implementation validation (2026-11-01)

The release build used native CMake, GCC 15.2.0, Ninja, SDL 3.4.10, the pinned
framework and the guarded options from `tools/prepare_guarded.ps1`. Build
command: `cmake --build local/baseline-build -j 8`. The synthetic fixture compiles
the real OBJ compositor and covers linked/generated layouts, changed priority,
hidden objects, observational callbacks, more than 128 pieces, malformed cycles,
OAM-upload latching, raster-time graphics/palette changes, opaque black, camera
movement, center preservation, margin ownership versus native slot zero, and
provider clearing. `python -m unittest discover -s tests -p 'test_*.py' -q` runs
the fixture with `local/guarded-framework` or `SNESRECOMP_ROOT`. The optional
pinned-reference reset/state replay tests also passed with `KDC_TEST_REFERENCE=1`.
With `KDC_TEST_REFERENCE=1`, that suite ran 21 tests: 20 passed, with only the
optional APU accuracy integration test skipped. The entire guarded patch sequence, including the new
provider in setup's actual order, also applied to a fresh pinned checkout.
With no opt-in environment variables, that revision ran 18 tests and skipped
three (the APU integration and two local reference tests). Main's launcher
integration and the review regressions add tests; current results are below.

The 8,200-frame route and initial SRAM retain the identities recorded above.
Native and fallback runs at 800 columns produced identical complete images and
WRAM for all 100 retained frames 8100-8199; their original-center images also
matched the disabled enhancement. No first center or WRAM divergence occurred
in that sampled interval. The render build used for these captures has SHA-256
`06af86091364f01f0138e96926b027d906749ea0d0dc7fa5a6aa1e4d95813050`.
After queue-only reset handling and diagnostics were finalized, executable
SHA-256 `99aba19d6a585b5d281ed6bbf4a68f027580a1cbb60fc23f74dfdb940c60298f`
repeated all 100 checkpoint comparisons at 800 columns (native), 342 columns
(fallback), and default width. The 800-column images also matched the earlier
render build byte for byte. All three final SRAM images matched each other
and the reference's final SRAM hash.

A 4,000-frame prefix of the same recording, retaining only events ending at or
before frame 4000, captured every frame 2830-3999. All 1,170 WRAM dumps and center
images matched across default, 800-column native, 800-column fallback and
terrain-only widescreen runs. Native/fallback complete images matched as well.
This includes the moving-camera interval 3900-3999 and the earlier terrain
investigation's frame-3923 failure site. Intentional visible changes against
terrain-only begin at host frame 2836 in the right margin and 2854 in the left.
The first origin-rejected candidate pixels occur at host frames 2837 (right)
and 2872 (left), without a previous center draw being needed. No invalid lists
were logged across either complete 8,200-frame native/fallback run.

A fresh pinned Snes9x 1.63 capture, with the same recording and SRAM, again
compared reference 8200 to host 8199: maximum center channel difference two,
mean absolute difference approximately 0.315, no pixel over the eight-level
tolerance, and 108 differing WRAM bytes, first `$0022`. The existing first
overall host/reference divergence at host 15/reference 16 remains unresolved.
Snes9x supplies no margin-rendering oracle.

The final executable also completed a 4,150-frame save/load/reset smoke run
twice. Its script appends `wait 600`, `savestate 0`, `wait 40`, `loadstate 0`,
`wait 40`, `reset`, and `wait 100` to the 4,000-frame prefix's generated script.
The first restored draw had zero displayed pieces; the next OAM upload rebuilt
them. Console reset returned both lists to zero. All 4,150 diagnostic rows and
all retained WRAM/BMP files matched between runs, with no invalid lists.
Frame-dump names use the internal counter, which rewinds on load and resets
on console reset: dump range 3990-4149 yields **53 unique files per output
type**, not 160. Use `-ExpectedUniqueFrames 53` for this scenario. This is
lifecycle smoke/repeatability evidence, not full save-state accuracy.

Whole-hole zoom, vertical coverage outside the native 224-line field, all
graphics residency/animation states, arbitrary special emitters or direct OAM
writes, ordering between unobserved native emitters and recovered pixels,
camera rotations, every course, two-player scenes, hardware per-line sprite/tile
limit parity in margins, and exact immediate recovered-pixel presentation after
save/load/rewind remain unqualified. Synthetic reset clearing is
covered; it does not establish those gameplay paths. Horizontal widening does
not make the two negative-Y objects in the investigation sample visible.
ROMs, recordings, saves, decoded layouts and capture artifacts remain ignored
under `local/` and must not enter commits or releases.

## Review regressions and retest (2026-11-01)

The follow-up merges main's shared launcher integration at `4c8eee8`, keeping
both CMake patch guards and the course-sprite source. The synthetic review
fixture reproduces three failures in the original implementation:

- A direct piece plus an unconsumed queue request, followed by `$8841` and
  `$8C12`, incorrectly retained the discarded request. Both queue-only clears
  now discard pending requests while retaining expanded pieces; consumption
  also drains pending presentation requests to prevent duplicate expansion.
- A task hidden in `$7E` but visible in `$7F` used the wrong absolute operands.
  The observer now follows DB, keeps DP in bank zero, and invalidates unsupported
  mappings without bus reads. The test also checks CPU/RAM invariance.
- An empty or partially transparent host list erased native right-margin
  pixels. The real OBJ compositor fixture checks every native buffer pixel for
  the empty-list case and every uncovered margin pixel for partial coverage,
  including a large native sprite not represented by any host request.
  Opaque host replacements, opaque black, center pixels and overflow flags are
  checked separately. This is a removal check at the composition boundary;
  final-image differences alone do not establish coverage of native emitters.

All three regressions failed before the fixes and pass afterward. The merged
release build succeeded using the commands above. Its executable SHA-256 is
`9bd364997b97eaceaf679af92b00c660ad46986d9e0c0163fadfe3deec45bbbe`.
The full guarded patch sequence, including the launcher and revised provider,
applied to a fresh pinned checkout. The regenerated bank contains the compiled
`$80:8841` observer; the interpreter registers the same site.

With `KDC_TEST_REFERENCE=1`, `KDC_TEST_LAUNCHER=1`,
`RECOMP_UI_ROOT=<local/baseline-build/_deps/kdc_recomp_ui-src>` and
`PYTHONDONTWRITEBYTECODE=1`, `python -m unittest discover -s tests -p 'test_*.py' -v`
ran 27 tests: 26 passed and the optional APU integration test was skipped.
Do not infer this count from a default run without those local dependencies
and opt-in variables.

The same 4,000-frame prefix was rerun in default, terrain-only widescreen,
800-column native and 800-column fallback modes. All 1,170 retained WRAM and
center images matched; native/fallback complete images matched, including the
pre-review wide captures. Three complete 8,200-frame runs (default, 800-column
native and fallback) also matched all 100 retained WRAM/center checkpoints,
native/fallback complete images and final SRAM. No first center/WRAM divergence
or invalid list occurred in these comparisons. The known reference differences
above are unchanged; they are not fixed by this work.

A further 4,150-frame save/load/reset smoke run passed with 53 unique captures.
The restored draw cleared the displayed recovered list and the next upload
rebuilt it; console reset cleared both lists. All 53 retained WRAM files matched
the earlier lifecycle capture. The fixture's native-margin fallback applies
even while the restored recovered list is empty. Immediate recovered-pixel
parity and margin hardware-limit parity remain unqualified as stated above.
