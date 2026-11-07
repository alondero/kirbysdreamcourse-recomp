# HUD inventory and edge-placement study — issue #31

**Placement recommendations superseded:** the follow-up [quiet/expanded study](hud-expansion.md)
keeps the shot HUD in place and treats the full panel as an extension of the
same ability badge. [Open the revised three options](hud-expansion.html?variant=C).
The original five variants below remain a historical design record.

Design exploration, 2026-11-07. No game behavior or settings have changed.
Open `hud-prototype.html` directly in a browser, or run
`python -m http.server 8031` from the repository and visit
`http://localhost:8031/docs/prototypes/hud-prototype.html?variant=A`.
Five throwaway concepts use original schematic artwork and sample values;
they are not screenshots, extracted assets, or functional game HUDs.

## Evidence and scope

The supplied USA ROM was checked with PowerShell `Get-FileHash -Algorithm
SHA256`: `0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.
No new ROM execution or reference comparison was needed for these mockups.
Local existing captures were visually inspected: `shot-host.png`,
`shot3900-host.png`, `course3-shot-phase.png`, `twoplayer-turns-host.png`,
and `twoplayer-impact.png` under the main checkout's ignored `local/`.
These captures are historical visual evidence, not newly reproduced traces.

Sources:

- [Nintendo game-screen and controls manual, mirrored text](https://manuals.plus/m/67c7da172a34ce3239bc014f14a9514b42e3772456425496419e9a37805b3ddf), sections 4, 6–8.
- [Original USA instruction booklet, mirrored text](https://device.report/manual/20422196), Information Panel, Shot Panel, Stock, Handicaps.
- [Course renderer](../../src/course_view.c), [sprite classification](../../src/course_sprites.c), [orbit compositor](../../src/course_orbit.c).
- [Existing widescreen evidence](../widescreen.md) and [sprite evidence](../widescreen-sprites.md).

The inventory covers documented interface families, including bounded screens
that could otherwise be mistaken for movable HUD. It is **not an exhaustive
runtime classification of every tile, OAM piece, animation or game scene**.
Boss displays, rare power effects, endings and all scorecard variants still
need captures. Do not treat unknown pieces as HUD based only on screen position.

## Element inventory

| Element / sub-elements | Evidence | Proposed treatment |
| --- | --- | --- |
| Registered player name or drawn symbol; player portrait | Manual + local solo/versus captures | Move with player status, preserving custom symbols |
| Current ability / conditions icon, including empty and flashing states | Manual + captured panel | Keep with its owner; no inferred ability-name decoding yet |
| Hole number | Manual + solo capture | Compact shared context near an edge |
| Shot count / solo score | Manual + solo capture | Keep distinct from multiplayer points |
| Energy gauge: four solo slots; six versus slots | Manual + captures | Keep full/empty slots and owner association |
| Stock / reserve-ball icons and count | USA manual + solo capture | Solo status only; distinguish reserves from active ball |
| Both players' running point totals | Manual + versus capture | Symmetric, labelled P1/P2 groups |
| Stars won on the current hole | Game-screen manual | Separate from running totals; exact rendering needs capture |
| Spin/effect panel shell, crosshair, horizontal bar, target pointer and moving marker | Manuals + shot-phase captures; `ShotHudCaller` | Translate as one group; retain vertical spin geometry |
| Power meter track, moving fill and full-power indication | Manuals + shot-phase captures | Move with shot panel; do not alter timing or direction |
| Shot-panel energy/portrait companions | Shot-phase captures | Track panel visibility and ownership; do not duplicate full status bar |
| Aim guideline, bounce/trajectory marks | Manual + capture | World anchored; never pin to display edges |
| Out-of-bounds “OB” label at guideline end | Game-screen manual | Stay with trajectory; capture needed for runtime classification |
| Player/turn emphasis | Versus context; exact live emphasis unqualified | Explicit active-player label is a proposed modern addition |
| Scoreboard, between-hole scorecard, animated digits and totals | Manual; `ScorecardTask`; documented scorecard replay | Center as a complete bounded surface |
| Pause/message panel and selection cursor | Manual | Center together; suppress competing shot controls |
| Multiplayer order dice, portraits, handicap controls: shot power, guide length, marker speed, exit | Manual + local setup capture | Keep bounded and centered |
| Course-select label, course-introduction frame | Existing renderer signatures and replay docs | Keep centered with their scene; not gameplay status |
| Title/mode menus, member selection/name editor, erase/continue dialogs | Manual + renderer docs | Preserve centered foreground and cursor relationships |
| Result/medal, game-over/continue, boss and ending overlays | Manual identifies these scene families; full rendering unverified | Conservative original presentation pending capture |
| Ability duration indicator (e.g. UFO), transient rewards/messages, rare effects | Not established by inspected captures | Audit whether an on-screen element exists; do not invent a timer |

## Implementation evidence

The gameplay BG3 map at VRAM word `$3C00` overlays clean scenery retained at
WRAM `$7E:DADC`. The documented normal-course samples place panel differences
on tile rows 22–26. That difference identifies occupied scenery regions, not
the semantic boundaries of individual counters. A relocation needs clean
background restoration, live tile attributes, transparency, priority and color
math; moving final framebuffer rectangles would also move terrain behind gaps.

`ShotHudCaller` identifies nine call sites under `$84:90xx/$84:91xx`; its
comment records the fixed shot origin `$6B40/$6B42`. This is a useful group
boundary, not proof that every HUD sprite is covered. `ScorecardTask` identifies
types `$8C/$8D` with specific callbacks. Those digits must stay with scorecards.
The orbit renderer currently preserves complete occupied HUD row spans from
the native PPU output, so it must eventually share the same placement policy.

## Five design questions

| Variant | Structure | What it tests | Main tradeoff |
| --- | --- | --- | --- |
| A — Original corners | Square, retro-style status blocks in lower corners; shot panel above left block | Can familiar information simply move outward? | Requires reliable tile/sprite grouping; artwork here is schematic |
| B — Soft dock | Rounded bottom status dock, separate right-side shot instrument | Does a modern card treatment retain the game's warmth? | A complete reskin needs semantic value extraction |
| C — Side rails | Vertical player ledgers; shot instrument on active player's rail | Can side margins carry most interface weight? | More vertical occupation; narrow views need fallback |
| D — Quiet strip | Small top status line; shot control appears at bottom only while preparing | Does lower visual weight improve course visibility? | Tiny text/readability and contextual visibility need playtesting |
| E — Match scoreboard | Mirrored top player cards, shared hole context, fixed lower-right shot control | Can versus play gain clear ownership and stable eye targets? | Greater top-edge coverage; must work for solo too |

All five retain a world-space guideline. Status stays stable during a shot;
there is no moving target while timing input. Menu/scorecard previews show a
centered fallback. Unsupported signatures, mosaic and transitions should use
the existing centered presentation until independently qualified.

The preview uses constant HUD size relative to viewport height. Safe padding
is adjustable. Optional comfortable reach limits horizontal anchors to a
centered 21:9 region at 32:9; disabling it demonstrates literal outer-edge
placement. The original field outline is a layout reference, not a claim about
SNES pixel aspect. 4:3 intentionally falls back to one centered schematic bar.
Modern variants' labels, typography, cards and turn badge are design additions.
Sample point totals and current-hole stars are separately labelled.

## Next evidence gates

Choose a visual direction before production implementation. Then capture the
full solo/versus shot cycle, abilities, damage/energy loss, stock loss, hole
completion, pause/scoreboards, boss, transitions and load/reset. Identify each
group's live tile/sprite source and visibility; qualify both default and orbit
renderers. Preserve centered as the persisted default. Replay deterministic
inputs at native, 16:9, 21:9 and 32:9 widths; compare guest state and centered
output with the pinned baseline. Record first divergence and untested paths.
These prototypes establish no state-equivalence or emulator-fidelity result.

## Prototype verification

Chromium via Playwright: visually inspected all five variants in two-player
16:9 mode and the centered 4:3 fallback. Eighty combinations of variant, aspect,
player count and safe inset (3/7 percent) passed panel-width and viewport-bounds
checks. Forty two-player combinations passed pairwise overlap checks, excluding
the intentional shot instrument inside variant C's rail. All seven phase
previews passed shot/modal visibility checks; arrow-key variant switching passed.
Review caught and fixed fallback panel overflow/overlap and a P2 trajectory that
incorrectly originated at P1. These are prototype checks, not game regressions.
No C build was run because no compiled source or build configuration changed.
Screenshots and browser inspection scripts remain ignored locally.
