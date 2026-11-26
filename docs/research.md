# SNES recompilation research

Research snapshot: 2026-10-28. Project descriptions are self-reported; status and dependency revisions change. Links are primary project sources.

Optional pacing investigation, 2026-11-15: [score and hole transitions](faster-transitions.md)
records candidate boundaries, deterministic reference acknowledgement experiments,
and unresolved score/RNG safety gates. No pacing option is enabled or shipped.

Higher-refresh follow-up, 2026-11-07: [presentation investigation](high-refresh-presentation.md)
records the pinned clock/renderer assessment, optional camera/object
interpolation, the ROM-free rate probe and deterministic real-game validation.

Launcher and settings follow-up, 2026-11-01: [source comparison and Kirby integration](menu-research.md) covers Super Mario World, Super Metroid, DKC1 and DKC2, with exact framework/UI pins and local validation.

## Common architecture

[SNESRecomp](https://github.com/RetroPortingToolKit/snesrecomp) translates 65816 instructions to C and links the result with a shared SNES hardware runtime. Unresolved code can execute in an interpreter. The game project supplies analysis metadata and integration; framework compatibility does not itself make an arbitrary ROM playable. The [Super Mario World project](https://github.com/mstan/SuperMarioWorldRecomp) describes the runtime as LakeSnes-derived with Snes9x-attributed algorithms: the PPU, SPC700/APU, DMA/HDMA, memory mapping and register I/O remain hardware emulation, not native translations of game logic.

| Project | Relevant practice | Source |
| --- | --- | --- |
| Donkey Kong Country | Uses a pinned SNESRecomp fork, a headless validation host and locally generated code; its disassembly-derived metadata has separate provenance. | [DKC1Recomp](https://github.com/elliotttate/DKC1Recomp) |
| Donkey Kong Country 2 | Tracks 65816 processor modes and widths during analysis, resolves proven control flow to C, and falls back for unavailable exact entry states. The project compares deterministic input and machine-state checkpoints, with an emulator as oracle. SDL2 supplies a portable desktop host. | [DKC2 architecture](https://github.com/elliotttate/DKC2Recomp/blob/main/docs/ARCHITECTURE.md), [DKC2 README](https://github.com/elliotttate/DKC2Recomp) |
| Super Mario World | Keeps per-game runtime, analysis configuration, generated C and build glue apart from the shared hardware framework. | [SuperMarioWorldRecomp](https://github.com/mstan/SuperMarioWorldRecomp) |
| Super Metroid | Uses a ROM/interpreter ground truth, exact M/X variants and optional HLE optimizations; generated ROM-derived C is local. Its tested routes do not establish full-game completion. | [SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp) |

A static coverage count does not prove runtime coverage: DKC2 documents a later indirect target that was not covered by its static graph. Track actual fallback and indirect dispatch, and replay real input routes alongside structural analysis. [DKC2 README](https://github.com/elliotttate/DKC2Recomp)

## Runtime gameplay validation

- **Automobili Lamborghini Recomp** starts at a race warp, a local state, or the original menus. Its runner applies controller replay on game updates, isolates settings and saves, bounds the run, and checks the observed circuit, replay progress, vehicle motion and capture. The one-frame A scenario verifies that a short press reaches a game update. The Windows window-input helper supports interactive menu checks. Kirby reuses its existing frame script and developer hole selector for the equivalent workflow. [Harness design](https://github.com/alondero/automobililamborghini-recomp/blob/b17dbd326b14a05685a4a891babf9bde82e9d47e/docs/automation-harness.md), [runner](https://github.com/alondero/automobililamborghini-recomp/blob/b17dbd326b14a05685a4a891babf9bde82e9d47e/tools/run_game_scenario.py)
- **Donkey Kong Country** defines routes as inputs, bounded waits on WRAM predicates, and named checkpoints. Its route tool can launch the headless runner or the visible desktop host and records the ROM, runner, recipe and optional snapshot hashes. The project also calls for reproducing a tester's exact state and entering the same scene again from an earlier clean state. [Route tool](https://github.com/elliotttate/DKC1Recomp/blob/3eb9a10c49cb210df2bd63addafe2a78cf9f4e22/tools/run_route_recipe.py), [project validation rules](https://github.com/elliotttate/DKC1Recomp/blob/3eb9a10c49cb210df2bd63addafe2a78cf9f4e22/AGENTS.md)
- **Donkey Kong Country 2** records controller input in the desktop host and replays it in a headless executable. The Pirate Panic gate checks level entry, time spent in the level, completion flags, an exit transition, audio clipping, and unresolved dispatch diagnostics. The documented replay reached the exit but failed the gate at an unresolved dispatch. The project records the failing frame and identifies reference-emulator comparison as follow-up work for that route. [Route documentation](https://github.com/elliotttate/DKC2Recomp/blob/3b386cb4a6b67472bc6693664ce61a11190e7b5d/docs/FIRST_LEVEL_ROUTE_TESTING.md), [route gate](https://github.com/elliotttate/DKC2Recomp/blob/3b386cb4a6b67472bc6693664ce61a11190e7b5d/scripts/test_pirate_panic_route.ps1)
- **Super Mario World** launches its desktop executable, restores a known save at a normal frame boundary, and drives the running game with controller inputs. Its Falcon route tool captures player and sprite state, screenshots, and the object data the PPU rendered. The project also documents a Snes9x comparison that traces WRAM writes to find the first bad write behind a later visible symptom. [Controller route](https://github.com/mstan/SuperMarioWorldRecomp/blob/9cae67dbafddf4ab91068771d7136559e8adbf2e/tools/falcon_natural_route.py), [reference debugging](https://github.com/mstan/SuperMarioWorldRecomp/blob/9cae67dbafddf4ab91068771d7136559e8adbf2e/docs/GOLDEN_TESTING.md)
- **Super Metroid** runs isolated gameplay checks with private starting states and compares stock and enabled settings under native and interpreter execution. Its save-station check uses controller input to activate the station, verifies the resource refill at the prompt, and reads back the saved resource values and checksums from SRAM. Its X-ray check verifies angular movement and inspects the rendered beam. [Feature validation report and commands](https://github.com/mstan/SuperMetroidRecomp/blob/192aa995113f2407fa03c70bf471ea06d25f41f5/docs/validation/SM_ISOLATED_FEATURE_CHECKS_20261028.md)

For Dream Course, these examples support extending the existing recorded routes with short, bounded scenarios that reach the affected game path through controller input. Capture the expected transition or result and frame numbers. For an optional effect, exercise the enabled and disabled settings on the same starting state and input. Check game state for rules or physics, the rendered frame for visual effects, and a fresh-process reload for persistence. Use the pinned reference for stock behavior where practical and record the first divergence and untested paths. Keep recordings, saves, and captures local. [Validation contract](../architecture.md#validation-contract), [Dream Course runtime evidence](compatibility.md)

## Starting a new game

The framework offers a [project setup guide](https://github.com/RetroPortingToolKit/snesrecomp/blob/main/docs/GAME_PROJECT_SETUP.md) and ROM probe. Probe mapping and enhancement hardware before choosing a runtime; the [compatibility table](https://github.com/RetroPortingToolKit/snesrecomp#rom-compatibility) distinguishes implemented chips from unsupported or title-gated ones. Probe output is evidence, not validation of this specific game. The [Kirby Super Star project](https://github.com/NVDEMU/KirbySuperStarPC) describes a practical sequence: beam-aligned frame driver, identified symbols and AOT roots, then investigate unresolved dispatches. It is a different game and does not establish Dream Course hardware or behavior.

No verified Dream Course disassembly or independently confirmed cartridge hardware was established in this search. Identify the local ROM and its board, mapping, revision, reset vectors and hardware use before treating framework compatibility as proven. Do not confuse Kirby's Dream Land or Kirby Super Star material with Dream Course.

## Provenance and limits

The cited Super Mario World and Super Metroid projects state PolyForm Noncommercial 1.0.0 licenses. DKC1 states that its own host and tools are MIT but its framework and disassembly-derived metadata have separate terms. Inspect the exact selected framework revision and all transitive notices before copying, linking or distributing anything; root project licenses do not override dependencies. These projects normally require a user-supplied matching ROM, and Super Metroid and DKC1 explicitly keep generated ROM-derived C out of their source trees. [SMW](https://github.com/mstan/SuperMarioWorldRecomp), [Super Metroid](https://github.com/mstan/SuperMetroidRecomp), [DKC1](https://github.com/elliotttate/DKC1Recomp)
