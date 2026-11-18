# Android

Install `kirbysdreamcourse-recomp-android-arm64.apk` on an ARM64 device running Android 9 (API 28) or newer. Open **Choose ROM** and select your headerless, 1 MiB Kirby's Dream Course (USA) image in the Android document picker. The app verifies the SHA-256 in `rom_identity.txt` before replacing its private copy. On subsequent launches, tap **Play**.

The game opens in landscape. The on-screen controls map to the SNES D-pad, A/B/X/Y, L/R, Start and Select. The host also accepts SDL gamepad input. Android Back exits the game. The host stores configuration and saves in the app-specific external files directory, including `saves/save.srm`. Uninstalling the app removes its data. Debug builds use the separate `io.github.alondero.kdc.debug` application ID.

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

A Redmi K30 Pro running Android 16 imported the verified USA ROM through the system picker, showed the title screen in landscape and reached one-player name entry with on-screen controls. Physical gamepads, Android 9, multiplayer, audible output and full-course completion on Android still need device testing.

An Android 16 emulator running the arm64 APK through ARM translation rejected a wrong ROM, resumed from Home, returned to the import screen with Back, started a second session and wrote an 8,192-byte SRAM file on exit. A rejected import preserved the previously verified ROM. A diagnostic build of the same game library recorded 20 consecutive no-input boot WRAM dumps. Two native boots also produced identical 32-frame CPU traces. All 20 WRAM dumps matched the Windows guarded host, a second Android run and Android with forced interpreter fallback. Against the pinned Snes9x 1.63 core, the first mismatch was at the 16th completed frame. The existing host trace numbers that frame 15. See [compatibility](compatibility.md) for the desktop reference alignment and remaining fidelity limits.
