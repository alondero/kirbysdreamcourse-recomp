# Hole ID reference

These decimal IDs index the supported USA ROM map tables. Course and hole numbers are one-based; use the selector with `tools/play_experimental.ps1 -DevHole`. The table is descriptive metadata, not extracted game data.

The four attract demo selectors reuse IDs 0, 42, 44 and 77. Demo Play IDs 160-179 are separate layouts: the numbering follows [map ID research](https://wikiwiki.jp/taslabo/KirbysDreamCourseTricks) and the 8/8/4 layout in the [map archive](https://vgmaps.de/maps/snes/kirbys-dream-course). Multiplayer course names follow the [game manual](https://db.hfsplay.fr/files/2018/10/06/542dfe54-c533-43ba-a787-c3464c5db484.pdf#page=21). These external sources are identification evidence, not proof of behavior for every slot.

IDs 180-191 and 224-255 do not have an established ordinary course assignment. Some are incomplete or repeated. ID 184 is used by the opening sequence, but the normal loader does not recreate the cinematic. A raw ID uses a one-player context and does not establish that the map is playable.

## Create a local screenshot gallery

After `tools/setup.ps1`, run:

```powershell
python tools/hole_index.py
```

Open `local/hole-gallery/index.md`. The tool verifies the ROM SHA-256, uses fresh isolated saves and captures frame 599 of a zero-input start. Screenshots show the initial viewport; larger holes extend beyond it. This does not reproduce Demo Play recordings or cinematic setup. For a subset or another executable:

```powershell
python tools/hole_index.py --ids 0,42,160-179 --output local/hole-gallery-demo
python tools/hole_index.py --executable local/guarded-build/KirbyDreamCourseRecomp.exe --jobs 4
```

Output must be a new directory inside `local/`. The local gallery, screenshots, ROM and generated game-derived output must not be committed. Each capture checks the reported map ID and final WRAM; a failure is marked in the gallery and produces a nonzero exit code. See [developer starts](dev-start.md) for the loader and its limits.

## Single-player normal

| ID | Correspondence | Selector |
| ---: | --- | --- |
| 0 | Course 1, hole 1; attract demo 1 | `normal:1:1` |
| 1 | Course 1, hole 2 | `normal:1:2` |
| 2 | Course 1, hole 3 | `normal:1:3` |
| 3 | Course 1, hole 4 | `normal:1:4` |
| 4 | Course 1, hole 5 | `normal:1:5` |
| 5 | Course 1, hole 6 | `normal:1:6` |
| 6 | Course 1, hole 7 | `normal:1:7` |
| 7 | Course 1, hole 8 | `normal:1:8` |
| 8 | Course 2, hole 1 | `normal:2:1` |
| 9 | Course 2, hole 2 | `normal:2:2` |
| 10 | Course 2, hole 3 | `normal:2:3` |
| 11 | Course 2, hole 4 | `normal:2:4` |
| 12 | Course 2, hole 5 | `normal:2:5` |
| 13 | Course 2, hole 6 | `normal:2:6` |
| 14 | Course 2, hole 7 | `normal:2:7` |
| 15 | Course 2, hole 8 | `normal:2:8` |
| 16 | Course 3, hole 1 | `normal:3:1` |
| 17 | Course 3, hole 2 | `normal:3:2` |
| 18 | Course 3, hole 3 | `normal:3:3` |
| 19 | Course 3, hole 4 | `normal:3:4` |
| 20 | Course 3, hole 5 | `normal:3:5` |
| 21 | Course 3, hole 6 | `normal:3:6` |
| 22 | Course 3, hole 7 | `normal:3:7` |
| 23 | Course 3, hole 8 | `normal:3:8` |
| 24 | Course 4, hole 1 | `normal:4:1` |
| 25 | Course 4, hole 2 | `normal:4:2` |
| 26 | Course 4, hole 3 | `normal:4:3` |
| 27 | Course 4, hole 4 | `normal:4:4` |
| 28 | Course 4, hole 5 | `normal:4:5` |
| 29 | Course 4, hole 6 | `normal:4:6` |
| 30 | Course 4, hole 7 | `normal:4:7` |
| 31 | Course 4, hole 8 | `normal:4:8` |
| 32 | Course 5, hole 1 | `normal:5:1` |
| 33 | Course 5, hole 2 | `normal:5:2` |
| 34 | Course 5, hole 3 | `normal:5:3` |
| 35 | Course 5, hole 4 | `normal:5:4` |
| 36 | Course 5, hole 5 | `normal:5:5` |
| 37 | Course 5, hole 6 | `normal:5:6` |
| 38 | Course 5, hole 7 | `normal:5:7` |
| 39 | Course 5, hole 8 | `normal:5:8` |
| 40 | Course 6, hole 1 | `normal:6:1` |
| 41 | Course 6, hole 2 | `normal:6:2` |
| 42 | Course 6, hole 3; attract demo 2 | `normal:6:3` |
| 43 | Course 6, hole 4 | `normal:6:4` |
| 44 | Course 6, hole 5; attract demo 3 | `normal:6:5` |
| 45 | Course 6, hole 6 | `normal:6:6` |
| 46 | Course 6, hole 7 | `normal:6:7` |
| 47 | Course 6, hole 8 | `normal:6:8` |
| 48 | Course 7, hole 1 | `normal:7:1` |
| 49 | Course 7, hole 2 | `normal:7:2` |
| 50 | Course 7, hole 3 | `normal:7:3` |
| 51 | Course 7, hole 4 | `normal:7:4` |
| 52 | Course 7, hole 5 | `normal:7:5` |
| 53 | Course 7, hole 6 | `normal:7:6` |
| 54 | Course 7, hole 7 | `normal:7:7` |
| 55 | Course 7, hole 8 | `normal:7:8` |
| 56 | Course 8, hole 1 | `normal:8:1` |
| 57 | Course 8, hole 2 | `normal:8:2` |
| 58 | Course 8, hole 3 | `normal:8:3` |
| 59 | Course 8, hole 4 | `normal:8:4` |
| 60 | Course 8, hole 5 | `normal:8:5` |
| 61 | Course 8, hole 6 | `normal:8:6` |
| 62 | Course 8, hole 7 | `normal:8:7` |
| 63 | Course 8, hole 8 | `normal:8:8` |

## Single-player extra

| ID | Correspondence | Selector |
| ---: | --- | --- |
| 64 | Extra course 1, hole 1 | `extra:1:1` |
| 65 | Extra course 1, hole 2 | `extra:1:2` |
| 66 | Extra course 1, hole 3 | `extra:1:3` |
| 67 | Extra course 1, hole 4 | `extra:1:4` |
| 68 | Extra course 1, hole 5 | `extra:1:5` |
| 69 | Extra course 1, hole 6 | `extra:1:6` |
| 70 | Extra course 1, hole 7 | `extra:1:7` |
| 71 | Extra course 1, hole 8 | `extra:1:8` |
| 72 | Extra course 2, hole 1 | `extra:2:1` |
| 73 | Extra course 2, hole 2 | `extra:2:2` |
| 74 | Extra course 2, hole 3 | `extra:2:3` |
| 75 | Extra course 2, hole 4 | `extra:2:4` |
| 76 | Extra course 2, hole 5 | `extra:2:5` |
| 77 | Extra course 2, hole 6; attract demo 4 | `extra:2:6` |
| 78 | Extra course 2, hole 7 | `extra:2:7` |
| 79 | Extra course 2, hole 8 | `extra:2:8` |
| 80 | Extra course 3, hole 1 | `extra:3:1` |
| 81 | Extra course 3, hole 2 | `extra:3:2` |
| 82 | Extra course 3, hole 3 | `extra:3:3` |
| 83 | Extra course 3, hole 4 | `extra:3:4` |
| 84 | Extra course 3, hole 5 | `extra:3:5` |
| 85 | Extra course 3, hole 6 | `extra:3:6` |
| 86 | Extra course 3, hole 7 | `extra:3:7` |
| 87 | Extra course 3, hole 8 | `extra:3:8` |
| 88 | Extra course 4, hole 1 | `extra:4:1` |
| 89 | Extra course 4, hole 2 | `extra:4:2` |
| 90 | Extra course 4, hole 3 | `extra:4:3` |
| 91 | Extra course 4, hole 4 | `extra:4:4` |
| 92 | Extra course 4, hole 5 | `extra:4:5` |
| 93 | Extra course 4, hole 6 | `extra:4:6` |
| 94 | Extra course 4, hole 7 | `extra:4:7` |
| 95 | Extra course 4, hole 8 | `extra:4:8` |
| 96 | Extra course 5, hole 1 | `extra:5:1` |
| 97 | Extra course 5, hole 2 | `extra:5:2` |
| 98 | Extra course 5, hole 3 | `extra:5:3` |
| 99 | Extra course 5, hole 4 | `extra:5:4` |
| 100 | Extra course 5, hole 5 | `extra:5:5` |
| 101 | Extra course 5, hole 6 | `extra:5:6` |
| 102 | Extra course 5, hole 7 | `extra:5:7` |
| 103 | Extra course 5, hole 8 | `extra:5:8` |
| 104 | Extra course 6, hole 1 | `extra:6:1` |
| 105 | Extra course 6, hole 2 | `extra:6:2` |
| 106 | Extra course 6, hole 3 | `extra:6:3` |
| 107 | Extra course 6, hole 4 | `extra:6:4` |
| 108 | Extra course 6, hole 5 | `extra:6:5` |
| 109 | Extra course 6, hole 6 | `extra:6:6` |
| 110 | Extra course 6, hole 7 | `extra:6:7` |
| 111 | Extra course 6, hole 8 | `extra:6:8` |
| 112 | Extra course 7, hole 1 | `extra:7:1` |
| 113 | Extra course 7, hole 2 | `extra:7:2` |
| 114 | Extra course 7, hole 3 | `extra:7:3` |
| 115 | Extra course 7, hole 4 | `extra:7:4` |
| 116 | Extra course 7, hole 5 | `extra:7:5` |
| 117 | Extra course 7, hole 6 | `extra:7:6` |
| 118 | Extra course 7, hole 7 | `extra:7:7` |
| 119 | Extra course 7, hole 8 | `extra:7:8` |
| 120 | Extra course 8, hole 1 | `extra:8:1` |
| 121 | Extra course 8, hole 2 | `extra:8:2` |
| 122 | Extra course 8, hole 3 | `extra:8:3` |
| 123 | Extra course 8, hole 4 | `extra:8:4` |
| 124 | Extra course 8, hole 5 | `extra:8:5` |
| 125 | Extra course 8, hole 6 | `extra:8:6` |
| 126 | Extra course 8, hole 7 | `extra:8:7` |
| 127 | Extra course 8, hole 8 | `extra:8:8` |

## Multiplayer normal

| ID | Correspondence | Selector |
| ---: | --- | --- |
| 128 | Whispy Woods, hole 1 | `multi:1:1` |
| 129 | Whispy Woods, hole 2 | `multi:1:2` |
| 130 | Whispy Woods, hole 3 | `multi:1:3` |
| 131 | Whispy Woods, hole 4 | `multi:1:4` |
| 132 | Whispy Woods, hole 5 | `multi:1:5` |
| 133 | Whispy Woods, hole 6 | `multi:1:6` |
| 134 | Whispy Woods, hole 7 | `multi:1:7` |
| 135 | Whispy Woods, hole 8 | `multi:1:8` |
| 136 | Mr. Shine & Mr. Bright, hole 1 | `multi:2:1` |
| 137 | Mr. Shine & Mr. Bright, hole 2 | `multi:2:2` |
| 138 | Mr. Shine & Mr. Bright, hole 3 | `multi:2:3` |
| 139 | Mr. Shine & Mr. Bright, hole 4 | `multi:2:4` |
| 140 | Mr. Shine & Mr. Bright, hole 5 | `multi:2:5` |
| 141 | Mr. Shine & Mr. Bright, hole 6 | `multi:2:6` |
| 142 | Mr. Shine & Mr. Bright, hole 7 | `multi:2:7` |
| 143 | Mr. Shine & Mr. Bright, hole 8 | `multi:2:8` |
| 144 | Kracko, hole 1 | `multi:3:1` |
| 145 | Kracko, hole 2 | `multi:3:2` |
| 146 | Kracko, hole 3 | `multi:3:3` |
| 147 | Kracko, hole 4 | `multi:3:4` |
| 148 | Kracko, hole 5 | `multi:3:5` |
| 149 | Kracko, hole 6 | `multi:3:6` |
| 150 | Kracko, hole 7 | `multi:3:7` |
| 151 | Kracko, hole 8 | `multi:3:8` |
| 152 | Gordo, hole 1 | `multi:4:1` |
| 153 | Gordo, hole 2 | `multi:4:2` |
| 154 | Gordo, hole 3 | `multi:4:3` |
| 155 | Gordo, hole 4 | `multi:4:4` |
| 156 | Gordo, hole 5 | `multi:4:5` |
| 157 | Gordo, hole 6 | `multi:4:6` |
| 158 | Gordo, hole 7 | `multi:4:7` |
| 159 | Gordo, hole 8 | `multi:4:8` |

## Demo Play and other raw slots

| ID | Correspondence | Selector |
| ---: | --- | --- |
| 160 | Demo Play course 1, hole 1 | `map:160` |
| 161 | Demo Play course 1, hole 2 | `map:161` |
| 162 | Demo Play course 1, hole 3 | `map:162` |
| 163 | Demo Play course 1, hole 4 | `map:163` |
| 164 | Demo Play course 1, hole 5 | `map:164` |
| 165 | Demo Play course 1, hole 6 | `map:165` |
| 166 | Demo Play course 1, hole 7 | `map:166` |
| 167 | Demo Play course 1, hole 8 | `map:167` |
| 168 | Demo Play course 2, hole 1 | `map:168` |
| 169 | Demo Play course 2, hole 2 | `map:169` |
| 170 | Demo Play course 2, hole 3 | `map:170` |
| 171 | Demo Play course 2, hole 4 | `map:171` |
| 172 | Demo Play course 2, hole 5 | `map:172` |
| 173 | Demo Play course 2, hole 6 | `map:173` |
| 174 | Demo Play course 2, hole 7 | `map:174` |
| 175 | Demo Play course 2, hole 8 | `map:175` |
| 176 | Demo Play course 3, hole 1 | `map:176` |
| 177 | Demo Play course 3, hole 2 | `map:177` |
| 178 | Demo Play course 3, hole 3 | `map:178` |
| 179 | Demo Play course 3, hole 4 | `map:179` |
| 180 | Unclassified raw slot | `map:180` |
| 181 | Unclassified raw slot | `map:181` |
| 182 | Unclassified raw slot | `map:182` |
| 183 | Unclassified raw slot | `map:183` |
| 184 | Unclassified raw slot; opening-sequence map data | `map:184` |
| 185 | Unclassified raw slot | `map:185` |
| 186 | Unclassified raw slot | `map:186` |
| 187 | Unclassified raw slot | `map:187` |
| 188 | Unclassified raw slot | `map:188` |
| 189 | Unclassified raw slot | `map:189` |
| 190 | Unclassified raw slot | `map:190` |
| 191 | Unclassified raw slot | `map:191` |

## Multiplayer extra

| ID | Correspondence | Selector |
| ---: | --- | --- |
| 192 | Extra Whispy Woods, hole 1 | `multi-extra:1:1` |
| 193 | Extra Whispy Woods, hole 2 | `multi-extra:1:2` |
| 194 | Extra Whispy Woods, hole 3 | `multi-extra:1:3` |
| 195 | Extra Whispy Woods, hole 4 | `multi-extra:1:4` |
| 196 | Extra Whispy Woods, hole 5 | `multi-extra:1:5` |
| 197 | Extra Whispy Woods, hole 6 | `multi-extra:1:6` |
| 198 | Extra Whispy Woods, hole 7 | `multi-extra:1:7` |
| 199 | Extra Whispy Woods, hole 8 | `multi-extra:1:8` |
| 200 | Extra Mr. Shine & Mr. Bright, hole 1 | `multi-extra:2:1` |
| 201 | Extra Mr. Shine & Mr. Bright, hole 2 | `multi-extra:2:2` |
| 202 | Extra Mr. Shine & Mr. Bright, hole 3 | `multi-extra:2:3` |
| 203 | Extra Mr. Shine & Mr. Bright, hole 4 | `multi-extra:2:4` |
| 204 | Extra Mr. Shine & Mr. Bright, hole 5 | `multi-extra:2:5` |
| 205 | Extra Mr. Shine & Mr. Bright, hole 6 | `multi-extra:2:6` |
| 206 | Extra Mr. Shine & Mr. Bright, hole 7 | `multi-extra:2:7` |
| 207 | Extra Mr. Shine & Mr. Bright, hole 8 | `multi-extra:2:8` |
| 208 | Extra Kracko, hole 1 | `multi-extra:3:1` |
| 209 | Extra Kracko, hole 2 | `multi-extra:3:2` |
| 210 | Extra Kracko, hole 3 | `multi-extra:3:3` |
| 211 | Extra Kracko, hole 4 | `multi-extra:3:4` |
| 212 | Extra Kracko, hole 5 | `multi-extra:3:5` |
| 213 | Extra Kracko, hole 6 | `multi-extra:3:6` |
| 214 | Extra Kracko, hole 7 | `multi-extra:3:7` |
| 215 | Extra Kracko, hole 8 | `multi-extra:3:8` |
| 216 | Extra Gordo, hole 1 | `multi-extra:4:1` |
| 217 | Extra Gordo, hole 2 | `multi-extra:4:2` |
| 218 | Extra Gordo, hole 3 | `multi-extra:4:3` |
| 219 | Extra Gordo, hole 4 | `multi-extra:4:4` |
| 220 | Extra Gordo, hole 5 | `multi-extra:4:5` |
| 221 | Extra Gordo, hole 6 | `multi-extra:4:6` |
| 222 | Extra Gordo, hole 7 | `multi-extra:4:7` |
| 223 | Extra Gordo, hole 8 | `multi-extra:4:8` |

## Other raw slots

| ID | Correspondence | Selector |
| ---: | --- | --- |
| 224 | Unclassified raw slot | `map:224` |
| 225 | Unclassified raw slot | `map:225` |
| 226 | Unclassified raw slot | `map:226` |
| 227 | Unclassified raw slot | `map:227` |
| 228 | Unclassified raw slot | `map:228` |
| 229 | Unclassified raw slot | `map:229` |
| 230 | Unclassified raw slot | `map:230` |
| 231 | Unclassified raw slot | `map:231` |
| 232 | Unclassified raw slot | `map:232` |
| 233 | Unclassified raw slot | `map:233` |
| 234 | Unclassified raw slot | `map:234` |
| 235 | Unclassified raw slot | `map:235` |
| 236 | Unclassified raw slot | `map:236` |
| 237 | Unclassified raw slot | `map:237` |
| 238 | Unclassified raw slot | `map:238` |
| 239 | Unclassified raw slot | `map:239` |
| 240 | Unclassified raw slot | `map:240` |
| 241 | Unclassified raw slot | `map:241` |
| 242 | Unclassified raw slot | `map:242` |
| 243 | Unclassified raw slot | `map:243` |
| 244 | Unclassified raw slot | `map:244` |
| 245 | Unclassified raw slot | `map:245` |
| 246 | Unclassified raw slot | `map:246` |
| 247 | Unclassified raw slot | `map:247` |
| 248 | Unclassified raw slot | `map:248` |
| 249 | Unclassified raw slot | `map:249` |
| 250 | Unclassified raw slot | `map:250` |
| 251 | Unclassified raw slot | `map:251` |
| 252 | Unclassified raw slot | `map:252` |
| 253 | Unclassified raw slot | `map:253` |
| 254 | Unclassified raw slot | `map:254` |
| 255 | Unclassified raw slot | `map:255` |
