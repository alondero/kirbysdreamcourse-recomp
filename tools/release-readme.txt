Kirby's Dream Course Recomp - experimental Windows x64 build

1. Extract the ZIP into its own writable directory.
2. Put your headerless Kirby's Dream Course (USA).sfc next to
   KirbyDreamCourseRecomp.exe.
3. Double-click KirbyDreamCourseRecomp.exe. Keep assets/ and licenses/ in place.

Supported ROM: 1,048,576 bytes, SHA-256
0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b
The ROM is not included. No compiler or local code generation is required.

Player one defaults to keyboard: arrows, X for A, Z for B, Return for Start,
Right Shift for Select. Configure controllers and player two in the launcher.
Ctrl+L opens settings during play. Alt+Return toggles fullscreen.
Choose PRACTICE on the game's title menu to select a course and hole.
During practice, Start opens resume, restart, hole selection and exit.
Choose a starting ability in the picker. Its music/background keep running.
Finishing the selected hole returns to the picker for replay or another hole.
Practice uses saves/practice; ordinary progress is kept separate.
Save states and rewind are unavailable during practice.
Settings and saves are stored beside the EXE. Back them up before upgrading;
save-state compatibility between builds is not qualified.

The EXE uses the guarded timing/native-dispatch defaults without a script.
To diagnose interpreter fallback, start it from PowerShell with:
  $env:SNESRECOMP_LLE_BOUNCE = '0'
  ./KirbyDreamCourseRecomp.exe

This is experimental: recorded first/second courses and one two-player hole
have been checked, but reference state/audio differences and untested paths
remain. See the release notes and tagged compatibility document for evidence.
Report the version from build-info.json, mode, settings and course/hole.
Do not attach ROMs, saves or game-derived captures to public reports.

Runtime and project licensing: LICENSE and THIRD_PARTY_ATTRIBUTION.md.
Dependency notices: licenses/. The framework uses PolyForm Noncommercial 1.0.0.
Project: https://github.com/alondero/kirbysdreamcourse-recomp
