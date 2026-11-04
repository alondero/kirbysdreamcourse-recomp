# Widescreen course view

Widescreen is an optional presentation enhancement. Enable `[Graphics] Widescreen = 1` or the launcher's Mods > Widescreen setting. It defaults to off. The View aspect option follows the drawable by default, or selects a fixed 16:9, 21:9 or 32:9 view; fixed views are letterboxed as needed. The pinned host sizes an even-width raster using the selected view and configured SNES pixel aspect. With the default pixel shape, 16:9 uses 342 columns, 21:9 uses 448, and 32:9 uses 682. The existing runtime capacity is 800 columns; ratios beyond that capacity are letterboxed rather than stretched. Portrait/narrow windows retain the original field in automatic mode.

`src/main.c` supplies adaptive geometry. `src/course_view.c` supplies presentation-only terrain through a direct tile-provider callback. `src/course_sprites.c` recovers presentation draw requests before viewport rejection and supplies horizontal-margin sprites through the PPU's extra-object compositor. Guest sprite emission/culling, collision and camera simulation retain their original behavior. The PPU owns priority, windows, palettes and color math. Course BG3 scenery fills the sides from its clean source map; the tile-based HUD and its object elements remain centered. Bounded menu foregrounds remain centered while BG2/backdrop can fill the sides. See [sprite implementation and validation](widescreen-sprites.md) for the additional hook configuration, runtime patch and remaining coverage limits.

## Source evidence

The local USA ROM was verified against SHA-256 `0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b` before investigation. The supported identity is maintained in `rom_identity.txt`.

The inspected runtime is the existing pin `147b55e252dcb3e7680a99c7c2f50e7183891844`, under the repository's existing PolyForm Noncommercial terms. `patches/snesrecomp-widescreen-tile-provider.patch` adds a generic raster-time source callback for 8x8 background margins. It contains no game addresses or content. Setup applies it automatically; CMake rejects a runtime lacking the API. The patch was checked against the clean pin. No dependency version or generated code changes are required.

Simply extending VRAM repeats stale terrain: gameplay uses circular 64x32 maps at word addresses `$5000` and `$5800`. The ROM's row streamer at `$80:BC3E` and column streamer at `$80:BE17` resolve their source from WRAM:

| WRAM address | Meaning |
| --- | --- |
| `$7E:0086`, `$7E:0088` | Signed horizontal/vertical map scroll used by the streamers |
| `$7E:8BB0 + 2*y` | Packed row offset, in words |
| `$7E:93B0 + 2*y` | Inclusive left tile column |
| `$7E:9BB0 + 2*y` | Inclusive right tile column; equal bounds denote an empty row |
| `$7F:3000`, `$7F:9800` | Parallel packed BG1/BG2 tile entries |

For an in-bounds tile, the word index is `row_offset + x - left`. Each source allocation is bounded to `$6800` bytes; invalid rows, columns and offsets yield the blank entry. The course path is selected only for Mode 1, BG maps `$51/$59`, and the course's shared character base. The `$50/$58` course preview is a bounded screen, not a streaming course.

At host frame 8199 of the recorded course-three route below, all 1,914 native entries (33 columns by 29 rows, two layers) matched this source lookup exactly. Local ROM disassembly, WRAM, VRAM, images and recordings remain ignored inputs, not repository content.

Margin tiles are resolved directly from source data at raster time using that line's horizontal scroll and scrolled Y. PPU scroll registers retain only ten bits. The signed WRAM camera selects the nearest 1,024-pixel world period, while all ten scroll bits come from the live raster. This recovers negative camera positions without using the pending camera's fine scroll. WRAM camera staging can already contain the next frame's scroll: using it directly caused the first center-pixel divergence at moving-camera frame 3923 in an earlier implementation. The direct provider avoids that mismatch and the shadow store's smaller coordinate capacity. Tiles overlapping either native edge always retain real VRAM, including the 33rd tile exposed by fine scrolling. No captured history supplies margins. Session reset clears the provider; each draw registers it again. Mosaic effects retain their native tile source, so their margins may temporarily repeat. The full margin is reapplied before drawing because the pinned desktop host narrows it to eight bits.

