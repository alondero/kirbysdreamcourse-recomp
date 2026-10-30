# Dream Course inventory research

Research date: 2026-10-28. This is independent catalog and physical-board evidence, not a certification of an individual dump. The project does not redistribute ROM data.

## Identity and cartridge

- [OpenRetro's game database](https://openretro.org/snes/kirbys-dream-course/edit), attributing its entry to a 2021-05-17 No-Intro import, identifies `Kirby's Dream Course (USA).sfc` as 1,048,576 bytes with SHA-1 `e1f29ad02981ec84156b68d4fb95b2182dc8cd82`. Compare that SHA-1 and size to the local file in addition to the project's SHA-256 allowlist. This is a catalog record, not a manufacturer-signed verification; a matching digest does not establish the provenance of the physical dump. OpenRetro also lists a distinct USA/Europe Switch Online image, so do not conflate variants.
- [SNES Central's cartridge survey](https://snescentral.com/article.php?id=0430) reports USA cartridge ROM `SNS-CG-0` on `SHVC-1A3M-30`, battery-backed 64 kbit SRAM, MAD-1 decoding, and no special chip. Its [board entry](https://snescentral.com/pcbboards.php?chip=SHVC-1A3M-30) calls the board LoROM and gives the ROM and save mappings. This is an independent observational source, not Nintendo documentation; sampled boards do not prove all production variants. 64 kbit is 8 KiB.

## Disassembly search

GitHub repository searches on 2026-10-28 for `"kirby" "dream course"` and `"kirby bowl" disassembly` found no identified Dream Course disassembly. This is a bounded search, not proof none exists. [andlabs/65816disasm](https://github.com/andlabs/65816disasm) is a GPL-3.0 disassembler whose README mentions potential invalid vectors or register-size analysis problems for Kirby Bowl. Its published repository contains no Kirby Bowl analysis script and does not specify a ROM revision for that observation. It is a tool and a warning about automated analysis, not a verified Dream Course disassembly or source of reusable symbols. [huderlem/kirbydreamland](https://github.com/huderlem/kirbydreamland) explicitly targets the unrelated Game Boy game; do not use its symbols. No game-specific source or metadata has been imported.
