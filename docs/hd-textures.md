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
| Orbit/high-refresh terrain, scenery and world sprites | Screen-anchored orbit HUD sprites |
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
The orbit renderer no longer uses this per-character model; it enlarges the
whole terrain image at once (see
[Orbit terrain texture](#orbit-terrain-texture-2026-11-11)). Subpixel
reconstruction is an enhancement, not evidence of subpixel geometry in the
original SNES game.

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
  preservation with HD on. Its art-cache fixture samples two sprite characters
  5,000 times in one draw, changes VRAM and palette between draws and fills the
  cache past capacity; it checks one enlargement per source per draw, refresh
  on the next draw after a change, and no regeneration under pressure.
- `tests/test_hd_image.py` checks the whole-image enlargement: an isometric
  square becomes a rhombus whose edges move exactly two texels per row, a
  boundary line drawn across a square does not round its corners, the line
  keeps its own smoothing, and 8x8 partial updates equal one whole update.
- `tests/test_course_terrain.py` checks the incremental terrain texture: an
  unchanged update regenerates nothing, a palette cycling every frame shows
  its live colours without regenerating the enlargement, and once it settles
  the enlargement catches up exactly once.
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

### Orbit terrain texture, 2026-11-11

With HD textures and the orbit view (and therefore high refresh), the course
composition took about 41 ms per present (worst frames over 150 ms), against
about 7 ms with HD off, so the view could not reach even 60 Hz. Timing inside
the composition showed why: every output pixel re-derived its terrain colour
by inverse-projecting the triangle it lay in, then looking up and sampling a
per-character 4x image through a hash cache (13 to 34 ms per frame); the
scenery behind the course was enlarged per pixel the same way (about 10 ms);
and all of it ran on one thread. The same per-character enlargement caused
the uneven checker edges: a tile holding a square's tip failed the straight
edge fit, fell back to the pixel-art scaler, and showed a flattened tip or a
kink beside correctly fitted neighbours, and rotating the view then point
sampled the 4x texels, adding ragged teeth.

The terrain is now a texture, built once and then kept up to date:

- `src/course_terrain.c` composes both terrain planes, with their priority, into
  one image of the whole streamed map, and the clean scenery map into another.
  Each update compares the live tile maps, characters and palette with the last
  ones and redraws only the changed 8x8 cells. An unchanged update costs about
  0.02 ms.
- `src/hd_image.c` enlarges a whole image at 4x, so every pixel sees its real
  neighbours across character boundaries. A flat region no larger than 40
  pixels that is exactly the set of pixel centres inside straight edges (1:2,
  1:1, 2:1, horizontal or vertical) becomes that polygon. Edges a region does
  not need are dropped, so the two sides of a checker square meet in a sharp
  point instead of pixel art's blunt tip. Everything else uses the
  palette-locked xBR scaler, now run over the whole image
  (`KdcXbrScaleRegion`). A change re-enlarges only the area it can reach, and
  the result for any texel is the same as a full rebuild. That reach is 47
  source pixels (`kKdcHdImageReach`): a shape's pieces span less than 40, and
  the relaxed fit below looks up to seven pixels past them. Each worker thread
  keeps its working memory between updates; allocating it per call queued the
  workers on the heap.
- When a line drawn on the course (the boundary line and its shadow) cuts a
  square, the pieces are no longer exact polygons on their own. A relaxed fit,
  tried only for regions of at least 12 pixels that the strict fit rejects,
  ignores colours other than the piece's own and its background, and borrows
  evidence from the same square's pieces across the line (up to six pixels
  away). Each piece then reproduces the whole square's outline, and leaves the
  line's own smoothed texels alone.
- Edge decisions use palette luma, but only once a palette has held still for
  30 updates. Colour cycling therefore costs nothing (it previously
  re-enlarged about 900 characters per frame on hole 1-1), while a fade or a
  newly loaded palette is caught up once. Colours always come from the live
  palette, because the images hold palette indices.
- `src/course_orbit.c` caches the course mesh by its shape bytes, projects it
  once per present, and draws the frame as 8-row bands on a small worker pool
  (`src/host_parallel.c`, half the logical processors, at most twelve;
  `KDC_THREADS=n` overrides). Each band clears its depth, draws scenery,
  terrain, ground decals, sprites and the HUD for its own rows. Magnified
  terrain is blended between neighbouring 4x texels, so rotated edges are
  smooth. Below 1x the existing footprint averaging over the source image
  is kept.

- The scenery redraws only the cells whose map word or character changed. Some
  backdrops still animate: the starry sky behind hole 1-1 flips two star
  characters, used in 93 cells across the whole backdrop, every other frame.
  Those cells span the image, so each twinkle re-enlarges nearly all of it
  (about 2.5 ms on 12 threads, 14 ms on one). Half-width tiles spread that
  work evenly across the workers. A tighter, still exact, area was estimated
  at a third of the image at best, and was not worth a second copy of the fit
  rules.

`KDC_ORBIT_PROFILE=1` prints, every 300 presents, the mean preparation and
banded drawing time, the thread count and how many characters were recomposed
and re-enlarged. It also prints every present slower than 8 ms, with the work
that frame did.

Offline, from memory dumps of five courses, a full build of the enlarged
terrain took 6 to 45 ms (hole 8-8's map is 2160x1216), done once per hole. A
changed character used across the map took 2 to 9 ms.

`tools/benchmark_hd.py --scenario orbit` (deterministic course with the orbit
camera, SDL dummy display, 2026-11-11, Ryzen 9 3900X), before and after:

| | Compose mean | Compose max | Frames per second |
|---|---|---|---|
| HD on, before | 29.5 ms | 72 ms | 34 |
| HD on, after | 3.6 ms | 15 ms | 85 |
| HD off, before | 5.8 ms | 13 ms | 82 |
| HD off, after | 0.4 ms | 1.5 ms | 127 |

The frame rate there includes emulation and the dummy display; the course
view itself now takes about 3.6 ms (1.5 ms preparation, 2.1 ms drawing) per
present on average. The `still` and `shot` scenarios, which do not use this
renderer, produce byte-identical images at unchanged speed.

### Reusing scenery animation, 2026-11-13

The orbit/high-refresh renderer was enlarging a backdrop again whenever its
characters changed, even when it returned to an animation frame seen two
updates earlier. On hole 1-1, the twinkling stars repeatedly invalidated most
of the scenery image. Smaller dirty rectangles cannot eliminate this repeated
work, because the scaler's neighbourhood extends across character boundaries.

`KdcSceneryUpdate` now retains four enlarged animation frames. A hit requires
an exact match of the entire decoded 256x256 source and the settled luma used
for edge decisions. The image contains indices, so live palette animation
continues without rebuilding it. Cache misses retain partial enlargement:
when replacing a different slot, the last displayed image seeds its unchanged
area. The bounded cache uses at most 4 MiB of HD indices and about 256 KiB of
keys (roughly 3.25 MiB more than before), and is freed on reset or HD disable.
The normal 4x backdrop sampler also walks consecutive texels directly instead
of dividing and taking a remainder for every output pixel. Other scales keep
the existing sampler.

A ROM-free two-phase animation replay on one thread fell from 5.778 to
0.068 ms per update after warmup. This is the cost of updating the scenery,
not a whole-game frame-rate claim. The regression fixture asserts zero further
enlargements after both phases have been seen, and checks exact pixels against
fresh enlargement through cache eviction, map/flip changes, palette animation,
settled palette changes and HD toggling. The orbit fixtures also check guest
state preservation and rendering at native and HD dimensions.

`KDC_ORBIT_PROFILE=1` now includes `scenery_enlargements` and
`scenery_cache_hits`. The deterministic benchmark accepts `--threads`, `--hole`
and `--max-compose-ms`, so composition can be budgeted independently of PPU
raster time. Example (all captures remain under ignored `local/`):

```powershell
python tools/benchmark_hd.py --executable local/hd-build/KirbyDreamCourseRecomp.exe --baseline local/hd-baseline/KirbyDreamCourseRecomp.exe --scenario orbit --repeat 2
```

Windows Release, Ryzen 9 3900X, SDL dummy/software display, widescreen,
unpaced, default twelve rendering threads; two runs against a baseline built
from the same revision (`d756846`) and the same pinned framework/dependencies:

| HD orbit | Before | After |
| --- | --- | --- |
| Composition mean, averaged across runs | 3.802 ms | 2.802 ms |
| Whole-run throughput, averaged across runs | 77.8 fps | 84.7 fps |
| Composition peak, run 1 / run 2 | 13.2 / 17.0 ms | 14.8 / 12.6 ms |

Composition fell about 26%; whole-run throughput increased about 9%. The
peaks remain at first orbit entry (frame 501), so this is not a cold-start
improvement. Every HD-on and HD-off frame-800 capture matched the baseline
byte-for-byte. Timing varies with host load; these are bounded replay results,
not a guarantee of full speed across courses or hardware.

With `--threads 1`, the same orbit replay fell from 19.740 to 12.507 ms of
HD composition (37% less), and rose from 43.1 to 53.5 fps. HD-off remained
110.5 fps. The images still matched exactly. This isolates rendering worker
count on the same CPU; it is not a substitute for testing a slower machine,
and the result still falls short of full game speed with just one worker.

Normal PPU-only views were unchanged within timing variation: HD still
85.0 to 86.6 fps, HD shot 78.7 to 79.2 fps. Their frame-340/frame-800 captures,
respectively, matched exactly with both HD settings. Windows Release builds
and six targeted tests passed (`test_course_terrain`, `test_course_orbit_render`,
`test_hd_image`, `test_hd_tiles`; the orbit module runs three fixtures).

The remaining improvement routes are distinct:

- Normal-view PPU tile recording and subpixel composition are unchanged by
  this work: still/shot HD raster costs about 5.3–5.6 ms versus about 1.2 ms
  with HD off in these runs. Moving that composition to a GPU would require
  preserving the existing per-pixel priority, window and colour-math exactness
  checks.
- Orbit terrain rasterization and the 4x surface transfer still run on the
  CPU. GPU rasterization could remove substantially more per-pixel work, but
  requires a separate renderer and differential image validation.
- First-time scenery/terrain enlargement still costs time. This cache removes
  recurring animation work; it does not remove cold course-entry spikes or
  guarantee that every backdrop's animation fits in four entries.

No reference-emulator comparison is claimed for enhanced subpixels. Baseline
image comparisons and synthetic guest-state preservation checks validate this
presentation-only change; Linux and slower physical machines remain untested.

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
- The orbit/high-refresh renderer draws directly at 4x resolution from the
  enlarged terrain and scenery images; sprites still use the PPU tile scaler.
  Its existing geometry, occlusion and colour-math limits still apply.
- The enlarged terrain image is 4x the source, so a 1:2 edge is a staircase of
  one-texel steps. Rotated, blending turns those into a faint ripple along the
  edge rather than a perfectly straight one.
- A terrain character that animates regenerates the enlargement around every
  place it is used: about 2 to 9 ms on the measured courses, on the frame it
  changes. Palette animation does not.
- HD CPU cost depends on the renderer and animation state; measured scenarios
  are recorded above. It has not been measured on slower machines or Linux.
- The per-cell memo trusts a cell for the rest of the frame unless VRAM is
  written through counted paths, CGRAM changes, or the frame advances. A
  widescreen tile provider whose answer changes mid-frame without such a
  write could give one frame of stale neighbour context; the exactness check
  still keeps every native pixel centre.

### HD persistence through course loading, 2026-11-10

With high refresh enabled, loading initially showed the PPU's HD surface, but
once the course composition became ready the host enlarged its native image.
The regression capture had zero within-texel detail at frames 340, 380 and 600.
`hd-composition` adds an optional host callback with an HD destination and native
field dimensions. The course renderer scales its presentation transform after
native camera framing/interpolation, samples terrain and original artwork at
4x, and retains native HUD coordinates. The host keeps the ordinary composition
fallback for games without the hook. PPU work can still pause while composition
owns the presented frame.

Both renderers share `hd_tile_source.c`. Orbit artwork is cached by source and
palette, revalidated once per draw against live words and colours. Bounded
associative admission prevents colliding sources from repeatedly regenerating
artwork per pixel; an exhausted bucket samples new sources natively for that
draw. Resolved terrain tiles are retained at 4x source resolution across camera
movement, revalidating both layers and their neighbour evidence once per draw.
Background reuse also checks output dimensions when switching between native
and HD presentation.

Validation: Windows Release build; deterministic real-ROM no-input `normal:1:1`
captures at frames 340, 380 and 600 retain HD detail with high refresh enabled.
The existing native HUD/title checks pass. Synthetic fixtures exercise HD
buffer bounds, HUD positioning (including negative edge shifts), interpolation,
rotation, changing output dimensions, live texture/palette updates and cache
pressure, while checking unchanged guest RAM and PPU state. Reproduce with:

```powershell
python -m unittest discover -s tests -p test_course_orbit_render.py
$env:KDC_TEST_HD='1'
python -m unittest discover -s tests -p test_hd_textures_runtime.py
```

A separate 480-frame deterministic rotation/zoom replay at widescreen width,
with edge HUD and high refresh enabled, produced identical guest-state traces
with HD off/on. Its frame-440 capture was inspected locally; assets and logs
remain under ignored `local/`.

This verifies the presentation fix, not new fidelity to a reference emulator:
the enhanced subpixels have no SNES equivalent. Linux, full-course playthroughs
and sustained high-refresh performance remain unqualified. Rendering the
composition at 4x adds CPU work compared with enlarging a native image. In the
bounded Windows SDL-dummy widescreen rotation/zoom replay (frames 450-480,
screenshot I/O excluded), composition averaged about 56 ms versus 7 ms with HD
off. Source caching, native-texel scenery/mask reuse and affine decal projection
reduced the initial HD implementation's roughly 152 ms, but this path did not
yet meet a 60 Hz budget on the test machine (since replaced; see
[Orbit terrain texture](#orbit-terrain-texture-2026-11-11)). The older PPU-only timing figures
above do not describe HD orbit/high-refresh composition. Cached analytic
terrain is sampled at 4x source-texel centres, so extreme zoom/rotation can still
show sampling steps.