The game still emits ordinary signed 9-bit sprite coordinates. Zero right-margin hints preserve their original signed interpretation even above 256 added columns per side. Zero left-margin hints with motion grace disabled keep wholly hidden objects out of the sprite budget, while retaining sprites crossing the native edge. The renderer fixture checks both cases at maximum width; earlier unrestricted decoding displaced a left-edge sprite in the moving-camera test beginning at frame 3900.

## Validation (2026-11-01)

Release compilation succeeded with native CMake, MinGW GCC and Ninja, using the guarded framework configuration, existing generated source, and SDL 3.4.10. The isolated build was configured with the nine `KDC_EXPERIMENTAL_*_STATE` options used by `tools/prepare_guarded.ps1`, including autojoy and SPC D4, and without SPC C4. Verified build command: `cmake --build local/wide-native -j 8`. Standard fresh setup remains `./tools/setup.ps1 -CMake <native-cmake.exe>`.

`python -m unittest discover -s tests -p 'test_*.py' -q` passes 16 tests with the patched runtime. The integration fixtures default to `local/guarded-framework`, or use `SNESRECOMP_ROOT`; they skip if that checkout/API is unavailable. The packed-map fixture only needs GCC. Synthetic tests cover both layers, inclusive bounds, invalid offsets, the final valid WRAM word, fine-scroll overlap without history, clipped margin tiles, live per-line scroll, high map coordinates, negative rows, mosaic fallback and disabling/resetting the provider. A real 16x16 PPU drawer fixture checks non-course BG2 pixel phase at all seven nonzero fine-scroll offsets; the 8x8 provider does not activate the separate world-history path.

For a custom experiment name, set `SNESRECOMP_ROOT` to that experiment's patched framework checkout before running the tests, for example `$env:SNESRECOMP_ROOT = (Resolve-Path local/wide-framework).Path`.

The local 8,200-frame course-three recording has SHA-256 `50b521dbdf8ce53e62d9db37a5a73a079f683163838ba977cb51ce55560e5a1b`. Initial SRAM must have SHA-256 `278e7bb6bc4c789319fe7b4d8202102c88971e28258be23610fb0c500b44c354`; blank SRAM follows a different menu route. With the three APU options from `tools/play_experimental.ps1` and native bounce enabled, baseline, 342-column and 800-column runs reached the same checkpoint. All 100 retained WRAM frames 8100-8199 and all corresponding original-center images matched byte for byte. No divergence was found in those comparisons. The earlier baseline's final ten WRAM/BMP frames also matched the disabled enhancement. A fallback replay matched the final ten native WRAM and complete 342-column images. These are sampled comparisons, not proof of every earlier frame.

A fresh Snes9x 1.63 capture used the pin and DLL hash in `docs/compatibility.md`, the same recording and SRAM, and reference frame 8200 against host 8199. No center pixel differed by more than eight channel levels; maximum channel difference was two, mean absolute difference approximately 0.315. Full WRAM differed at 108 bytes, first address `$0022` at this checkpoint. This enhancement does not fix the previously documented first overall mismatch at host 15/reference 16. Snes9x supplies no widescreen oracle; margin evidence comes from the streamer's source lookup and visual checks for repetition/seams.

The final raster-time implementation also passed the moving-camera comparison at frames 3900-3999: 100 WRAM frames and original-center images matched baseline, covering horizontal fine-scroll phases 0, 2, 3, 4, 5, 6 and 7. The final 800-column native and 342-column fallback runs pass this comparison after the sprite policy correction. The old frame-3923 divergence is absent. Headless drawable-size checks at 640x360, 840x360 and 1280x360, with no fixed-width override, produced 342-, 448- and 682-column fields respectively.

To repeat a fixed-width host capture, use the usual timing switches, `SNESRECOMP_FRAMEDUMP_PIXELS=1`, an explicit dump range, and:

