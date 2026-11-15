# Faster transitions: Super Metroid precedent

Research snapshot: 2026-11-15, for issue #32. This is source inspection,
not a reproduced upstream gameplay test or evidence of Kirby compatibility.

## Exact sources

The requested `main/docs/releases/v0.4.0.md` link was inspected with GitHub's
API. At inspection, `main` resolved to
`113951fc32f9783c1be0a947ea17c9aabfba9066`; the `v0.4.0` tag resolves directly
to commit `1453c1331ea879a4bebab229b37da60a0ca10338`. Links below pin the
release commit. GitHub's commit comparison reports no changes to the release
notes, door/elevator plugins or pacing-route test between those commits; the
release manifest and license were separately read at the release pin.

- [Release notes](https://github.com/mstan/SuperMetroidRecomp/blob/1453c1331ea879a4bebab229b37da60a0ca10338/docs/releases/v0.4.0.md)
  describe optional native enhancements for an unmodified supported ROM,
  including faster doors and elevators. Elective features are disabled by
  default. Reported verification includes targeted gameplay, stock-behavior
  and compiled/interpreted comparisons, but explicitly excludes a full-game
  audit. These are upstream claims, not tests rerun here.
- [Pacing manifest](https://github.com/mstan/SuperMetroidRecomp/blob/1453c1331ea879a4bebab229b37da60a0ca10338/mods/preloaded/packages/super-metroid.pacing/1.0.0/manifest.toml)
  independently switches features off by default, offers elevator speeds
  `2x`/`3x` and door speeds `2x`/`max`, and targets a particular Super Metroid
  ROM hash. A feature's default option does not enable the feature.
- [Door implementation](https://github.com/mstan/SuperMetroidRecomp/blob/1453c1331ea879a4bebab229b37da60a0ca10338/src/mods/sm_door_plugin.c)
  changes annotated immediate operands for scroll distance, iteration counts
  and fades, with hooks correcting Samus's speed and placement. Its comments
  report rendering failures with larger steps: the map uploader imposes a
  limit, and the verified maximum differs by direction. This is targeted
  game-state work, not a general host fast-forward mechanism.
- [Elevator implementation](https://github.com/mstan/SuperMetroidRecomp/blob/1453c1331ea879a4bebab229b37da60a0ca10338/src/mods/sm_elevator_plugin.c)
  scales pixel/subpixel operands in the game's movement routines; its
  documented destination clamp preserves the stopping position.
- [Pacing-route test](https://github.com/mstan/SuperMetroidRecomp/blob/1453c1331ea879a4bebab229b37da60a0ca10338/tools/test_qol_pacing_routes.py)
  uses setup cheats, then exercises bidirectional elevators and boss gates.
  It checks destination/game state, elevator SRAM preservation and shorter
  transition durations. Its comparison mode checks observed frames and full
  WRAM snapshots against another run and verifies increased interpreter
  execution. It does not itself compare against an external emulator.

## Implications for Kirby

The useful precedent is a disabled-by-default, narrowly scoped enhancement
with explicit state boundaries, unchanged destinations, and deterministic
checks of timing and persistent state. The source also illustrates why a
single speed multiplier can break rendering or placement.

It does **not** identify Kirby's score tally, hole-change state machine,
timers, input handling, sound sequencing or save boundaries. Super Metroid
addresses, limits and hooks must not be transplanted. Nor does a shared
framework prove that Kirby's current pinned integration exposes the same mod
APIs. No upstream code was copied and no ROM was accessed for this note.

Under [the architecture contract](../architecture.md), a Kirby follow-up
must first identify the exact supported ROM and establish deterministic
baseline transitions. Candidate changes need unchanged default behavior,
state-aligned comparisons with a pinned reference where practical, matching
score/hole/save outcomes, and explicit reporting of the first divergence and
untested modes. Reduced wall-clock time alone is insufficient evidence.

The [upstream root license](https://github.com/mstan/SuperMetroidRecomp/blob/1453c1331ea879a4bebab229b37da60a0ca10338/LICENSE)
is PolyForm Noncommercial 1.0.0, while the pacing manifest declares MIT for
its package. That declaration is not evidence that every implementation or
dependency can be copied under MIT; inspect applicable file/dependency terms
before any reuse. This investigation proposes no dependency adoption.
