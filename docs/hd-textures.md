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
   recorded too, so an HD edge can grow over them. Images live in a
   content-addressed cache: the same character words and palette colours
   share one image even when the game streams them through different video
   memory slots. A full lookup (resolve nine neighbouring characters, hash
   their border key and the query, probe) runs once per background character
   cell per frame, not once per
   scanline: `snesrecomp-hd-tile-perf.patch` remembers each cell's result,
   keyed by its position, character, palette, flips and layer registers, until
   the frame, a counted VRAM write, CGRAM contents or the cache change.
2. **Scaling** (`src/course_checker.c`, `src/hd_scaler.c`). A cache miss asks
   the game for the HD image. For BG1/BG2, opaque two-colour characters are
   first tested against straight isometric edges (1:1 and 2:1, including
   reflected edges, corners and crossings). Corner interiors and complementary
   exteriors are recognized independently of palette order. The 236 candidate
   half-planes are compile-time constants. A model is accepted only if it
   reproduces all 64 original pixel centres and the 80 neighbouring samples
   in a two-pixel border, with at least eight interior pixels of each colour,
   and the fit is unique. The search only pairs candidates whose masks
   contain the target (an intersection can match nothing else) and finds an
   XOR partner by its mask, testing each pair of distinct masks once; because
   the fit must be unique, this pruning cannot change the result. Ambiguous corner fits stay on the
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
   When the result would be discarded, the game pauses tile work entirely
   (`PpuPauseHdTiles`): outside courses, and while the orbit view composes
   the presented frame itself. Paused lines get the nearest-scaled native
   line. The orbit decision uses the previous presentation (`KdcOrbitComposing`
   reports whether the last orbit draw composed and the view is still ready),
   so the first frame after leaving the view, or a frame the orbit view
   declines to draw, can show without HD detail. It never shows stale tiles.
5. **Presentation** (`patches/snesrecomp-hd-present.patch`). The desktop host
   binds a 4x surface for the next frame and presents it only after a whole
   frame has been drawn into it. A game-composed frame (the orbit view) is
   nearest-scaled a row at a time instead. Screenshots taken while HD is active are 4x
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
at most once per draw, reuses fits while VRAM words and neighbour samples are unchanged,
and resolves palette colours at draw time. Its 4096-entry cache probes at most
32 slots per lookup and never evicts an entry used in the current draw. When
those slots are occupied, new locations retain native pixels for that draw,
bounding solver calls even under adversarial collisions. HD off retains the old path.

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
  all four corner rotations in both palette polarities at native and fractional
  coordinates, ambiguous-corner rejection,
  and rejection of unsupported patterns. Its equivalence fixture keeps the
  original exhaustive pair search as an oracle: 4,000 models sampled from the
  candidate set (a quarter with perturbed borders) and 2,000 irregular
  two-colour patterns must give identical fits. With `KDC_TEST_TIMING=1` it
  also requires the pruned search to be at least 4x faster in the same
  process; that wall-time check is not run in CI.
- `tests/test_course_orbit_render.py` also checks that `KdcOrbitComposing`
  turns off after a declined orbit draw, so HD tile work resumes.
- `tests/test_course_orbit_render.py` also checks magnified checker rendering,
  repeatability, palette changes, streamed tile invalidation and guest-state
  preservation with HD on.

- The orbit cache regression alternates colliding locations for 5,000 samples,
  exhausts their probe window, and changes VRAM between draws. It checks that
  resident locations are not refitted per pixel and changed content is refitted.
- `tests/test_hd_tiles.py` builds the real PPU against synthetic scenes. A
  nearest-neighbour source must reproduce the nearest-scaled native frame
  exactly, with every line on the HD path and no fallback pixels. It checks
  both the unchanged-pixel shortcut and the full compositor (with unchanged
  summaries conservatively disabled). A one-pixel recolouring fixture checks
  that flipped tiles process only that pixel. Explicit
  scenes check that transparent HD subpixels reveal the layer below, that
  opaque ones grow over transparent pixels, flips, the single-layer shortcut,
  the line policy, layer-specific cache isolation, neighbour-border flips,
  negative and wrapped coordinates for all four tilemap
  sizes, unwrapped widescreen-provider coordinates, biased-span rejection,
  and that unsupported lines stay native. Work-count checks require one
  content lookup per visible cell per frame on both a 4bpp and a 2bpp layer,
  new lookups after a counted VRAM write, a palette change between lines or a
  new frame, and no tile work at all while paused.
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
used as the identity oracle. `KDC_HD_STATS=1` prints cumulative counters,
including `lookups` (full content lookups, after the per-cell and sprite
memos) and `misses` (images requested from the source).

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
use `--native-width` to disable widescreen, and `--scenario shot` or
`--scenario orbit` for the moving cases described below. Logs, captures and JSON measurements
stay in ignored `local/`. `--max-raster-ms` optionally enforces a local budget;
wall-time thresholds are sensitive to other workloads and are not CI tests.