```powershell
./tools/headless.ps1 -Rom "Kirby's Dream Course (USA).sfc" `
  -Executable local/guarded-build/KirbyDreamCourseRecomp.exe `
  -Output local/wide-check -InputRecording <local-recording.json> `
  -InitialSram <matching-initial.srm> -Frames 8200 -ExpectedUniqueFrames 100 `
  -Unpaced -Widescreen -WideExtra 43
```

Use dump range 8100-8199, `-WideExtra 272` for the capacity test, and omit `-Widescreen` for baseline. The tool records the fixed margin in its manifest and restores the caller's prior `SNESRECOMP_WS_EXTRA`. For interactive adaptive sizing, leave that diagnostic variable unset.

Coverage does not establish every course, camera rotation, transition, two-player layout, save/load history, physical monitor/DPI configuration, audio, or sprites outside the original viewport. The rendering signature and source layout are USA-ROM-specific assumptions, separate from the generic PPU implementation.

## Hole 1-2 scenery correction (2026-11-02)

The margin provider previously treated the PPU's ten-bit scroll as a signed
16-bit world coordinate. On the local 17,000-frame hole 1-2 route (recording
SHA-256 `537d4c7c310d82ca3353d35fe78442698107b56f51d004d94242e962ac6d18fa`,
zero initial SRAM), the first negative vertical scroll in the examined
14450-14650 interval occurs at host frame 14546. The PPU stores -1 as 1023,
while the pending WRAM camera is already -2. Looking up rows using 1023
misidentifies 402 of 630 sampled native BG1 entries. Recovering the signed
period while preserving the live raster bits matches all 630. This explains
why the terrain margins fail temporarily and recover when the camera returns
to nonnegative Y. Horizontal scroll needs the same period recovery; blindly
sign-extending ten bits would incorrectly negate valid coordinates above 511.

A separate persistent cutoff came from the runtime's default centered BG3
policy. In the captured streamed course, the BG3 tilemap contains decorative
islands and windmills. The original investigation incorrectly classified the
HUD as object-only; its panel is also composited into BG3 (see below). Course preparation now
widens BG3 from visible scanline 1 and explicitly disables that policy when
leaving the course or disabling widescreen. BG3 uses its native circular
tilemap; the packed terrain provider remains restricted to BG1/BG2.

ROM-free regressions exercise negative X/Y scroll, a pending camera across
zero, positive coordinates above 511 and beyond one PPU period, plus the real
2bpp scenery drawer at 342 and 800 columns with unchanged center pixels.

Release compilation passed with native CMake, MinGW GCC and Ninja using
`cmake --build local/diagnostic-build -j 4`, the existing guarded framework
at the documented pin, and the same nine state options listed above. The
27-test Python suite passed with six optional checks skipped and
`SNESRECOMP_ROOT` pointing to the patched guarded framework. Both new
regressions fail against the pre-fix source and pass with the correction.

Two fresh native 342-column runs and an interpreter-fallback 800-column run
completed the same 17,000-frame route. All 701 retained frames 14400-15100
have identical WRAM and original-center pixels to the pre-fix capture.
The two native complete images match each other, and the shared 342-column
region matches the 800-column fallback images for all 701 frames. No first
divergence occurs in these state/center comparisons. Visual inspection at
14546 confirms continuous terrain margins across the wrapped scroll and
background scenery outside both original edges.

A fresh pinned Snes9x 1.63 run with the same ROM, recording and zero SRAM
completed 17,000 frames. At host 14546/reference 14547, 408 center pixels
exceed eight channel levels (maximum 34, mean absolute channel difference
0.473); at host 15100/reference 15101, 573 do (maximum 58, mean 0.418).
These center images are byte-identical before and after the change; the
reference differences are not fixed here. The earlier first overall
host/reference mismatch remains the one documented at host 15/reference 16.
Snes9x does not supply a widescreen oracle.

Period recovery assumes the staged camera is within 512 pixels of the
raster camera. Abrupt larger jumps, other courses' BG3 usage, all menu and
save/rewind transitions, and unsampled frames remain unqualified by this
replay. The existing moving-camera center comparison above remains relevant
because the correction retains the live raster's complete ten-bit phase.


## Clean scenery extension (2026-11-04)

