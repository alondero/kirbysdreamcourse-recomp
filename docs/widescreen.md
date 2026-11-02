# Widescreen course view

Widescreen is an optional presentation enhancement. Enable `[Graphics] Widescreen = 1` or the launcher's Widescreen setting, then resize or enter fullscreen. It defaults to off. The pinned host sizes an even-width raster using the drawable aspect and configured SNES pixel aspect. With the default pixel shape, 16:9 uses 342 columns, 21:9 uses 448, and 32:9 uses 682. The existing runtime capacity is 800 columns; ratios beyond that capacity are letterboxed rather than stretched. Portrait/narrow windows retain the original field.

`src/main.c` supplies adaptive geometry. `src/course_view.c` supplies presentation-only terrain through a direct tile-provider callback. `src/course_sprites.c` recovers presentation draw requests before viewport rejection and supplies horizontal-margin sprites through the PPU's extra-object compositor. Guest sprite emission/culling, collision and camera simulation retain their original behavior. The PPU owns priority, windows, palettes and color math. Course BG3 scenery fills the sides; the object-based HUD remains centered. Bounded menu foregrounds remain centered while BG2/backdrop can fill the sides. See [sprite implementation and validation](widescreen-sprites.md) for the additional hook configuration, runtime patch and remaining coverage limits.

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
islands and windmills; the HUD is drawn with objects. Course preparation now
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