Validation also compares 221 consecutive HD frames (380–600) and the complete
605-frame state trace against the pre-optimization executable, separately at
native width and widescreen. These checks establish preservation of the previous
renderer, not new fidelity to the pinned reference emulator. Linux, other holes,
extreme aspect ratios, and sustained high-refresh play are not qualified by this
bounded run.

### Lookup and orbit costs, 2026-11-08

Play with HD on fell well short of 60 fps. Timers inside the PPU during a
scripted shot (ball rolling, camera following) attributed most of the 9.5 ms
HD overhead to bookkeeping, not to generating HD images:

- About 27,000 full content lookups per frame, 6,700 of them decoding a
  neighbour border: roughly 6.5 ms. The neighbour context made each lookup
  depend on the cell position, so the per-line memo, keyed by the previous
  span, almost never hit, and every scanline of every cell repeated the work.
  BG3 also passed only the in-tile x offset as its position.
- Composition of the ~15,000 pixels per frame that really change: ~2.5 ms,
  plus ~0.3 ms of nearest row seeding.
- Image generation: under 0.1 ms per frame on average, but each checker fit
  scanned all ~27,700 candidate pairs (about 19 µs each in the fixture), and
  the orbit view fits every newly visible location: 38 ms on its first frame.
- With the orbit view (and therefore high refresh), the host presents the
  game's own composition, so the whole PPU tile pass was discarded, and that
  composition was nearest-scaled with a divide per output pixel.

The per-cell memo, the BG3 position, pausing discarded work, the pruned fit,
row-wise expansion and wider hash mixing address these. Full lookups fell to
about 4,700 per frame. `benchmark_hd.py --scenario` reproduces the moving
cases: `shot` plays a scripted shot (1100 frames, three A presses from
frame 600), and `orbit` plays the same shot while `KDC_CAMERA_SCRIPT` turns the
orbit view at frames 500, 700 and 900. Both profile frames 350–1100 and
compare a 4x screenshot of frame 800 in a separate untimed run, because
encoding it inside the window adds about 250 ms to one frame.

```powershell
python tools/benchmark_hd.py --executable local/guarded-build/KirbyDreamCourseRecomp.exe --baseline local/baseline-build/KirbyDreamCourseRecomp.exe --repeat 1 --display
python tools/benchmark_hd.py --executable local/guarded-build/KirbyDreamCourseRecomp.exe --baseline local/baseline-build/KirbyDreamCourseRecomp.exe --repeat 1 --display --scenario shot
python tools/benchmark_hd.py --executable local/guarded-build/KirbyDreamCourseRecomp.exe --baseline local/baseline-build/KirbyDreamCourseRecomp.exe --repeat 1 --display --scenario orbit
```

`local/baseline-build` held a Release build of the previous commit. Results on
a Ryzen 9 3900X, Direct3D display, widescreen, unpaced, HD on (means over the
window; max is the slowest frame; fps is the whole run):

| Scenario | Stage | Before | After |
| --- | --- | --- | --- |
| still (default) | HD raster | 9.145 ms | 5.101 ms |
| | Unpaced fps | 77.2 | 97.1 |
| shot | HD raster | 9.597 ms | 5.110 ms |
| | Unpaced fps | 69.5 | 94.4 |
| orbit | HD raster | 9.661 ms | 2.148 ms |
| | Orbit composition (`compose`, mean / max) | 10.245 / 48.6 ms (frame 501) | 6.572 / 16.6 ms |
| | Unpaced fps | 46.9 | 77.2 |

HD off measured about 1.0 ms of raster in the same runs (142 fps still and
shot, 101 fps orbit), unchanged between builds. With `--native-width` the
same scenarios measured shot 6.968 → 3.665 ms raster (81.9 → 106.2 fps) and
orbit 7.053 → 1.444 ms raster, composition 8.904 / 33.6 → 5.852 / 13.5 ms
(54.8 → 86.2 fps). Every run's screenshot was byte-identical to the previous
build's. The worst orbit frame was the view's
first, which fits every newly visible location. A first version of the
row-wise expansion read back from the presentation buffer; on a real display
that is a write-combined texture mapping, and orbit composition rose to 20 ms.
It now expands into ordinary memory. Wall-clock numbers remain indicative.

Remaining costs, in rough order: the orbit view rebuilds each visible
location's 80-sample border every draw (80 tile lookups per location);
composition of changed pixels is serial per line; and the 4x frame is still
seeded, copied and uploaded by the CPU. Moving composition to the GPU, from an
uploaded native frame, per-pixel tile records and an HD tile atlas, would
remove the last two but is a larger change.

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
- The HD path costs about 4 ms of CPU per frame on the development PC (about
  1 ms in the orbit view, plus its own checker sampling). It has not been
  measured on slower machines or Linux.
- The per-cell memo trusts a cell for the rest of the frame unless VRAM is
  written through counted paths, CGRAM changes, or the frame advances. A
  widescreen tile provider whose answer changes mid-frame without such a
  write could give one frame of stale neighbour context; the exactness check
  still keeps every native pixel centre.
