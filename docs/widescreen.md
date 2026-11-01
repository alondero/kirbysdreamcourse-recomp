# Widescreen course view

Widescreen is an optional presentation enhancement. Enable `[Graphics] Widescreen = 1` or the launcher's Widescreen setting, then resize or enter fullscreen. It defaults to off. The pinned host sizes an even-width raster using the drawable aspect and configured SNES pixel aspect. With the default pixel shape, 16:9 uses 342 columns, 21:9 uses 448, and 32:9 uses 682. The existing runtime capacity is 800 columns; ratios beyond that capacity are letterboxed rather than stretched. Portrait/narrow windows retain the original field.

`src/main.c` supplies adaptive geometry. `src/course_view.c` supplies presentation-only terrain through a direct tile-provider callback. The PPU still owns tile decoding, priority, windows, palettes, color math and sprites. The game, ROM, interpreter, native dispatch, collision and camera simulation are unchanged. BG3/HUD remains centered. Bounded menu foregrounds remain centered while BG2/backdrop can fill the sides. Original sprite emission/culling is retained: terrain outside the original viewport can be visible before the game emits an object there.

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

Margin tiles are resolved directly from source data at raster time using that line's horizontal scroll and scrolled Y. WRAM camera staging can already contain the next frame's scroll: using it caused the first center-pixel divergence at moving-camera frame 3923 in an earlier implementation. The direct provider avoids that mismatch and the shadow store's smaller coordinate capacity. Tiles overlapping either native edge always retain real VRAM, including the 33rd tile exposed by fine scrolling. No captured history supplies margins. Session reset clears the provider; each draw registers it again. Mosaic effects retain their native tile source, so their margins may temporarily repeat. The full margin is reapplied before drawing because the pinned desktop host narrows it to eight bits.

The game still emits ordinary signed 9-bit sprite coordinates. Zero right-margin hints preserve their original signed interpretation even above 256 added columns per side. Zero left-margin hints with motion grace disabled keep wholly hidden objects out of the sprite budget, while retaining sprites crossing the native edge. The renderer fixture checks both cases at maximum width; earlier unrestricted decoding displaced a left-edge sprite in the moving-camera test beginning at frame 3900.

## Validation (2026-11-01)

Release compilation succeeded with native CMake, MinGW GCC and Ninja, using the guarded framework configuration, existing generated source, and SDL 3.4.10. The isolated build was configured with the nine `KDC_EXPERIMENTAL_*_STATE` options used by `tools/prepare_guarded.ps1`, including autojoy and SPC D4, and without SPC C4. Verified build command: `cmake --build local/wide-native -j 8`. Standard fresh setup remains `./tools/setup.ps1 -CMake <native-cmake.exe>`.

`python -m unittest discover -s tests -p 'test_*.py' -q` passes 16 tests with the patched runtime. The integration fixtures default to `local/guarded-framework`, or use `SNESRECOMP_ROOT`; they skip if that checkout/API is unavailable. The packed-map fixture only needs GCC. Synthetic tests cover both layers, inclusive bounds, invalid offsets, the final valid WRAM word, fine-scroll overlap without history, clipped margin tiles, live per-line scroll, high map coordinates, negative rows, mosaic fallback and disabling/resetting the provider. A real 16x16 PPU drawer fixture checks non-course BG2 pixel phase at all seven nonzero fine-scroll offsets; the 8x8 provider does not activate the separate world-history path.

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
