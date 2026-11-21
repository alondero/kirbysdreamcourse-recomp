# Quiet and expanded HUD: revised design study

2026-11-07, follow-up to issue #31 and draft PR #36. This supersedes the
placement recommendations in the [first five-concept study](hud-inventory.md).
Open [the revised interactive prototype](hud-expansion.html?variant=C).
Use Quiet / Expanded (or Space outside form controls) to compare; A/B/C or
arrow keys change layout. The original game implementation is unchanged.

## Design decision from the user

Keep the shot/spin/power HUD in its original game-controlled position. The
feature concerns the persistent small ability/conditions portrait and the
larger status panel that appears around it. The ability portrait must retain
the same position, dimensions, artwork and owner across those two states.
The expanded panel should feel like an extension of that existing anchor.

The earlier concepts incorrectly made full status persist and moved the shot
instrument to an edge. A generic P1 portrait plus separate ability text also
missed the original relationship. The revised study uses the ability itself
as the quiet badge and keeps the registered player drawing separate.

## What the evidence supports

The supplied USA ROM SHA-256 was rechecked:
`0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.
No ROM-derived assets are included. These are schematic visuals with sample
values, not pixel-accurate reconstructions or a new execution trace.

Historical local captures were inspected again in the main checkout's ignored
`local/`: `shot-host.png`, `shot3900-host.png`, `course3-shot-phase.png`, and
`twoplayer-turns-host.png`. The first pair shows an isolated lower-left portrait
and the expanded strip at that same location. The phase contact sheet shows
the portrait surviving both quiet and expanded scenes, plus a separate shot
instrument and an energy row directly below it. The versus capture shows the
two colored conditions portraits, P1/P2 labels, point totals and six-slot
energy gauges. This is visual evidence of layout, not a trace of the exact
trigger or a proof that every juxtaposed frame is consecutive.

The [original USA manual text](https://device.report/manual/20422196) identifies
conditions as current powers, the registered mark as player identity, and
hole, shots, energy and stock as solo status. It describes versus point totals
and each player's conditions/energy. The revised preview uses those fields.
It omits the previously proposed extra current-hole-star counter because its
separate runtime display has not been established by the inspected captures.

| Information | What is available now | Constraint for implementation |
| --- | --- | --- |
| Ability/conditions portrait, including normal state and flashes | Live visual output in both HUD forms | Preserve the actual portrait, palette and animation; do not replace it with a guessed ability name |
| Registered name/drawing | Live artwork in solo's expanded panel | Preserve player-created content; it is not the ability icon |
| Hole and shots | Existing displayed counters | Relocate original fields; no predicted score, par or new course-progress widget |
| Solo energy and stock | Existing gauge and reserve count | Preserve empty slots and original stock meaning; stock is not multiplayer energy |
| Versus scores and energy | Both existing owner-specific displays | Preserve pink/P1 and yellow/P2 associations; no solo stock or shot fields added |
| Shot instrument and its nearby energy | Captured output; shot callers already classified | Retain original coordinates and companion display; status relocation must exclude them |
| Quiet/expanded visibility | Visually evident; exact flag/routine not mapped | Follow original visibility; do not infer from a timer, ball motion, or our preview phase selector |

`ShotHudCaller` in [course_sprites.c](../../src/course_sprites.c) identifies nine
shot-related call sites. Its comment identifies origin words `$6B40/$6B42`.
This does **not** establish that the meter is always at the mathematical screen
center or that it follows Kirby with a host-defined offset. Leave the game's
coordinates and timing alone. The preview uses a fixed schematic native-field
sample, with the active ball near native `(48,100)` and the panel near
`(24,118)`, roughly matching the inspected shot-phase composition. This is an
illustration, not a newly measured invariant or a tracking algorithm proposal.

The existing clean BG3 scenery buffer and shot classification provide useful
seams. They do not yet provide semantic APIs such as `ability`, `energy`,
`stock`, or `hud_expanded`. Visible data availability is distinct from verified
memory addresses. Prefer moving live rendered groups with their existing
visibility over rebuilding a semantic HUD from guessed RAM fields. Even that
requires tile/sprite ownership, color math, clipping and restoration work.

## Three realistic options

| Option | Quiet state | Expanded state | Assessment |
| --- | --- | --- | --- |
| A — Attached ribbon | Ability badge at lower-left; P2 at lower-right in versus | A shallow strip grows inward from each badge. Solo includes registered drawing, hole, shots, energy and stock. Versus includes the owner's points and energy. | Closest to the original visual hierarchy; best first implementation candidate. Solo remains a relatively long strip. |
| B — Compact elbow | Identical badge and anchor | Details stack upward, with the badge forming the outer lower corner | Less horizontal coverage, but occupies more scenery height. Needs rearranged field groups and frame artwork. |
| C — Split essentials | Identical badge and anchor; no permanent energy sliver | Only energy extends inward from each badge. Solo's drawing/hole/shots/stock appears lower-right; versus totals appear together top-center. | Recommended design to test: small local expansion and a clear lower middle. Requires eye travel and synchronized visibility for all separated groups. |

All extra groups share the game's expanded-state visibility. C must not leave
the score display permanently visible after the rest collapses. No new timing,
fades, flashing warnings, low-energy notifications or contextual reveal logic
are proposed. The prototype's independent detail and shot controls intentionally
allow combinations for layout inspection; they are not a model of when the
original game chooses to expand.

Original ability artwork should be reused during implementation. The umbrella,
flame, neutral portrait and registered scribble here are original geometric
stand-ins only. The quiet badge is the same DOM geometry as the expanded badge;
there is no duplicate portrait in the added information. Modernization here is
limited to spacing, compact grouping and modest rounding in B/C.

At 32:9 the optional centered 21:9 reach region avoids pushing status to extreme
peripheral vision. Disable it to compare literal outer edges. At 4:3 the demo
uses a centered A-style ribbon fallback; it is schematic, not an exact original
HUD. Production centered/default mode and unsupported scenes must continue to
use the existing original renderer. Menus, scorecards, boss and endings are
outside this relocation study and retain their original placement.

## Recommended path

Compare A and C by toggling quiet/expanded while viewing the original shot
instrument. Choose A if original grouping matters most; choose C if minimizing
the expanded HUD's footprint around the ball is the priority. B is the compact
single-corner alternative, with a greater vertical-occlusion tradeoff.

Before implementation, trace a deterministic quiet → expanded → quiet route,
record exact visibility/ownership sources, and include ability changes, empty
energy, two-player turn changes, stock loss, and menus. Preserve each player's
ability anchor for every sampled transition and prove the original shot group
is unchanged. Compare state and default pixels with the pinned baseline;
separately qualify widescreen and orbit compositing. The earlier unqualified
boss/ending/rare-effect inventory remains unqualified. No first-divergence or
emulator-equivalence claim is made by this design study.

## Prototype verification

Chromium inspection covered A/B/C in solo and versus. A 48-case browser sweep
(three layouts, four aspects, two player counts, reach limit on/off) confirmed
identical badge and shot rectangles before/after expanding, no visible expanded
groups while quiet, no panel horizontal overflow, and all panels inside the
viewport. Separate checks confirmed identical shot geometry across the three
designs, the four shot-preview visibility modes, identical ability artwork
across quiet/expanded for all three samples, and the three energy samples.
Browser console reported no errors or warnings. Review caught and corrected
C's versus totals remaining visible when quiet and widened its six-slot gauge;
B's versus details were explicitly stacked. `git diff --check` passed.
No C build was needed; production code and build configuration are unchanged.
