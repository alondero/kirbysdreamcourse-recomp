# Playable orbit camera

The experimental camera rebuilds the course as a height mesh and draws the
original characters, enemies, aiming guide, shadows and effects at world
anchors. Shot physics and SNES input remain unchanged. This is a separate
presentation renderer, not a rotation of the finished framebuffer.

## Controls

**Orbit camera** is enabled by default on the launcher's **Mods** page.
The original renderer remains active until camera input is used on a hole.

| Input | Action |
| --- | --- |
| Player one's right stick left/right | Orbit |
| Right stick up/down | Zoom in/out |
| Right-stick click (R3) | Restore original view |
| Keypad 4/6, 8/2, 5 | Orbit, zoom, reset respectively |

The shared camera uses player one's configured gamepad seat, including in
two-player games. Guest bindings take precedence over R3 and keypad shortcuts.
Camera input is suspended while paused, unfocused, in the launcher or during
speculative frames. The option is saved beside the executable in
`kdc-camera.cfg`. Disabling it restores the original renderer.

Full stick deflection turns 90 degrees per second. Zoom changes exponentially
at one octave per second, bounded to 0.25x through 4x, with a 20% radial
deadzone. Rotation and zoom pivot on the ground beneath the active player's
ball. The pivot height eases to the terrain under the ball (0.1 s time
constant) rather than following the ball through the air, so bounces do not
shake the course. When the ball's anchor would come within 8 pixels of the
bottom edge, or within 8 pixels plus a 16-pixel sprite (scaled by zoom) of
the top, the view shifts vertically just enough to keep it in frame: mainly a
high ball when zoomed in, and the player at the top of the original framing at
4x. Nearer terrain or scenery can still hide the player (see
[Limits](#limits)). While aiming, the original camera frames
the player off center; that look-ahead is kept at the original angle, is not
magnified when zooming in, shrinks with the course when zooming out, and fades
out by a quarter turn, from where the player is centered. At 1x and the
original angle the framing is exactly the native view. Reset, timeline restoration,
leaving a hole and changing maps clear the camera. Fades and mosaic transitions
temporarily use the native renderer.

## Implementation and evidence

The supported headerless 1 MiB USA ROM has SHA-256
`0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.
All addresses below refer to that image. Evidence comes from its local
disassembly using the pinned framework's decoder and deterministic captures.
REA engines were unavailable; these are not REA evidence IDs. The framework
remains pinned to `147b55e252dcb3e7680a99c7c2f50e7183891844`.

`course_camera.c` owns camera math; `course_camera_host.c` connects live input
and deterministic camera playback. The additive framework patch exposes raw
right-stick state for the configured seat without changing guest buttons.
`course_geometry.c` reads collision data, and `course_orbit.c` rasterizes
terrain and billboards with a depth buffer. `course_sprites.c` observes original
draw producers and retains their world anchors through the OAM frame latch.
These modules do not write guest RAM or replace collision/physics.

The collision loader at `$82:B1CA` fills planes with capacity `$800` cells:

| WRAM | Meaning |
| --- | --- |
| `$8B92/$8B94` | Grid width/height |
| `$6B90` | Shape: zero is void, 1-27 are supported |
| `$7390` | Base elevation index |
| `$7B90` | Edge flags, not consumed by the mesh |
| `$8390` | Dispatch/object data, not an established texture material ID |
| `$8B9C/$8B9E` | Original projected-map origin |

ROM slope selectors at `$81:9D81`, diagonal tables at `$81:9569/$81:9629`
and elevation tables at `$88:8000/$88:80C8` produce four triangles per cell
meeting at its center. Four corners alone do not represent every shape.
The geometry smooths the original fixed-point quantization. Coordinates use
cell units and upward elevation steps:

```
screen_x = origin_x + 32 * (x + y)
screen_y = origin_y + 16 * (x - y - z)
```

Top surfaces and originally visible cliffs sample the runtime BG1/BG2 map,
VRAM and palette. Newly visible cliff backs use a color derived from the
original cliff palette: no back-face artwork has been identified. Outer and
void-facing walls end at elevation zero, the depth the native artwork shows
on hole 1-1 (the only hole measured).
The clean BG3 scenery map at `$DADC` supplies the background. Outside the
native field it is reflected at whole-tile boundaries with the same
`KdcReflectedColumn` rule as the [widescreen](widescreen.md) renderer, so
zooming or turning never exposes the raw 256-pixel wrap. Changed HUD spans are
copied from the native frame to retain color math and portrait composition.
Below 1x, terrain and sprite texels are averaged over each pixel's footprint
(up to 4x4 samples) instead of point sampled, which removes checkerboard and
sprite shimmer when zoomed out.

Sprite anchors use observed projector output at `$81:A5CC`, verified entity
registry/task identity and dedicated producer hooks. Original OBJ graphics,
animation, palettes and piece offsets remain intact. Bodies are billboards
standing on their anchors; rows the original image draws below the anchor
(the cup ring, shadow pieces drawn with an enemy body, feet) lie on a flat
plane at the anchor's height in front of it instead of being buried in the
terrain; terrain rising towards the camera can still cover them. Anchors within a quarter step below the mesh
rest on it, absorbing fixed-point height quantization. Ground sprites (the
tee, separate ball shadows and guide marks) are projected onto the visible terrain as a decal:
each pixel is unprojected to the course and mapped back through the native
projection, so it turns with the course and follows slopes. Inherently 2D
particles retain their original motion relative to the spawning actor's
world anchor.

The original game also masks sprites with Mode 1 OBJ priority: priority 0/1
pieces lie behind every opaque BG1/BG2 pixel, priority 2 behind
high-priority tiles and priority 3 in front. The cup's interior pieces
(priority 0) only show through the hole cut into the floor tiles. The orbit
renderer applies the same test at each texel's native screen position before
depth testing, so these masks follow the artwork at any angle. Equal depths
are compared at depth-buffer precision so original OBJ order decides ties.

| Object | U low/high | V low/high | Z low/high |
| --- | --- | --- | --- |
| Player ball | `$D8AA/$D8AE` | `$D8B2/$D8B6` | `$D8BA/$D8BE` |
| Flight guide | `$B746/$BB46` | `$BF46/$C346` | `$C746/$CB46` |
| Landing markers | `$6B0C/$6B12` | `$6B18/$6B1E` | `$6B24/$6B2A` |

Ball offsets are 0/2 for the players; sample arrays use even offsets. Guide
ground heights are `$CF46/$D346`. Guide hooks are `$81:AEFC/$AF2D/$AF58`;
marker hooks are `$81:AE70/$AEA1`. The entity registry starts at `$B4B8`
(kind), `$B4F8` (task), `$B538/$B578` (U/V), `$B5B8/$B5F8` (Z) and
`$B638/$B678` (projected position). Generic task fields are reused and are
not universally interpreted as physical coordinates.

Task allocation clears sidecars; clone/spawn hooks preserve attachment
anchors. Type BE radial particles retain the frozen ball anchor, while type
5D rewards retain the verified enemy parent. Recovery after a host metadata
reset uses validated live task fields. HUD/scorecard producers remain fixed
on screen. Observation hooks are in source recompilation configuration and
interpreter registration; generated source is never edited.

## Validation

Run the ROM-free suite with:

```
python -m unittest discover -s tests -p 'test_*.py' -v
```

Set `SNESRECOMP_ROOT` to a prepared patched framework and `RECOMP_UI_ROOT` to
the pinned launcher source when using non-default build directories.
`KDC_TEST_CAMERA=1` enables the ROM-backed camera replay; use
`KDC_CAMERA_EXECUTABLE` to select the executable. On Windows/Ninja,
`KDC_TEST_SDL_CAMERA=1` enables the real SDL virtual-controller test; set
`KDC_CAMERA_BUILD` for a non-default build directory.

Fixtures cover projection, zoom/deadzone/reset, geometry branches, sprite
anchors and task recovery, terrain occlusion, ground/body ordering, HUD,
widescreen/pitch bounds, high-elevation initialization, transition fallback
and read-only RAM/PPU behavior. SDL tests cover seat selection, axis input,
R3 conflicts, guest-input isolation, focus/pause/modal gating and disconnect.

Local Windows validation on 2026-11-05:

- Release configuration and build passed. The full suite ran 60 tests,
  including the enabled ROM replay and SDL integration checks; six unrelated
  optional checks were skipped. All 52 setup patches applied in order to a
  clean checkout of the pinned framework.
- Camera-off output matched the original executable's frame and RAM capture.
- A 900-frame shot replay compared native and interpreter camera rendering,
  RAM and exact return to native pixels after reset.
- A 1,200-frame moving-camera shot replay matched all 610 sampled RAM frames;
  99 post-reset images matched the native renderer.
- A 13,200-frame original-menu replay completed hole one and entered hole
  two (map change at frame 12067). All 1,400 sampled RAM frames matched the
  camera-off run, as did all 200 post-reset images. First RAM divergence: none.
- A 6,600-frame replay exercising radial particles and reward effects matched
  all 1,000 sampled RAM frames, with no unresolved sprite anchors in that
  sampled gameplay interval. First RAM divergence: none.
- Elevated and sloped holes were inspected in normal, extra and multiplayer
  modes, including normal 8:8. Original sprites and guides remain present.
- Focus, sinking and zoomed-out fixes (same date): renderer fixtures cover
  4x focus, zoom-out centering, quarter-turn centering, pivot height tracking,
  feet and the ground plane below anchors, decal orientation and ordering,
  a high ball kept in frame at 4x, wall height, reflected scenery margins,
  OBJ priorities 0-3 over low- and high-priority tiles at two angles, and
  zoomed-out terrain, sprite and decal filtering. The full suite ran 60 tests with the
  ROM replay and SDL checks enabled; seven unrelated optional checks were
  skipped. In 342-column captures, Kirby stayed on screen at 4x on hole 1-1
  (previously off screen). Sampled upright sprite pixels hidden by terrain fell
  from 16.0% to 6.7% across all 64 normal holes at 135 degrees; the inspected
  remainder were covered by nearer terrain or scenery. The cup ring was complete at 90 and -60 degrees in the
  hole-completion recording. These are presentation-only changes: guest
  RAM is unchanged (the shot replay above still matches), and no reference
  emulator can render this view, so there is no reference divergence to
  report. The in-frame clamp moved the resting 4x view of hole 1-1 down by
  about 7 pixels; zoomed-out and turned captures were unchanged. Untested
  paths: a real lofted shot while zoomed in (fixture only), the second
  player's focus, the ball over the void,
  rows below anchors on slopes, decals beside nearer walls at a similar
  height, and HUD rows with panels at both edges.

Recordings, captures, private generated source and ROM-derived images stay in
ignored `local/`. The hole-completion recording SHA-256 is
`537d4c7c310d82ca3353d35fe78442698107b56f51d004d94242e962ac6d18fa`;
its initial zero-filled 8192-byte SRAM SHA-256 is
`9f1dcbc35c350d6027f98be0f5c8b43b42ca52b7604459c0c42be3aa88913d47`.

## Limits

The renderer is experimental. The original game provides only projected
terrain artwork; sampling it can duplicate an occluding surface onto another
surface, and reconstructed cliff backs do not reproduce original textures.
Characters remain original 2D billboards, not newly modeled characters.
Unknown sprite producers retain a conservative screen-space fallback.
Tall scenery or terrain nearer the camera can hide the player at some angles
(for example the tree on hole 1-3 at 135 degrees); no see-through silhouette
is drawn. Ground shadows that the original game draws as background artwork,
such as tree shadows, are not reproduced by the orbit renderer. OBJ
priority masks are tested at each texel's original screen position, which is
correct for artwork-relative masks such as the cup; a priority-2 sprite behind
high-priority scenery would keep that scenery's outline after turning. No such
sprite was observed in the sampled aiming frames of the 64 normal holes.

Every power, moving object, two-player turn and map combination has not been
played through. Save/load anchor recovery has fixture coverage, not a full
runtime save/load replay of every effect. Physical controller feel and this
renderer on Linux/Steam Deck remain untested. No new reference-emulator
comparison was performed: camera-specific checks use the unchanged host as
the baseline. Existing host/reference differences remain documented in
[compatibility](compatibility.md); camera-on parity does not resolve them.
