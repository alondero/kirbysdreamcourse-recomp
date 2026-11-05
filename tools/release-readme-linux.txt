Kirby's Dream Course Recomp - experimental Linux x86-64 build

1. Extract the ZIP into its own writable directory.
2. Put your headerless Kirby's Dream Course (USA).sfc beside
   KirbyDreamCourseRecomp.
3. Run ./start.sh. If your extractor discarded executable permissions, run:
   chmod +x start.sh KirbyDreamCourseRecomp
   ./start.sh
Keep assets/ and licenses/ in place. No compiler or generation is required.

Built on Ubuntu 24.04; requires glibc 2.39 or newer, libstdc++6 and system
OpenGL libraries. SDL3 is linked statically. build-info.json records the
actual required glibc symbol version and linked libraries. Steam Deck uses
its system graphics/audio drivers; no Proton or SteamOS modifications needed.
For a non-Steam shortcut select start.sh and set Start In to this directory.

Supported ROM: 1,048,576 bytes, SHA-256
0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b
The ROM is not included. Settings and saves live beside the executable;
back them up before upgrading. Save-state compatibility is not qualified.

Keyboard: arrows, X for A, Z for B, Return for Start, Right Shift for Select.
Configure controllers/player two in the launcher. Ctrl+L or Select+L3 opens
settings during play. Alt+Return toggles fullscreen.

The executable uses the guarded timing/native-dispatch defaults. To select
interpreter fallback: SNESRECOMP_LLE_BOUNCE=0 ./start.sh

Experimental: reference state/audio differences and untested paths remain.
See the release notes and tagged docs/compatibility.md. Report the version
from build-info.json, mode, settings and course/hole. Do not attach ROMs,
saves or game-derived captures to public reports.

Runtime/project licensing: LICENSE and THIRD_PARTY_ATTRIBUTION.md.
Dependency notices: licenses/. Framework: PolyForm Noncommercial 1.0.0.
Project: https://github.com/alondero/kirbysdreamcourse-recomp
