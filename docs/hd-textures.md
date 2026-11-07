# HD textures

The optional HD textures mode redraws course terrain, scenery and sprites at
four times the native resolution. It keeps the original palette and every
priority, window, colour-math and brightness rule. Regular checker edges are
reconstructed geometrically; other original 8x8 characters use a palette-locked pixel-art scaler, so
every HD pixel uses one of that character's own palette entries. Palette
fades and colour cycling therefore keep working. Guest state is unchanged;
this is presentation only.

## Enabling

Enable **HD textures** on the launcher's **Mods** page. It is off by default
and can be changed during play. The choice is saved in `kdc-hd.cfg` beside
the executable.

What changes and what stays pixel sharp:

| Scaled at 4x | Kept native (nearest-scaled) |
| --- | --- |
| Live course terrain (BG1/BG2) | Title, menus, course select and scorecards |
| Course scenery (BG3) | The HUD: its BG3 panel and screen-anchored sprites |
| Course sprites, including widescreen margins | Lines using mosaic, interlace, pseudo-hires or 16x16 characters |
| Analytic checker edges also work in the orbit camera | Other orbit-view artwork |
| | Any pixel the exactness check below cannot reproduce |

## How it works

The work is split between a generic framework patch and a small game policy.

1. **Recording** (`patches/snesrecomp-hd-tiles.patch`). While a tile source
   is enabled, the Mode 1 8x8 background drawers and sprite evaluation
   record, for every native pixel and layer, which character was offered
   there, with its palette, priority and flips. Transparent pixels are
   recorded too, so an HD edge can grow over them. Each character span is
   looked up once in a content-addressed cache: the same character words and
   palette colours share one image even when the game streams them through
   different video memory slots.
2. **Scaling** (`src/course_checker.c`, `src/hd_scaler.c`). A cache miss asks
   the game for the HD image. For BG1/BG2, opaque two-colour characters are
   first tested against straight isometric edges (1:1 and 2:1, including
   reflected edges, corners and crossings). A model is accepted only if it
   reproduces all 64 original pixel centres and the 80 neighbouring samples
   in a two-pixel border, with at least eight interior pixels of each colour,
   and the fit is unique. Ambiguous corner fits stay on the
   existing path rather than choosing a phase that could disagree with a
   neighbouring tile. Accepted edges are evaluated at each output sample, keeping
   the original palette indices and tile flips. This avoids rounded checker
   corners and repeated stair steps without inventing a course material or
   baking in colours. Uniform characters remain native. Other characters
   run a port of Hyllian's
   xBR-lv2-noblend (MIT) on its palette indices, comparing colours by the
   luma of their actual palette entries. Index 0 (transparent) gets a luma no
   colour can have, so transparency edges are treated as edges. Uniform
   characters gain nothing and stay native. Each image also stores per-pixel
   summaries (fully opaque, fully clear, unchanged), used to skip work later.
3. **Exactness check.** Only pixels with a recorded HD subpixel that differs
   from its native index are resolved again from the recorded characters.
   Unchanged records are still retained: a changing layer can reveal them.
   The priority values for the
   main and sub screens, its sprite value and its final colour must equal
   what the native renderer produced. Only then are its 16 HD subpixels
   composed, re-resolving priority per subpixel and reapplying colour math.
   Anything not modelled (a host overlay, a layer drawn by another path, a
   widescreen extra object) fails the check and keeps the native pixel.
   The output is first filled with a contiguous nearest-scaled row and copied
   vertically; composition overwrites only the pixels that need HD work.
4. **Game policy** (`src/hd_textures.c`). For each HD line the game can keep
   the whole line or individual columns native. Outside live courses the
   whole frame stays native. On course lines the HUD stays native: the BG3
   spans where the panel is composited over the clean scenery map (the same
   rule the orbit camera uses, `KdcHudSpan` in `src/course_view.h`), plus the
   screen-anchored sprite pieces recovered by `src/course_sprites.c`.
5. **Presentation** (`patches/snesrecomp-hd-present.patch`). The desktop host
   binds a 4x surface for the next frame and presents it only after a whole
   frame has been drawn into it. Screenshots taken while HD is active are 4x
   (1024x896, or wider in widescreen). Thumbnails, frame dumps and the overlay
   backdrop keep using the native frame.

### Procedural checker geometry

The checkerboard is already projected into the terrain atlas, including slopes.
A world-space flat checker overlay would discard that authored projection and
could cover hazards and decorations. Instead, the renderer recovers straight
boundaries from live character data. This is a conservative geometric fit, not
a semantic identification of every grass tile: other opaque two-colour terrain
edges with exactly the same geometry also qualify. Transparent, multicolour,
irregular and insufficiently supported patterns retain the existing renderer.

