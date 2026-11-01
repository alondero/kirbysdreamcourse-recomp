# Widescreen sprite investigation

Status: investigated, 2026-11-01. This records a proposed implementation path;
it does not implement offscreen sprites or establish whole-hole coverage.
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
Live task entries using the
`$80:E55A` draw callback retained these values:

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
the dump contains 3, giving priority bits `$3000` through `$80:8D90-$80:8DA9`.
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
PowerShell session. Resolve the three input paths to local files matching the
hashes above. The executable used during the investigation was
`../cloven-shrieking-well/local/guarded-build/KirbyDreamCourseRecomp.exe`;
the recording and SRAM came from the main checkout's
`local/course3-source-8200.json` and
`local/course2-completion-fallback/saves/save.srm` respectively.

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
frames 8190-8199. Each output's `run.json` records input identities and options.

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

## Acceptance gates still outstanding

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