The USA ROM retains a complete 32x32 scenery tilemap at WRAM `$7E:DADC`
through `$7E:E2DB`. The display tilemap at VRAM word `$3C00` combines this
scenery with the HUD panel. Widening that display map repeated the panel in
both margins. The source buffer contains the actual scenery underneath it:
no extracted asset pack is needed. The initial implementation repeated this
clean map, preserving its tiles and palette but exposing mismatched artwork
edges. The reflected extension below supersedes that repetition.

`CourseTile` now supplies BG3 from this clean buffer when the live course
signature, map `$3C` and character base `$4000` match. BG1/BG2 retain their
world-data provider; non-course screens keep their existing presentation.
The source is read from current WRAM, so there is no captured-frame history
or persistent scenery cache to invalidate on load/reset. The 8x8 chunks that
overlap the original 256 columns still use real VRAM. Both main and sub screen
rendering retain the PPU's palette, window and color-math composition.

`patches/snesrecomp-scenery-provider.patch` extends the existing generic
margin callback to the third layer and the normal 2bpp drawer. It does not
expand the terrain-history store or add game addresses to the runtime.
Apply it after `snesrecomp-widescreen-tile-provider.patch`; the setup sequence
includes it and CMake rejects older provider APIs. Both patches apply to the
clean existing framework pin, without changing its license or revision.

Source evidence: after verifying the architecture's ROM SHA-256, deterministic
developer starts `normal:1:1` through `normal:8:1` were captured at frame 599.
All eight scenery maps occupy the same WRAM address. In seven captures, the
VRAM and WRAM maps differ only in rows 22-26, which contain the HUD panel;
course two had not yet displayed that panel at this checkpoint. Decoding the
source maps confirms complete scenery rather than blank HUD-sized holes.
All dumps and decoded artwork remain under ignored `local/`.

ROM-free tests exercise the real 2bpp drawer at margins 1, 43, 96, 213 and
272 with every fine-scroll phase, including chunks crossing the original
viewport boundaries. The provider fixture covers wrapped rows, negative X,
source tile attributes, unsupported signatures and the separate terrain path.

The initial version reused a 256-pixel motif rather than inventing unique
panoramic landmarks; its edge mismatch prompted the correction below. Automatic sizing accepts arbitrary
window ratios within the existing 800-column raster capacity; larger ratios
are letterboxed, and narrow windows retain the native field. Mosaic effects
still use the native tile source, including its HUD, during the effect.
Menu/title artwork and unsupported rendering signatures are not reauthored.


Validation: the Release build passes with native CMake 4.3.1, MinGW GCC 15.2,
Ninja, the existing guarded generated source and nine guarded state options.
Use the native MinGW resource compiler as well as its C/C++ compilers; an
MSYS `windres` earlier failed preprocessing the Windows manifest. The final
build command was `cmake --build local/bg-build -j 8`. The Python suite passes
46 tests with seven optional checks skipped and `SNESRECOMP_ROOT` pointing
to `local/bg-framework`. The new renderer fixture fails against the old
provider and passes with this patch. `git diff --check` and patch application
against the clean pin plus the preceding provider patch pass.

Fresh deterministic developer starts used `SNESRECOMP_APU_BEAM_ELAPSED=1`,
`SNESRECOMP_APU_HANDOFF=1`, `SNESRECOMP_APU_ABSOLUTE_PREANCHOR=1` and
`SNESRECOMP_LLE_BOUNCE=1`, zero SRAM and no controller input. Each ran 600
frames, retaining frames 590-599. For each of eight normal courses' first
holes, native 256-column, 342-column, 800-column and repeated 800-column runs
have byte-identical WRAM and original-center images. The complete repeated
800-column images match, and their shared 342-column region matches the
342-column run. No first divergence occurs in these 80 sampled checkpoints.
The initial cross-build exploratory runs used mismatched timing defaults and
are not used as state-equivalence evidence.