`snesrecomp-hd-tile-context.patch` adds the source layer and an unflipped
neighbour border to the HD query and cache key. The border uses the same native
tilemap and widescreen provider as the background. Biased anchor/margin spans, different palettes or
transparent neighbours cannot establish an opaque edge. Layer identity keeps
terrain rules from affecting identical sprite or scenery graphics.
The game requests this context only for eligible opaque, two-colour characters.
Eligibility is cached by character words; the PPU caches decoded borders by
all eight neighbours' live words and palette/flip attributes. Neither cache
assumes that a VRAM address or course tile identity remains unchanged.
The orbit renderer uses the same model at continuous texture coordinates, with
its existing footprint sampling when zoomed out. It checks each terrain location
once per draw, reuses fits while VRAM words and neighbour samples are unchanged,
and resolves palette colours at draw time. HD off retains the old path.

The mathematical edges have no texture-resolution limit, but the output still
has one: the standard HD surface is 4x, and the orbit compositor currently draws
at native output resolution before presentation scaling. This change removes
source-texel stair steps under orbit zoom; it does not make the whole renderer
draw at arbitrary window resolution. Subpixel reconstruction is an enhancement,
not evidence of subpixel geometry in the original SNES game.

## Validation

ROM-free tests:

- `tests/test_course_checker.py` checks native-centre identity, continuous
  straight edges, crossings, all 32 checker phases across adjacent tiles,
  ambiguous-corner rejection,
  and rejection of unsupported patterns.
- `tests/test_course_orbit_render.py` also checks magnified checker rendering,
  repeatability, palette changes, streamed tile invalidation and guest-state
  preservation with HD on.

- `tests/test_hd_tiles.py` builds the real PPU against synthetic scenes. A
  nearest-neighbour source must reproduce the nearest-scaled native frame
  exactly, with every line on the HD path and no fallback pixels. It checks
  both the unchanged-pixel shortcut and the full compositor (with unchanged
  summaries conservatively disabled). A one-pixel recolouring fixture checks
  that flipped tiles process only that pixel. Explicit
  scenes check that transparent HD subpixels reveal the layer below, that
  opaque ones grow over transparent pixels, flips, the single-layer shortcut,
  the line policy, layer-specific cache isolation, neighbour-border flips, biased-span rejection,
  and that unsupported lines stay native.
- `tests/test_hd_scaler.py` checks the scaler: flat images are unchanged,
  diagonals become monotone staircases, output is palette-locked, and
  rotating the input rotates the output.
- `tests/test_launcher_settings.py` covers the Mods page toggle and its
  persistence. `tests/test_course_view.py` covers the HUD span rule.

Opt-in real-ROM test, which needs the verified ROM and a local build:

```powershell
$env:KDC_TEST_HD = '1'; python -m unittest tests.test_hd_textures_runtime
```

It captures developer start `normal:1:1` at frame 380 and the title screen
at frame 300, with HD off and on. It requires that course art changes, that
the HUD portrait box stays native, and that the title screen stays native.
Captures are written under ignored `local/` and deleted afterwards.

### Procedural checker validation, 2026-11-07

The Windows Release build succeeded against the pinned framework and the full
56-patch setup sequence. Nine targeted tests passed with real-ROM HD checks
enabled: checker geometry, scaler, shared PPU, orbit rendering, course view,
and HD runtime preservation of menus/HUD. The standalone high-refresh probe
also passed at 60/120/144/240 Hz with zero guest changes.

For deterministic no-input starts at `normal:1:1` through `normal:8:1`, HD off
and on produced identical WRAM, VRAM, CGRAM, OAM, SRAM and native framebuffer
dumps at frame 381. HD screenshots at frame 380 showed the intended presentation
change; a repeated first-course HD capture was byte-identical. Source rendering
and game state therefore have no divergence at those checkpoints. This is not
a full-course playthrough or a new comparison against Snes9x: the enhancement's
subpixels have no SNES reference equivalent, and the previously documented
emulation divergences remain. Linux and every slope/hazard combination remain
untested. Captures stay under ignored `local/`.

A scripted zoom-in capture at frame 500 and a widescreen capture at frame 380
also kept WRAM, VRAM, CGRAM, OAM and SRAM identical with HD off/on. The zoomed
checker edges visibly lose their enlarged source-texel steps; sprite artwork
and the HUD retain their existing rendering.

The context-request and border-cache optimizations preserved the frame-340
screenshot byte-for-byte. A final SDL-dummy, native-width benchmark measured
about 12.0 ms of HD raster work versus 1.3 ms with HD off, with composition and
presentation measured separately. This enhancement adds CPU work; background
load varied substantially during testing, so these are local measurements,
not a sustained frame-rate guarantee. Use `tools/benchmark_hd.py` to measure
the target machine.

