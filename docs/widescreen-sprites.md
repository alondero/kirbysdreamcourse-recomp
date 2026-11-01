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
| `$80:E5A2` | Similar origin bounds for a path using screen coordinates directly. |
| `$80:E60B`, `$80:E635` | Helpers applying the same bounds to supplied coordinates, with and without camera subtraction respectively. |
| `$80:9E8E`, `$80:9EBD` | Separate actor drawing paths also impose viewport bounds; their negative thresholds differ. |
| `$80:8B09`, `$80:8C08` | Queue and consume ten ordered sprite-layout buckets beginning at WRAM `$2884`, spaced `$182` bytes apart. |
| `$80:8D89` | Expands layouts into OAM, limits allocation, rejects piece Y outside `[0,224)` or `[-32,0)`, and packs X into hardware coordinates. |

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

At frame 8199, camera staging was `(460,196)`. Live task entries using the
`$80:E55A` draw callback retained these values:

| Task byte offset | World origin | Camera-relative origin | Layout pointer | Coarse culling |
| --- | --- | --- | --- | --- |
| 4 | `(640,360)` | `(180,164)` | `$D242` | Accepted |
| 6 | `(384,168)` | `(-76,-28)` | `$145D` | Rejected |
| 8 | `(320,136)` | `(-140,-60)` | `$143F` | Rejected |

The accepted layout was present in the draw queue; the two rejected layouts were
absent. The rejected objects still had animation indices 17 and 16. Their layouts
decoded into six pieces each from low WRAM mirrored into bank `$89`, rather than
from ROM. The accepted layout decoded from ROM and used the layout's live-WRAM
priority override. This establishes recoverable layout data for these examples,
not complete graphics residency or all object types. Both rejected examples also
extend above the current raster; widening alone would not bring them into view.

Evidence remains ignored under `local/sprite-before`, `local/sprite-repeat`, and
`local/sprite-proof.json`. ROM bytes, layout dumps, images, recordings and saves
must not enter commits or release assets.

## Proposed implementation

1. Capture presentation draw requests before viewport rejection, retaining full
   coordinates, layout identity and ordering. Cover the actor paths, generic task
   helpers and special drawing callbacks. Do not invoke stateful callbacks a
   second time against live gameplay memory: `$80:E55A` itself updates position.
2. Expand layouts from the verified ROM and the corresponding live RAM snapshot.
   Preserve linked layouts, generated WRAM layouts, dynamic priority, tile
   attributes, flips, sizes, shadows and intentional hidden/deleted states.
3. Decode graphics and submit a separate presentation list through the pinned
   runtime's `PpuSetExtraObjects` API. `PpuExtraObject` has signed 32-bit X/Y,
   priority, color-math classification and ordering fields. This is a promising
   compositor seam, not proof its current ordering model can represent every
   whole-hole draw request. Audit OAM-slot ordering and graphics residency first.
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