Native-versus-800-column checks also match at all ten retained checkpoints
for `extra:1:1`, `extra:8:1`, `multi:1:1`, `multi:4:1`, and ordinary boot to
the title screen. Interpreter fallback (`SNESRECOMP_LLE_BOUNCE=0`) matches
normal course 1's complete 800-column image and WRAM at frame 599. Visual
inspection confirms that course margins contain scenery rather than HUD
panels, including the two-player layout. The title retains its existing
repeating framed/stars background; it has not received new artwork.

No fresh Snes9x comparison was run for this presentation-only change; the
unchanged centers and WRAM do not resolve the previously documented first
reference divergence at host 15/reference 16. These samples do not establish
all holes, course changes, menu/ending scenes, mosaic effects, live resizing,
or save/load transitions. The clean map's address and scene signature remain
USA-game-specific assumptions, not generic SNES hardware behavior.


## Continuous reflected scenery (2026-11-04)

The clean map is a complete image, not a seamless texture. The first version's
modulo-32 lookup joined unrelated left/right edges, cutting through clouds and
cliffs. A user screenshot exposed the mistake that center-preservation tests
could not catch. Decoded source-map previews reproduce the same discontinuity
without the PPU; this identifies the artwork boundary rather than scroll math
or HUD reconstruction as the cause.

The margin lookup now reflects the visible clean tile strip back and forth.
At a reflection boundary, the adjoining pixels come from the same edge of the
same source tile, in opposite directions. Existing horizontal flips are XORed
with the reflection flip; palette, priority and vertical-flip bits survive.
There is no filtering, color blending, stretched edge column or generated art.
The map is still read live from WRAM and the usual PPU renders the tiles.

The protected strip comprises 32 tiles at aligned horizontal scroll, or all
33 partially visible tiles under fine scroll. Reflection starts beyond those
complete tiles. This keeps the authentic native-edge chunks untouched while
joining their adjacent clean scenery with matching pixels. The strip follows
the live scroll and its period is independent of viewport width, so resizing
does not choose a different panorama. With aligned scroll the period is 512
pixels; fine-scroll phases use 528 pixels. This is a presentation choice for
this game's scenery, not an emulated SNES wrap rule.

Reflection removes the hard cuts but can create symmetric or paired landmarks.
It is not a uniquely painted panorama. Large course motifs in particular can
make that symmetry apparent. A future art-directed extension would need newly
authored scenery and a suitable local asset pipeline; this change deliberately
uses only the player's original tiles. The existing menu, mosaic, raster-width
and unqualified-transition limits still apply.

A new real-PPU regression uses asymmetric, non-tileable synthetic artwork with
palette and both tile-flip attributes. It compares every output pixel across
800 columns to the reflected original strip, sampling the complete ten-bit
scroll domain in steps of 17 (all fine-scroll phases). The test fails with the
old wrapping lookup and passes with reflection. Separate assertions preserve
the native HUD map and check explicit left/right reflection boundary entries.
This seam-continuity check complements, rather than replaces, center/state
comparisons and visual inspection.


Follow-up validation: Release compilation and the 46-test suite pass (seven
optional skips). Fresh native, 342-column, 800-column and repeated 800-column
runs of all eight normal-course first-hole starts match WRAM and center
pixels at frames 590-599; repeated full images and shared-width regions also
match. All 80 native captures are byte-identical to the previous build. No
first state/center divergence was found in these samples. Visual checks of
the cloud/cliff, geometric and cake themes show continuous reflected joins.
A fresh two-player first-hole sample preserves center/state across the ten
checkpoints, and interpreter fallback matches the full normal-course-1 image
and WRAM at frame 599. Reference-emulator coverage remains unchanged.

All captured course BG3 horizontal scroll values are zero. This policy is
qualified for that stationary scenery, not smooth horizontal BG3 animation:
changing between aligned/fine-scroll phases changes the protected strip's
width, which can move reflected motifs discontinuously (for example source
X=111 becomes X=127 at screen X=400 when scroll changes 0 to 1). Static
fine-scroll correctness is tested, but that does not prove temporal continuity.
If a scene animates BG3 horizontal scroll, it needs separate qualification or
a pixel-level extension policy before claiming smooth scrolling support.


## Course-select island and sky (2026-11-04)