Results on `normal:1:1`, frame 380, recorded 2026-11-07:

- The nearest source produced a 1024x896 frame byte-identical to the 4x
  nearest-scaled native frame. With 64 extra widescreen columns per side it
  produced 1536x896, also byte-identical.
- No pixel failed the exactness check (`fallback=0`).
- The smooth source changed 2.65% of HD pixels: edges of terrain, scenery and
  sprites.
- Unpaced, on the development PC (400 frames, including the hole's load):
  native about 127 fps, HD about 98 fps. Later runs on the same machine
  varied widely under background load, so treat these as indicative. In a
  synthetic ROM-free benchmark of the PPU alone, at 4x: native 1.2 ms per
  frame, the HD path with no images 3.4 ms, and every pixel replaced 7.4 ms.

Developer switches for headless checks: `KDC_HD=0|1` overrides the saved
choice. `KDC_HD_SOURCE=nearest` selects the exact nearest-neighbour source
used as the identity oracle. `KDC_HD_STATS=1` prints cumulative counters.

### Performance diagnosis, 2026-11-07

On the Ryzen 9 3900X / RTX 3080 Windows machine, the bottleneck was CPU raster
composition. The HD tiles are composed by the software PPU; a faster graphics
card does not accelerate that work. Unchanged texels were still entering
candidate collection and exactness checking, and nearest expansion wrote four
interleaved rows per native pixel. The shortcuts above remove those costs
without reducing the texture scale or disabling validation for changed pixels.

Two paired Direct3D11 runs with screenshot I/O excluded gave:

| HD stage (mean ms/frame) | Before, runs 1 / 2 | After, runs 1 / 2 |
| --- | --- | --- |
| CPU raster | 6.490 / 5.993 | 4.924 / 5.764 |
| Frame composition/copy | 0.595 / 0.539 | 0.590 / 0.842 |
| Upload/present | 0.912 / 1.101 | 0.954 / 0.966 |

The raster improvement was 24% in the first pair and 4% in the second. Overall
unpaced throughput was 73.4 to 83.4 fps, then 76.5 to 73.3 fps: background load
can outweigh the saving. Subsequent sampling found an unrelated PowerShell
process consuming roughly 12 logical cores, plus build and antivirus activity.
These measurements identify removable raster work, not a sustained FPS promise.
The first, headless isolation run also reduced raster time from 6.041 to
5.038 ms with the unchanged-texel shortcut alone. Synthetic tests assert the
work reduction independently of wall-clock noise.

A final three-pair sample under that heavier load measured raster means of
8.223 / 10.314 / 7.979 ms before and 7.084 / 6.982 / 5.042 ms after. The
three-run median fell from 8.223 to 6.982 ms (15%). All benchmark screenshot
comparisons passed; background load still prevents a controlled FPS conclusion.

Reproduce an isolated comparison with two Release executables:

```powershell
python tools/benchmark_hd.py --executable local/guarded-build/KirbyDreamCourseRecomp.exe --baseline path/to/previous.exe --repeat 3 --display
```

This runs `normal:1:1` with deterministic no-input starts, widescreen, audio off,
VSync off, and no frame delay. It profiles frames 350–605, after course loading.
The screenshot comparison occurs at frame 340, before timing starts, so image
encoding does not inflate composition time. Omit `--display` for SDL dummy;
use `--native-width` to disable widescreen. Logs, captures and JSON measurements
stay in ignored `local/`. `--max-raster-ms` optionally enforces a local budget;
wall-time thresholds are sensitive to other workloads and are not CI tests.

Validation also compares 221 consecutive HD frames (380–600) and the complete
605-frame state trace against the pre-optimization executable, separately at
native width and widescreen. These checks establish preservation of the previous
renderer, not new fidelity to the pinned reference emulator. Linux, other holes,
extreme aspect ratios, and sustained high-refresh play are not qualified by this
bounded run.

## Known limits

- Each character is scaled on its own, so the scaler cannot see across
  character edges. Diagonals that cross a tile boundary can show a small
  step at the seam.
- Sprites only record their opaque pixels, so HD edges can soften a sprite's
  outline but not grow it outward. Where sprites overlap, only the top
  sprite is recorded. A transparent HD subpixel at the edge of the top sprite
  shows the background rather than the sprite beneath.
- HUD sprites are recognised through recovered screen-anchored draw requests.
  A HUD piece whose anchor is unresolved is treated as course art.
- The orbit camera evaluates checker geometry at its own output pixels, but
  the composed frame is still nearest-scaled for presentation.
- The HD path costs a few milliseconds per frame. It has not been measured
  on slower machines or Linux.
