# Compatibility and evidence

This project uses the RetroPortingToolKit SNESRecomp revision `147b55e252dcb3e7680a99c7c2f50e7183891844`, the USA ROM identity in `rom_identity.txt`, and Snes9x 1.63 revision `fae2fea08f74180759ef540ee94259213f503480` as its comparison reference. The tested Snes9x libretro DLL SHA-256 is `72e0f16839c0298d8ce52eb2281e83feaede49af73936a39bb782b0f488e92e0`.

Recorded native and interpreter-fallback runs completed the first two single-player courses, with final save images matching the reference for those recordings. A 17,000-frame two-controller recording completed the first two-player hole and reached the next hole in both host modes and the reference. The reference runs reproduced their frame records; the host modes reproduced each other's CPU logs and final save, which matched the reference save. At the final checkpoint, host and reference differed in 422 WRAM bytes. These checks cover recorded inputs and checkpoints, not every shot, save, or mode.

The first full-WRAM boot mismatch occurs at host frame 15 versus reference frame 16. Later selected frames differ too. Audio is not sample-exact; DMA, APU scheduling, reset, save-state compatibility and rendering in all situations are not fully qualified. The reference has timing approximations and is not a hardware oracle. Native blocks are guarded and the interpreter remains available.

Generated source and runtime outputs are generated locally from the user's ROM and must not be committed or distributed. The project is Windows-oriented; other platforms have not been qualified.
