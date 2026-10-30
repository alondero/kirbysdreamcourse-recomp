# Project instructions

Read [architecture.md](architecture.md) before implementation or changes to recompilation strategy; it records the proposed design, evidence gates and current unknowns. Read [docs/research.md](docs/research.md) when evaluating frameworks, prior projects or source claims.

The target is the locally supplied Kirby's Dream Course (USA) ROM. Verify the exact hash before ROM-dependent work. Treat ROMs, extracted assets, generated game-derived output, saves and recordings as local inputs; keep them out of commits and releases. Do not edit generated code: change its source configuration or generator and regenerate.

For behavior changes, reproduce with deterministic input and compare against a pinned reference emulator where practical. Report the first divergence and untested paths. Keep hardware behavior separate from game-specific assumptions and preserve the interpreter fallback until evidence supports translation.

The architecture is a plan, not proof that dependencies or commands work. Inspect the chosen upstream revision and licenses before integration; record exact pins and verified build/test commands when established.
