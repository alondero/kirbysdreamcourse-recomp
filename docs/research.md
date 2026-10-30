# SNES recompilation research

Research snapshot: 2026-10-28. Project descriptions are self-reported; status and dependency revisions change. Links are primary project sources.

## Common architecture

[SNESRecomp](https://github.com/RetroPortingToolKit/snesrecomp) translates 65816 instructions to C and links the result with a shared SNES hardware runtime. Unresolved code can execute in an interpreter. The game project supplies analysis metadata and integration; framework compatibility does not itself make an arbitrary ROM playable. The [Super Mario World project](https://github.com/mstan/SuperMarioWorldRecomp) describes the runtime as LakeSnes-derived with Snes9x-attributed algorithms: the PPU, SPC700/APU, DMA/HDMA, memory mapping and register I/O remain hardware emulation, not native translations of game logic.

| Project | Relevant practice | Source |
| --- | --- | --- |
| Donkey Kong Country | Uses a pinned SNESRecomp fork, a headless validation host and locally generated code; its disassembly-derived metadata has separate provenance. | [DKC1Recomp](https://github.com/elliotttate/DKC1Recomp) |
| Donkey Kong Country 2 | Tracks 65816 processor modes and widths during analysis, resolves proven control flow to C, and falls back for unavailable exact entry states. The project compares deterministic input and machine-state checkpoints, with an emulator as oracle. SDL2 supplies a portable desktop host. | [DKC2 architecture](https://github.com/elliotttate/DKC2Recomp/blob/main/docs/ARCHITECTURE.md), [DKC2 README](https://github.com/elliotttate/DKC2Recomp) |
| Super Mario World | Keeps per-game runtime, analysis configuration, generated C and build glue apart from the shared hardware framework. | [SuperMarioWorldRecomp](https://github.com/mstan/SuperMarioWorldRecomp) |
| Super Metroid | Uses a ROM/interpreter ground truth, exact M/X variants and optional HLE optimizations; generated ROM-derived C is local. Its tested routes do not establish full-game completion. | [SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp) |

A static coverage count does not prove runtime coverage: DKC2 documents a later indirect target that was not covered by its static graph. Track actual fallback and indirect dispatch, and replay real input routes alongside structural analysis. [DKC2 README](https://github.com/elliotttate/DKC2Recomp)

## Starting a new game

The framework offers a [project setup guide](https://github.com/RetroPortingToolKit/snesrecomp/blob/main/docs/GAME_PROJECT_SETUP.md) and ROM probe. Probe mapping and enhancement hardware before choosing a runtime; the [compatibility table](https://github.com/RetroPortingToolKit/snesrecomp#rom-compatibility) distinguishes implemented chips from unsupported or title-gated ones. Probe output is evidence, not validation of this specific game. The [Kirby Super Star project](https://github.com/NVDEMU/KirbySuperStarPC) describes a practical sequence: beam-aligned frame driver, identified symbols and AOT roots, then investigate unresolved dispatches. It is a different game and does not establish Dream Course hardware or behavior.

No verified Dream Course disassembly or independently confirmed cartridge hardware was established in this search. Identify the local ROM and its board, mapping, revision, reset vectors and hardware use before treating framework compatibility as proven. Do not confuse Kirby's Dream Land or Kirby Super Star material with Dream Course.

## Provenance and limits

The cited Super Mario World and Super Metroid projects state PolyForm Noncommercial 1.0.0 licenses. DKC1 states that its own host and tools are MIT but its framework and disassembly-derived metadata have separate terms. Inspect the exact selected framework revision and all transitive notices before copying, linking or distributing anything; root project licenses do not override dependencies. These projects normally require a user-supplied matching ROM, and Super Metroid and DKC1 explicitly keep generated ROM-derived C out of their source trees. [SMW](https://github.com/mstan/SuperMarioWorldRecomp), [Super Metroid](https://github.com/mstan/SuperMetroidRecomp), [DKC1](https://github.com/elliotttate/DKC1Recomp)
