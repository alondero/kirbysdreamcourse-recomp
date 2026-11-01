# Compatibility and evidence

This project uses the RetroPortingToolKit SNESRecomp revision `147b55e252dcb3e7680a99c7c2f50e7183891844`, the USA ROM identity in `rom_identity.txt`, and Snes9x 1.63 revision `fae2fea08f74180759ef540ee94259213f503480` as its comparison reference. The tested Snes9x libretro DLL SHA-256 is `72e0f16839c0298d8ce52eb2281e83feaede49af73936a39bb782b0f488e92e0`.

Recorded native and interpreter-fallback runs completed the first two single-player courses, with final save images matching the reference for those recordings. A 17,000-frame two-controller recording completed the first two-player hole and reached the next hole in both host modes and the reference. The reference runs reproduced their frame records; the host modes reproduced each other's CPU logs and final save, which matched the reference save. At the final checkpoint, host and reference differed in 422 WRAM bytes. These checks cover recorded inputs and checkpoints, not every shot, save, or mode.

The first full-WRAM boot mismatch occurs at host frame 15 versus reference frame 16. Later selected frames differ too. Audio is not sample-exact; DMA, APU scheduling, reset, save-state compatibility and rendering in all situations are not fully qualified. The reference has timing approximations and is not a hardware oracle. Native blocks are guarded and the interpreter remains available.

## Audio baseline

Focused instruction, timer, port-ordering and DSP checks, including an opt-in
queue-boundary candidate, are documented in [APU accuracy](apu-accuracy.md).
The candidate has its own snapshot identity and is not enabled by ordinary setup.

With zero SRAM and no input, two paced 400-frame guarded native runs produced identical DSP-ring captures, and two Snes9x captures produced identical audio over the same interval. The first 180 frames were silent, so silence alone is not an audio accuracy test. The first nonzero stereo sample appeared at host sample 156932 and reference sample 157388. Aligning those onsets gives an observational offset of 456 samples; the first difference after that alignment is host sample 156991, `(4722, 4781)`, versus reference sample 157447, `(4723, 4782)`. The host capture skips the first DSP capture interval, while the reference captures libretro output, so this offset is not a measurement of hardware latency. The comparison does not isolate DSP arithmetic from scheduling, resampling or other emulator differences.

For this baseline, run `tools/setup.ps1 -CMake 'C:/path/to/native/cmake.exe'` to build the guarded configuration, set the four timing and native-dispatch environment variables from `tools/play_experimental.ps1`, and set `SNESRECOMP_DSPOUT` to a fresh absolute path under `local/`. Run `tools/headless.ps1` for 400 frames without `-Unpaced`, twice with separate output and capture paths. Run `tools/reference_capture.py` twice for 400 frames with `--audio-raw`, the pinned core, and no input. Both tools check the ROM identity; the reference tool also checks the core hash. Captures contain game-derived audio and remain local. This baseline does not test port ordering, individual SPC instruction or timer phases, gameplay input, fallback mode, or physical hardware.

Use `python tools/compare_audio.py HOST_PCM REFERENCE_PCM --offset 456 --allow-partial` to reproduce the diagnostic comparison. The offset is explicit and may differ in other scenarios. The tool reports the overlap and first differing stereo sample; it does not resample, infer alignment, or certify timing. Compare repeated captures at offset zero without `--allow-partial` to require equal lengths. A partial comparison cannot prove either capture is complete.

Generated source and runtime outputs are generated locally from the user's ROM and must not be committed or distributed. The project is Windows-oriented; other platforms have not been qualified.

## Reset, state and persistence evidence

The pinned reference was run twice for 240 frames with zero initial SRAM and no input. A state captured after frame 80 was restored after frame 240 and frames 81–240 were replayed, including `retro_reset` immediately before frame 120. Each replayed frame matched its original reference video, audio, WRAM and SRAM hashes; the two complete captures also matched. Run this local ROM-dependent check with `$env:KDC_TEST_REFERENCE='1'; python -m unittest discover -s tests -p test_reference_state.py -v`. The test requires the ROM at the repository root and the pinned DLL at `local/reference/snes9x_libretro.dll`; both identities are checked by the capture tool. The reference snapshot stays in memory.

As a separate reference-only probe, the 8192-byte synthetic pattern `bytes((i * 73 + 19) & 255 for i in range(8192))` was loaded at startup. The game changed that image before frame 119; its SRAM at frames 119 and 120 (after reset immediately before frame 120) had the same SHA-256, `03f8e7ce39064f7f49f4bb6d16267fba50b9ce096f8e9373bd58d027bea00056`. This does not establish that arbitrary images survive the game's boot initialization or that other reset times preserve them.

These checks qualify only the reference's observed behavior for this short scenario. They do not establish host reset or host save-state round trips, rewind, cross-build state compatibility, or fresh-process SRAM persistence. Snapshot identities in `src/game_rtl.c` distinguish experimental layouts; an identity match is not evidence of compatibility across builds, versions, or runtime settings. Keep snapshots and SRAM local, and do not rely on them across builds. `compare_wram.py` rejects reference reset scenarios and recorded host runs starting from a state because it cannot establish their matching timeline and initial conditions.