The course-select screen uses three planes: a large island on BG1, a cloud
plane on BG2, and sky plus a raster-switched course label on BG3. The previous
non-course policy clipped BG1 and BG3 to the native field. The resulting
margins showed the green backdrop and occasional wrapped cloud fragments.
Removing the clamp alone is insufficient: the VRAM maps contain only the
currently streamed portion of the island/cloud planes.

The verified USA source maps are retained in WRAM:

| Plane | Source | Layout |
| --- | --- | --- |
| BG1 island | `$7F:0000` | Nine 32x32-tile pages, three pages across |
| BG2 clouds | `$7F:5000` | Same page layout, separate allocation |
| BG3 clean sky | Render VRAM word `$6000` | First 32x32 tilemap page |

Each complete world plane is 96x96 tiles (768x768 pixels). For `(x,y)` in tile
coordinates, the page is `(y/32)*3+x/32`; its entry is `(y%32)*32+x%32`.
Coordinates outside the nine-page plane return the transparent tile instead
of wrapping into another island. At the inspected selection checkpoint all
1,518 sampled native BG1/BG2 entries matched these source maps (759 per layer).
Decoded local previews show one intact island and its complete cloud plane.
These previews and all extracted data stay under ignored `local/`.

The presentation opts in only with Mode 1, 8x8 BG tiles, BG maps `$51/$59/$61`
and character-base register `$0230` or `$0240` (the cloud animation swaps
character banks). The callback checks the live raster
signature. BG1/BG2 use the existing live-scroll/WRAM-period reconstruction and
read the complete maps; native-edge chunks retain real VRAM. BG3 reflects the
clean sky page into the margins, using the existing scenery policy. The native
field keeps the raster's switch to the second page for the bottom course label,
so the label stays centered. Palette, priority, transparency, windows and color
math remain with the PPU. No camera, selection state or generated code changes.

The layer clamp is now assigned explicitly for every prepared frame: clear
for course/select world scenes and disabled widescreen, set for bounded menus.
This also prevents a menu clamp from persisting when changing scene. The
source layout and selection signature are game-specific assumptions, kept out
of the generic runtime. No additional framework patch or dependency is needed.

ROM-free regressions check every source page's corners in both planes, negative
and out-of-bounds coordinates, independent layer data, selection dispatch,
clean sky versus the course-label map, and policy reset when leaving selection,
disabling widescreen or returning to gameplay. The selection regression fails
against the preceding implementation and passes after this change.


Release compilation passes with `cmake --build local/bg-build -j 8` using the
same native CMake/MinGW/Ninja setup as above. The 46-test Python suite passes
with seven optional skips and `SNESRECOMP_ROOT` set to `local/bg-framework`.
The navigation recording starts from the completed-course SRAM used below,
presses Start at frames 301/451/651 and A at 851/1051 (ten frames each), then
Down at 1801 and Up at 1901 (ten frames each). It runs 2,000 frames and retains
1780-1999. The timing options are the four documented guarded defaults.

- Initial SRAM SHA-256: `60a2567bfa895abd404fef9c1c25eac5a9e9c9f9a2715b2b56245d4595b52511`.
- Recording SHA-256: `3606de88b1c2740c2db753ca8bd28b5fd92f24bc25daadfe16f2d1c5d4c06744`.

All 220 checkpoints match WRAM and native-center pixels between the previous
build, disabled widescreen, 342-column, 800-column, repeated 800-column and
interpreter-fallback runs. Complete 800-column repeat/fallback images match,
as do the 342-column regions shared with the wider output. There is no first
state/center divergence in this interval. The replay covers 35 distinct camera
positions, selects courses 1 and 2, and exercises both cloud character banks.
Visual inspection shows the formerly clipped island edges and complete clouds
against sky, with one centered course label and no green side borders.

The original fixed `$0230` prototype re-clipped when the cloud animation
switched to `$0240`; the longer replay caught this and the final signature and
regression cover both. No fresh Snes9x comparison was run: the unchanged center
and state do not resolve the existing baseline reference divergences. Extra
and two-player selection layouts, other menu/ending screens, mosaic effects,
all course unlock states and unsampled scene transitions remain unqualified.
