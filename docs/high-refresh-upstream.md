# Higher-refresh presentation: upstream evidence

Source inspection for [issue #34](https://github.com/alondero/kirbysdreamcourse-recomp/issues/34), 2026-11-07. This note records source evidence, not a runtime or visual qualification. No upstream implementation was copied into Kirby.

## Reusable framework support

Kirby's tested SNESRecomp pin is `147b55e252dcb3e7680a99c7c2f50e7183891844`. Inspection used `git show HEAD:<path>` in the local checkout after verifying HEAD, so local patches cannot silently change the evidence below.

- `SnesDesktopHostGame.presentation_hz` already separates presentation from simulation. Zero retains one presentation per simulation frame; a positive rate enables repeated presentations with fractional `draw_frame` alpha. `begin_sim_frame`/`end_sim_frame` provide capture boundaries, and `on_reset` tells presenters to discard history after timeline jumps. [Host interface](https://github.com/RetroPortingToolKit/snesrecomp/blob/147b55e252dcb3e7680a99c7c2f50e7183891844/runner/src/desktop/host_main.h)
- `CaptureSimulationFrame` owns the native raster pass, including guest raster IRQ/HDMA work. Presentation-only iterations reuse the captured field. Calling the guest raster again to obtain an intermediate image would violate that separation. [Host implementation](https://github.com/RetroPortingToolKit/snesrecomp/blob/147b55e252dcb3e7680a99c7c2f50e7183891844/runner/src/desktop/host_main.c)
- The shared clock has independent simulation and presentation deadlines. Its NTSC constant is `60.098811862` Hz; fixed presentation choices are 60, 90, 120, 144, 165, 240 and 360 Hz, with zero selecting display refresh. Alpha is clamped to [0, 1]. These are scheduling facilities, not object matching or image interpolation. [Clock header](https://github.com/RetroPortingToolKit/snesrecomp/blob/147b55e252dcb3e7680a99c7c2f50e7183891844/runner/src/desktop/host_clock.h), [implementation](https://github.com/RetroPortingToolKit/snesrecomp/blob/147b55e252dcb3e7680a99c7c2f50e7183891844/runner/src/desktop/host_clock.c)

## What Super Metroid actually supplies

The inspected SuperMetroidRecomp revision is `2c916b8166b805b1f6dcde099e14549755b5c633`, whose Git tree pins its `snesrecomp` submodule to `6ca2dbbe12f65901ae76015a15943c8254c4a85f`. It therefore is not evidence that every renderer helper exists in Kirby's older framework pin. [Pinned tree](https://github.com/mstan/SuperMetroidRecomp/tree/2c916b8166b805b1f6dcde099e14549755b5c633)

Presentation FPS is a separately enabled setting, defaulting off. Its host returns zero unless FPS is enabled and the custom renderer is available. The renderer captures on simulation boundaries and composes from those captures for each presentation. [Settings](https://github.com/mstan/SuperMetroidRecomp/blob/2c916b8166b805b1f6dcde099e14549755b5c633/src/sm_video.c), [host hooks](https://github.com/mstan/SuperMetroidRecomp/blob/2c916b8166b805b1f6dcde099e14549755b5c633/src/main.c)

The game-specific renderer retains two immutable source frames with per-line registers, palette, OAM and VRAM, plus separate object-owner and terrain WRAM snapshots. `continuous_frames` requires adjacent frame numbers, matching room/game state and bounded camera motion. Samus-piece matching verifies the actual ROM map rather than merely matching an OAM slot or nearest screen position. Invalid continuity removes the previous frame from interpolation. This is substantial game-specific reconstruction, not a transferable FPS switch. [Renderer implementation](https://github.com/mstan/SuperMetroidRecomp/blob/2c916b8166b805b1f6dcde099e14549755b5c633/src/sm_renderer.c)

Both inspected repositories carry PolyForm Noncommercial 1.0.0 licenses. This inspection introduces no dependency or copied implementation and does not change Kirby's existing distribution constraints. [Framework license](https://github.com/RetroPortingToolKit/snesrecomp/blob/147b55e252dcb3e7680a99c7c2f50e7183891844/LICENSE), [Super Metroid license](https://github.com/mstan/SuperMetroidRecomp/blob/2c916b8166b805b1f6dcde099e14549755b5c633/LICENSE)

## Implications for Kirby

At the starting revision, [main.c](../src/main.c) supplies the orbit draw hook but no presentation-rate hook; [course_orbit.c](../src/course_orbit.c) ignores alpha and reads the current scene. [course_sprites.h](../src/course_sprites.h) exposes world coordinates, graphics attributes, buckets and producer PCs, but no persistent actor generation identity. A producer PC can emit multiple objects, so it is not by itself a safe identity across frames.

Inference: the existing clock can support a bounded orbit-camera experiment without a framework upgrade. General moving-object interpolation needs explicit continuity evidence and a retained previous scene; framebuffer blending cannot establish that identity. Any such experiment should keep the native default, use the same camera transform for terrain and world billboards, reject ambiguous transitions, and separately validate state equality, default pixels and actual presented motion. This source inspection alone establishes none of those runtime results.
