# Project instructions

Read [architecture.md](architecture.md) before implementation or changes to recompilation strategy; it records the proposed design, evidence gates and current unknowns. Read [docs/research.md](docs/research.md) when evaluating frameworks, prior projects or source claims.

The target is the supplied Kirby's Dream Course (USA) ROM. Verify the exact hash before ROM-dependent work. Keep ROMs, extracted game assets, generated game source, saves and recordings out of this public repository and release packages. Release builds may fetch the ROM from a private companion repository using read-only Actions credentials, generate code privately during the build, and distribute the compiled EXE with runtime dependencies and notices. Players must still supply their own ROM at runtime. Never upload raw ROMs or generated source as CI artifacts. Do not edit generated code: change its source configuration or generator and regenerate.

For behavior changes, reproduce with deterministic input and compare against a pinned reference emulator where practical. Report the first divergence and untested paths. Keep hardware behavior separate from game-specific assumptions and preserve the interpreter fallback until evidence supports translation.

The architecture is a plan, not proof that dependencies or commands work. Inspect the chosen upstream revision and licenses before integration; record exact pins and verified build/test commands when established.
