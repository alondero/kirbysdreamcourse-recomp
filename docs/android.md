# Android

Install `kirbysdreamcourse-recomp-android-arm64.apk` on an ARM64 device running Android 9 (API 28) or newer. Open **Choose ROM** and select your headerless, 1 MiB Kirby's Dream Course (USA) image in the Android document picker. The app verifies the SHA-256 in `rom_identity.txt` before replacing its private copy. On subsequent launches, tap **Play**.

The shared launcher opens in landscape before the game starts. Use **Settings** for display and audio, the player cards for controller configuration, and **Mods** for Kirby's presentation options. Tap **Play** to start the game. **Skip launcher on boot** goes directly to gameplay on subsequent launches. **Settings** on the ROM screen always opens the startup launcher.

During play, tap **Menu** in the upper-right corner or use Android **Back**. A controller can open the menu with **Select+L3** (Back plus a left-stick click by default) or **Guide/Home** when Android passes that button to the app. Swipe vertically to scroll the menu. **Resume** or Android **Back** applies changes and resumes play. **Quit Game** returns to the ROM screen. Renderer and audio sample rate are available in the startup launcher, as on PC.

Slide your left thumb around the D-pad for eight-way movement. The right-hand diamond has X above, B below, Y on the left and A on the right. You can hold a direction and a face button together. L and R sit above the two clusters, with Select and Start at the bottom center. The on-screen SNES controls hide when Android detects a gamepad or joystick and return when the last one disconnects. The **Menu** button stays available during play.

The host stores configuration and saves in the app-specific external files directory, including `saves/save.srm`. Uninstalling the app removes its data. Debug builds use the separate `io.github.alondero.kdc.debug` application ID.

Super Mario All-Stars uses a dedicated on-screen menu button, Android Back and controller shortcuts. DKC2 offers Android settings before gameplay. Kirby uses those entry points with the shared recomp-ui settings on the SDLActivity surface. [Super Mario All-Stars controls](https://github.com/TanookiStudios/SuperMarioAllStarsRecomp/blob/25d73ed4db7033b9177ec116f99f4185e7397a6f/README.md#controls), [DKC2 Android port notes](https://github.com/deivid22srk/DKC2-Android-Port/blob/9a072b05c1c8145637fb883e75cf1767a52bf7ee/docs/PORT_ANDROID.md#real-settings-launchercfg)

## Build locally

Use the host prerequisites from the desktop build, JDK 17 or newer, and the Android SDK with platform 35, build-tools 35.0.0 and NDK 28.2.13676358. Set `ANDROID_HOME` to your SDK directory and `JAVA_HOME` to the JDK. The checked-in Gradle wrapper pins Gradle 8.12 and verifies its distribution checksum. The Android Gradle plugin is pinned to 8.9.2.

```sh
sdkmanager "platforms;android-35" "build-tools;35.0.0" "ndk;28.2.13676358"
python tools/build_android.py --rom "/private/Kirby.sfc" --version v0.0.0-dev
adb install -r local/releases/v0.0.0-dev/kirbysdreamcourse-recomp-android-arm64.apk
```

On Windows use a native Windows CMake, passing `--cmake <path-to-cmake.exe>` if MSYS CMake comes first on PATH. The builder reuses `tools/setup.py` to verify the ROM, apply the guarded framework patches, generate C on the host, and cross-compile for `arm64-v8a`. The APK links the same interpreter fallback, static SDL3 and C++ runtime into `libmain.so`. Gradle compiles SDLActivity from the matching SDL source tree. The packager checks the APK contents against an allowlist and runs `apksigner verify` before staging the result under `local/releases/<version>/`.

## Release signing

For a local release build, pass `--release` and set `ANDROID_KEYSTORE_PATH`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS` and `ANDROID_KEY_PASSWORD`. Preserve the signing key and its recovery information for future updates to `io.github.alondero.kdc`.

The release certificate for `io.github.alondero.kdc` has SHA-256
`17778ffb0a101469d202c21639516ed3987d122904bfa90807b41b81cbad4a89`.
The packager checks the signer against this fingerprint for every release build.
Keep a private backup of the keystore, alias and password together in encrypted storage.

For GitHub Actions, configure `ANDROID_KEYSTORE_BASE64` with the base64-encoded keystore and set the same three password/alias secrets in the Kirby repository. The Linux release job materializes the keystore in the runner's temporary directory for the signing step. A dry run uses the Android debug identity. A publishing run requires the release key and uploads the signed APK alongside the two desktop ZIPs. The release verification step compares all three downloads with the build outputs.

## Device evidence

The shared launcher was tested on an Android 15 emulator using the arm64 debug APK. The startup launcher, touch Menu, Android Back, swipe scrolling, Select+L3, Guide/Home, Resume and Quit Game were exercised. Controller shortcuts used an Android virtual gamepad. A startup sample-rate edit and an in-game volume edit persisted. With **Skip launcher on boot** enabled, **Play** went directly to the game and **Settings** on the ROM screen still opened the launcher.

On the same emulator, a diagnostic run opened the in-game launcher at frame 8 and held it open until Back. Against a no-input run without the launcher, all 32 CPU and APU trace frames, 20 WRAM captures and final SRAM matched. The first WRAM difference against the pinned Snes9x 1.63 reference was host frame 15 / reference frame 16 in both runs. The new launcher still needs testing on physical Android hardware.

On the Redmi K30 Pro, a two-finger input sequence held Up+Left and B, slid to Right while holding B, then released each finger independently. The pressed indicators matched at every step. Start opened the title menu. ADB-created HID gamepads exercised connection during play, launch with a pad attached and removal of two pads in succession. All twelve controls returned after the last disconnect.

A Redmi K30 Pro running Android 16 imported the verified USA ROM through the system picker, showed the title screen in landscape and reached one-player name entry with on-screen controls. Physical gamepads, Android 9, multiplayer, audible output and full-course completion on Android still need device testing.

An Android 16 emulator running the arm64 APK through ARM translation rejected a wrong ROM, resumed from Home, returned to the import screen with Back, started a second session and wrote an 8,192-byte SRAM file on exit. A rejected import preserved the previously verified ROM. A diagnostic build of the same game library recorded 20 consecutive no-input boot WRAM dumps. Two native boots also produced identical 32-frame CPU traces. All 20 WRAM dumps matched the Windows guarded host, a second Android run and Android with forced interpreter fallback. Against the pinned Snes9x 1.63 core, the first mismatch was at the 16th completed frame. The existing host trace numbers that frame 15. See [compatibility](compatibility.md) for the desktop reference alignment and remaining fidelity limits.
