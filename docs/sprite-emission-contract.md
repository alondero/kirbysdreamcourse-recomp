# Sprite queue and layout expansion

Discovery for [#83](https://github.com/alondero/kirbysdreamcourse-recomp/issues/83)
at project commit `141bfbe6972c88b45a4e51b332c0d0cb258806ec`. The audit
baseline was `e18a4f753f54ed8bedf015f7bd634d26f6fc4241`.

Keep the current emission observers. The native queue has persistent writes
and ABI outputs beyond presentation commands. The two paths also differ in
layout commands, allocation and consumption lifetime. Frozen enhanced
commands already differ across native-bounce and interpreter-only
execution in one route. Production emission, entry hooks and fallback stay in place.

## Bounded original-ROM ledger

The headerless 1,048,576-byte USA image matched SHA-256
`0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b`.
Analysis used `decode_insn` in `recompiler/snes65816.py` at framework pin
`147b55e252dcb3e7680a99c7c2f50e7183891844` and followed each branch with
its own M/X state. The literal-transfer search scanned the ROM and checked
candidate instructions and dispatch tables.
Raw matches are candidates until corroborated by reachable control flow.
The scan can be repeated by matching `20/4C` followed by the little-endian
target in the same physical bank, or `22/5C` followed by the target and a
bank that maps the same ROM bytes. Inspect the queue vector as ten
little-endian words at ROM offset `$0B0E`. For the $00/$80 bank, a PC
above $7FFF maps to offset `PC & $7FFF`. Low addresses follow the bus map.

### Enqueue and reset

- `$80:8B02` stages A (layout pointer) and X (screen X) into DP
  `$C0/$C2` and loads A from DB:`$2880` before falling through to
  `$8B09`. At `$8B09` A is the bucket, Y is screen Y, and DP
  `$C0/$C2` are layout pointer/screen X. Stock queue addressing needs
  DB=$7E for the high WRAM tables and a D whose direct-page accesses
  map the intended staging words. The dispatcher requires a native
  16-bit A and index pair and a long-return frame. It indexes the
  same-program-bank table at `$8B0E` by twice A. The ten slots
  lead to distinct blocks `$8B22 + $17 * bucket`, each ending in RTL.
  No call or yield occurs inside a legal bucket arm.
- A bucket starts at `$7E:(2884 + 182 * bucket)` (hex), with layout,
  X and Y planes spaced by $80 and the byte-count word at +$180.
  The arm uses the old byte count as the even index for all three
  word writes in layout/X/Y order, then increments the count by two.
  Sixty-four entries fit the structure. There is no bucket range, queue
  capacity, layout or coordinate test at enqueue. A write at index $80
  already overlaps the next plane, with Y temporarily overwriting
  the count word before the new count is stored. An out-of-range bucket
  uses an unqualified indirect target.
- For legal 16-bit inputs, A returns screen Y, X returns the incremented
  byte count, Y/D/DB are unchanged, carry is clear and N/Z derive from the
  final count. The $8B02 entry additionally writes the DP staging words.
  JSL pushes three native return bytes, JML producers transfer the
  outer JSL's frame, and the common RTL pops three bytes. A $8B09
  stack-derived caller can identify an outer wrapper at a tail call
  rather than the immediate producer.
- `$8823` requires 16-bit register state and initializes the OAM cursor,
  limit and packing state in DB:`$2C–$35` before falling through to
  the RTL-ending `$8841` clear. `$8841` and the separate RTS-ending
  `$8AE1` clear the ten byte-count words without erasing record
  planes or resetting OAM allocation. `$8AE1` explicitly clears the
  accumulator-width flag. The scan found no direct calls to either
  queue-only clear entry and did not rule out computed control flow.

### Queue consumption and preparation

- `$8867` JSRs `$8C08` within the long-call OAM-finalization path
  at `$8866`. The consumer forces 16-bit widths and reads
  DB:`$2882`. If its sign bit is clear it JSRs `$8ED8`, which calls
  `$81:9FC3` and returns to include the updater's newly staged requests.
  `$8C12` is the join, with no direct call found. It saves D, sets D=0
  and reads DB:`$009E`. It sets DB to $89 with the low byte of the
  pushed word $7E89. The $009E accumulator result is overwritten before
  its use as data. Preserve the bus read for a cycle/bus-accurate comparison.
- `$8C12–$8D88` iterates buckets 0 through 9, then ascending even byte
  indexes below each count. It reads the queue from explicit $7E long
  addresses, stages screen X/Y through DP $36/$38, supplies Y as the layout
  pointer, and calls the same `$8D89` expander ten places. $89 supplies
  both cartridge layout reads and low WRAM-mirror reads. Native fetches
  layouts per entry and never zeros headers here. A second raw consumer
  therefore traverses the same headers, subject to the then-current OAM
  allocation state. The wrapper at $8866 additionally rewrites the OAM
  cursor as a count, hides unused entries, and finalizes the high table.
- The consumer returns via RTS at `$8D88` after popping DB=$7E
  from the remaining byte of $7E89 and restoring D. This is an intentional
  observable DB output even when the incoming DB already equals $7E.
  Widths remain 16 bit in native mode. X/A reflect the final bucket cursor,
  Y can reflect the last expansion, carry is set by the final compare,
  and final N/Z come from the restored D. The caller at $8866 saves and
  restores its own P. Save stack writes and non-dead DP scratch in parity
  comparisons. The caller ABI's liveness outside the bounded sequence
  remains unqualified, so the whole outgoing state should be compared.
- `$81:9FC3` uses DB:`$ED07` to choose between two paths. The first
  reads player position, calls `$87:98C9` (which can call `$99DE`
  three times to compare DP coordinate pairs), then sets fields in
  `$0B1E/$0B22/$D962/$D99A/$DA6C/$DA70` and queues actor indices
  0 and 4 via `$80:9EBD`. The other path can adjust
  `$0B1E/$0B20`, invokes the position-ordering helper `$81:A0C7`,
  then can queue actors via $9EBD and attachments via
  `$81:A0B6` directly to $8B09. `$81:A0A0` stages the stored
  attachment layout and origin and clears the $6A98 pending word
  to $FFFF. This prelude can adjust bucket order and submit requests
  before expansion reads layout memory. DP-relative writes depend on D.
- Literal flag writers inspected set DB:`$2882` negative at
  `$80:8160/$93B6` and `$81:BCEB`, or zero at
  `$80:F2A9/$F2C3`, `$81:BD52`, `$82:FC78`, `$87:E779`.
  This set is bounded by simple literal STA/STZ encodings.
- `$82CA` is inside the OAM upload routine entered at `$82A8`,
  before writing $420B. The configured write to $2104 copies $0220 bytes
  from $00:0600. The host's displayed snapshot must align to this
  transfer separately from queue consumption and allocation.

### Direct expansion and layout grammar

- `$8A1A` is a JSR/RTL wrapper around `$8A1E`. `$8A1E`
  also accepts an RTS-style frame if reached by a compatible near caller.
  $8A1E saves P/D, sets D=0, normalizes widths, saves X/Y to DP
  $36/$38, moves A (layout pointer) to Y and reads the allocation cursor
  and cap from DP $2C/$32. A full cursor returns at $8A33, having
  written $36/$38. Otherwise it saves DB, sets DB from DP $41 and
  reaches the BRA at `$8A3D`. That hook sees the selected bank and
  normalized width, without covering the full-cursor path. The code
  consumes layout memory during this call. The normal return writes
  the final $2C cursor, restores DB, D and P and uses RTS at $8AE0.
  A/X/Y remain clobbered, subject to the restored index width.
  Emulation and decimal modes cannot be assumed compatible with the
  normal native binary arithmetic contract. Stock initialization
  assigns $89 to both $009E and $0041 at `$80:816F/$8172`.
- All three piece loops ($8A6D direct, $8DB9 normal queued, $8E39
  overridden queued) use a five-byte record with signed Y delta,
  attribute word, signed X delta and flags. Flag bit 7 ends the list and
  bit 0 supplies the OAM large-size bit. Header $80 loads a little-endian
  replacement pointer in the current layout bank. Direct $7E and normal
  queued $7F can be literal Y deltas. A queued $7E latches priority from
  low two bits of the long WRAM word `$7E:<operand>`, advances three
  bytes, and transfers to $8E39. This overridden loop recognizes $80
  links but treats subsequent $7E as a literal Y delta.
- Direct `$7F` reads a 16-bit selector through `[$3B],Y` with Y zero.
  The DP `$3D` store supplies bank `$7E` to that long pointer.
  It uses twice the selector plus the address
  after the three-byte header to fetch a replacement pointer from the
  layout bank. Selector range, pointed-to layout and
  cyclic or malformed lists have no explicit validity guard in either
  native expander. Binary arithmetic needs decimal clear. Host
  `Expand` uses a global WRAM selector, interprets every queued $7E as
  another priority command and applies a bounded all-list rejection.
- The expansion checks the OAM allocation cursor against the limit at
  entry and after each accepted slot, not for each skipped or linked
  record. Native Y accepts $FFE0–$FFFF or $0000–$00DF. It rejects a
  signed X whose high byte is neither $00 nor $FF after writing
  Y, attributes and low X to the current slot. Accepted X contributes the
  ninth OAM X bit. It packs X-high and size via DP $31 and writes groups
  of four pairs through DP ($2E), advancing the packing destination and
  the four-byte allocation cursor. DP `$2C/$2E/$31/$36/$38/$3A–$40`
  have writes along these paths. Capture partial writes and the stack,
  not just successfully allocated pieces.
- Fetches use native absolute,Y addressing for layout fields. In
  pinned Snes9x, indexed address addition can carry into the next bank
  and 16-bit LDA operands use `WRAP_NONE`. The host layout helper wraps
  its own offsets to `uint16_t`. Low $89 is a WRAM mirror and the
  bank's high half is ROM. Host out-of-map/error rejection and native
  hardware behavior on malformed addresses are separate policies.
  Ordinary stock routes here give no evidence for bank-boundary layouts,
  cyclic links or out-of-range selectors.

### Producer and entry search

| Target | Bounded evidence |
| --- | --- |
| $8B02, $8B09 | 20 and 41 literal JSL/JML candidates respectively, all encoded with $80 targets. $8B02 sources were in banks $80/$81/$84/$86/$87. $8B09 sources additionally covered $83/$85. Raw candidates are not an exhaustive producer ledger. |
| $8C08, $8ED8, $8D89 | One near call to the consumer, one to its updater wrapper, and ten near expansion calls. $8C12 is an interior join. $8E39 additionally has the $8DA9 jump from the priority header. |
| $8A1A, $8A1E, $8A3D | No direct caller for $8A1A or direct transfer to $8A3D from outside the routine. One direct call to $8A1E from $8A1A. Indirect producer presence and direct-expander entry modes still require evidence. |
| $E55A/$E55E | $E55A invokes $E5E2 to update the task position. $E55E is both fallthrough and a real interior callback. $84:A83A stores it and bank $80 into task table $519C/$521C, bypassing the position update. World coordinates subtract DB:$0086/$0088 before culling. |
| $E5A2/$E5A6 | $E5A2 invokes the same position updater. $E5A6 is an interior fallthrough and a real long tail from $85:884C. Screen coordinates require no camera subtraction. |
| $E60B/$E635 | Both have corroborated long calls and long tail entries (34 and seven encodings respectively). They cull supplied X/Y after camera subtraction ($E60B) or directly ($E635) and take bucket from A and layout from DP $C0. |
| $9E8E/$9E9E | $80:9405 stores $9E8E as actor draw callback $0BFE. It uses long DP-indirect layout selection and has an RTS return, making $9E9E an interior culling hook. Accepted origin goes via $9EB8 JSL $8B02. |
| $9EBD/$9EEC | Six direct JSL callers in the $81:9FC3 updater. Hidden or disabled cases return before $9EEC. The accepted path uses $9F06 JML $8B02, so RTL returns to the original caller. |
| $84:90xx HUD JSL sites | $84:902A/$9049/$9068/$9192 call $8B09 directly. $84:90B6/$90E8/$90FF/$910B/$9129 call $8B02 then reach $8B09. All nine rely on long return provenance for host HUD classification. |

The origin bounds are [-48,296) and [-32,280) for the task/helpers
and $9E8E. The $9EBD actor branch has lower X/Y bounds of -24.
Open indirect task and actor entry coverage includes computed or copied
targets and callbacks through future mod mappings. The canonical code
mirror $00:$8xxx and the stock $80:$8xxx entry have the same ROM bytes
but can put different bank bytes on the stack. Higher LoROM aliases
depend on actual map precedence. Snes9x maps WRAM over $7E/$7F and
its small-LoROM SRAM map may cover the high halves of $70–$7D and
$F0–$FF, unlike code windows in $00–$3F/$80–$BF. A byte scan
does not show execution in any alternate bank. Preserve actual PB and
return stack contents and treat all unresolved entries as interpreter paths.

### Confirmed generated layout authors

The generated layout case has a separate static writer chain. $82:B59A
and $82:B6A2 call $B730. The callee checks DB:$4DDA,X, then calls
$B769 to generate layouts and $B8B6 to stage a separate graphics
request. At $82:B769, ROM tables $B7D2/$B7EA describe piece and byte
counts, $B802 gives destination offsets, and $B832 selects lists of
source layout pointers. $B7A2 copies from $89 ROM to bank $00's
low-WRAM mirror via MVN. $B7BD adjusts eligible tile-low bytes, and
$B7C6 replaces priority bits in the high attribute bytes with DP:$56
(which is $20 or $30 in the respective setup paths). The template
groups start at $143F (one $89:D03E record), $145D and $1499 (one
$89:D03E record, then one $89:D05C record each). $14B7 is the second
record of the $1499 group. Each record is 30 bytes in these groups.
The writer runs separately from the `$8ED8` prelude. The `$8D89` code
reads the generated low mirror during queue consumption.
Frame and writer timing for other layouts remains unqualified.

The $0B1E/$0B20 pair influences enqueue bucket order. This is
independent of the $7E command's override of hardware priority bits.
$81:9FE1/A02C/A034/A03F/A042 and $87:9A81/9AD4 are statically confirmed
bucket writers. Detailed reference state captures for $8E39 saw
override sources $D96A/$ECEB/$697A (1P) and those plus $D96C (2P),
with sampled low two bits equal to three. The samples did not
establish a change in the value observed at that boundary.

The $80:953F indexed indirect JSR uses the low-WRAM actor callback
table at $0BFE, corroborating the $9E8E entry and RTS ABI. Task dispatch
at $80:E298/$E29F loads the $519C/$521C callback pointer, stages it at
DP:$DE/$E0 and invokes $E434, which performs JML [$1EDE].
Equivalent dispatch code is repeated for the other task groups.
The sampled D=$1E00 aligns the DP staging words with the fixed long
indirect pointer. The caller stack can refer to the E434 dispatcher
or another outer wrapper after a producer JML.

## Independent original-ROM replay

The independent reference is Snes9x 1.63 at
[`fae2fea08f74180759ef540ee94259213f503480`](https://github.com/libretro/snes9x/tree/fae2fea08f74180759ef540ee94259213f503480).
The stock Windows libretro DLL is pinned in [compatibility](compatibility.md).
The [diagnostic patch](../tools/diagnostics/snes9x-sprite-contract.patch)
was built with MinGW-w64 GCC 15.2.0. Its DLL SHA-256 is
`25b66c3305e47bc10c195cc6903d096676479033d66abf564f2398ea5ef7ee73`.
The dynamic `libwinpthread-1.dll` from that toolchain has SHA-256
`7882d1b093714ccdfaf4e0789a8177920a2b812609949dcbcf7937c5618132d8`.
Snes9x runs the original ROM and provides independent CPU, memory mapping,
DMA and raster execution. The project interpreter and native-bounce paths
share the framework.

Two fresh diagnostic runs per route used zero initial SRAM and identical
inputs. Ordered traces, full selected-boundary snapshots and each per-frame
WRAM/SRAM/video/audio fingerprint matched their repeats. The stock core
produced the same per-frame fingerprints through both routes. These
comparisons had no first divergence. The images show playable holes and HUD
in both modes, including the second hole at the 2P endpoint.

| Original-ROM run | Enqueue `$8B09` | Consumer `$8C12` | Expander `$8D89` | Updater `$8ED8` | Upload `$82CA` |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1P menu to Course 1 Hole 1, 3,101 frames | 22,328 | 2,690 | 22,328 | 204 | 2,879 |
| 2P through the first hole transition, 17,000 frames | 248,103 | 16,363 | 248,103 | 14,134 | 16,583 |

- The sampled enqueue events used D `$1E00`, DB `$7E` and native 16-bit A/X/Y. Sampled `$8D89` events used D zero and DB `$89`. There were 16 and 26 distinct raw stack return-address candidates at `$8B09` respectively. A tail call can share a caller with other producers.
- Detailed 1P frame 3101 and 2P frame 4000 show `$8ED8`, `$81:9FC3`, actor submissions, the updater return, and then `$8C12`. The updater added one request to each of buckets 1 and 6 in 1P, and two to each in 2P. Both retained those counts after `$8D88` returned.
- The WRAM watch observed writes at `$82:B7A2/$B7BD/$B7C6` on both replays. In 2P, `$B7A2` wrote `$1499/$14B7` at frame 2065 and the expander took those pointers at frames 2078 and 2116. Later copies wrote `$143F/$145D` as well. Other writers touch the watched range.
- Both detailed uploads issued 544 ordered `$2104` bytes equal to the pre-DMA WRAM buffer at `$0600–$081F`. All observed OAM transfers used source `$00:0600`, destination `$2104`, channel 0 and length `$0220`. The trace also captures scoped register calls through `S9xSetPPU`. Other DMA and PPU fast paths remain outside that probe.
- `$8A1A/$8A1E/$8A3D`, `$8AE1`, `$E776` and `$81:A0B6` had no stock hits on either replay. `$8841` occurred through `$8823`. The 2P run reached `$84:EFC1` once. Direct producer reachability, alternate modes and unresolved producers still need qualification.

### Reproduce the reference

Commands use Windows PowerShell from the repository root. The replay tool
checks the supported ROM hash and selected core hash. Keep the builds and
full captures under `local/`.

```powershell
$rom = "Kirby's Dream Course (USA).sfc"
Get-FileHash -LiteralPath $rom -Algorithm SHA256
$patch = (Resolve-Path tools/diagnostics/snes9x-sprite-contract.patch).Path
git clone --no-checkout https://github.com/libretro/snes9x.git local/gh83-snes9x
git -C local/gh83-snes9x checkout --detach fae2fea08f74180759ef540ee94259213f503480
git -C local/gh83-snes9x apply $patch
mingw32-make -C local/gh83-snes9x/libretro platform=win -j12
$pthread = Join-Path (Split-Path (Get-Command gcc).Source) 'libwinpthread-1.dll'
Copy-Item -LiteralPath $pthread -Destination local/gh83-snes9x/libretro
$core = 'local/gh83-snes9x/libretro/snes9x_libretro.dll'
$hash = (Get-FileHash $core).Hash.ToLowerInvariant()
New-Item -ItemType Directory -Path local/gh83 -Force | Out-Null
python -c "import json,pathlib; s=json.loads(pathlib.Path('scenarios/menu-to-hole.json').read_text()); pathlib.Path('local/gh83/menu-input.json').write_text(json.dumps({k:s[k] for k in ('frames','events')})+'\n')"

$env:KDC_SPRITE_REFERENCE_DETAIL = '3101'
$env:KDC_SPRITE_REFERENCE_WATCH_LAYOUTS = '1'
foreach ($run in @('a', 'b')) {
    $out = "local/gh83/menu-$run"
    $env:KDC_SPRITE_REFERENCE_TRACE = "$out.trace"
    $env:KDC_SPRITE_REFERENCE_STATE = "$out.state"
    python tools/reference_capture.py --rom $rom --core $core --core-sha256 $hash `
        --frames 3101 --input-recording local/gh83/menu-input.json `
        --wram-frame 3101 --sram-frame 3101 --image-frame 3101 --output "$out.json"
}
Get-FileHash local/gh83/menu-a.trace,local/gh83/menu-b.trace,local/gh83/menu-a.state,local/gh83/menu-b.state
python tools/reference_capture.py --rom $rom --core local/reference/snes9x_libretro.dll `
    --frames 3101 --input-recording local/gh83/menu-input.json `
    --wram-frame 3101 --sram-frame 3101 --image-frame 3101 --output local/gh83/menu-stock.json
python -c "import json,pathlib; r=[json.loads(pathlib.Path(f'local/gh83/menu-{s}.json').read_bytes())['frames'] for s in ('a','b','stock')]; assert r[0]==r[1]==r[2]"
```

The private 2P recording `twoplayer-hole-smoke-17000.json` has SHA-256
`7ec1038d566fafd4b62f547ed87dbdda86a83b853cb1ae89ddfa38b175d60ed4`.
Use it with `--frames 17000`, detail at 4000, `--sram-frame 4000` and WRAM
and image captures at frames 4000 and 17000.

Trace `B`/`I`/`A` rows carry PC, A/X/Y/D/S, DB, packed P, E and a raw
stack return candidate. `B` is a named boundary, `I` a sampled instruction
and `A` follows a traced `R` return. `W` records scoped CPU write calls
with width and write-order argument. `L` watches mapped low layout and
bucket fields. `D` is an OAM transfer and `P` is a scoped PPU register call.
`S` references a zero-based record of 128 KiB WRAM, including the live stack,
then 8 KiB SRAM and 544 bytes of OAM. `F` marks a one-based `retro_run` frame.
The probe scopes interior instructions to specific ranges and a chosen frame.

### Controlled original-ROM calls

The same pinned stock core also ran nine cases against synthetic CPU and
WRAM snapshots. WRAM trampolines enter the original routines with controlled
inputs, and no ROM bytes are changed. The fixture checks return state,
queue headers, origins and OAM buffer bytes. Every case matched its repeat
over unmasked WRAM, SRAM, register and PPU blocks.

```powershell
python tools/probe_sprite_contract.py --rom $rom --core local/reference/snes9x_libretro.dll
```

| Controlled case | Observed contract |
| --- | --- |
| Direct selector and selector update | `$7F` reads through the explicit `$7E` selector bank with layout DB `$7F`. Changing the selector before the call changes which record is emitted. |
| Direct link across `$FFFF` | The operand fetch crosses from `$7F:FFFF` into bank `$80`. The decoy in wrapped bank `$7F` was unused. The host's layout-offset arithmetic wraps within the bank. |
| Direct full cursor and rejected X | Full cursor stages origins and returns without writing OAM. Rejected X writes low X, Y and attributes and leaves the cursor at the same slot. |
| Repeated queued consumption | Native preserves the header and allocates a second slot on the repeat. The first `$7E` selects priority and the second is literal Y. DB `$7F` on entry with a `$7F` decoy in `$009E` still expands through `$89` and returns DB `$7E`. |
| Independent clears | Both `$8841` and `$8AE1` clear pending records and preserve previously emitted OAM. |
| Mutable queued record | An attribute changed after enqueue is the one emitted during consumption through bank `$89`. |

These cases establish the behavior of the supplied inputs. Stock
direct-emitter reachability, malformed pointers and live entry variants
remain separate questions.

### Existing host and independent reference

The frozen Windows host executable has SHA-256
`9c8ddc6f0f6850b259535d62c923505044fc88f0c3bddc5858f6097645e556cd`.
The first full WRAM difference in the idle prefix shared by both routes
is at host frame 15 versus reference frame 16, at `$7E:00A7` (`$0A` host,
`$0B` reference). Six bytes differed at that frame.

| Endpoint | WRAM differences | SRAM | 256×224 native center |
| --- | --- | --- | --- |
| 1P host 3100, reference 3101 | 7 bytes, first `$7E:EA91` | Exact | First RGB difference at `(0,0)`. Zero pixels differed by more than 8 in any channel. |
| 2P host 3999, reference 4000 | 70 bytes, first `$7E:0024` | Exact | First RGB difference at `(0,0)`. Zero pixels differed by more than 8 in any channel. |

RGB comparisons use the host's composed PPM and reference RGB565 export.
The pixel tolerance accommodates quantization. Alignment of interior CPU,
write and DMA streams across the host and reference remains open.

## Frozen enhancement replay

The enhanced oracle uses the executable at the project/host pin above,
framework `147b55e252dcb3e7680a99c7c2f50e7183891844`, launcher UI
`3f1d6f122a58823614abce14f5b86f4d165a629c` and SDL 3.4.10 `g8e37db5e7`.
The guarded Windows build used GCC 15.2.0, Ninja 1.13.2 and native CMake
4.3.1. There were no layout relocations. The diagnostic executable added
the [frozen-stream probe](../tools/diagnostics/frozen-sprite-stream.patch)
to a private copy of `course_sprites.c`. Its SHA-256 is
`00c3dfd10e949f974a1b48b65658edf07fd2e1dc454ec56b5809b9277fe64157`.
It records latch and insertion state, actual margin scanline commands,
including size, attributes, RGB15/opacity and raster window/color-math
state, and the sorted orbit visitor inputs. The comparisons use those
existing-emitter commands as expected values. Snes9x supplies the native
reference above.

| Frozen scenario | Ordered rows | Repeated stream SHA-256 |
| --- | ---: | --- |
| 1P wide shot, 428×224 | 108,979 | `37ddfff63ef97957d8a7e99126b3f280e309207750c6841610a31ac4b78a0743` |
| 1P orbit shot, 256×224 | 69,721 | `9bb01634912796585a377d66a5e49f1b1aaa092a8434d4794086e14ef78786d4` |
| 2P wide shot, 428×224 | 157,262 | `890212c06f3fc2d5b242cf5fa5f51a30ecc6d9cf4f86aa157d2191679ac14328` |
| Wrapped-Y wide view, 428×224 | 38,375 | `49e213e88262ee5b048528e3c3b43a49493876d3514f57941a7ba47897638008` |

Repeated runs within one execution mode matched all stream records, the
final 20 images and full WRAM dumps, and final SRAM. The diagnostic and
uninstrumented 1P wide and orbit captures matched all retained images,
WRAM and SRAM. The original-size images and wide center crops were equal
over the final 20 frames. The 2P run reached mode 1, map `$0080`, terrain 12.
In the wrapped-Y scene, WRAM Y was −31 and observed signed raster Y ranged
from −40 to −31. Disabling the provider changed 492 RGB pixels in the margins
in each of the 20 retained frames, starting at host frame 580 and canvas
position `(388,188)`. Center pixels matched. The images show the recovered
enemy at the right edge with the provider enabled.

Native bounce and interpreter-only disagree in the frozen enhancement
commands. The first differing wide record is 58,697 at the OAM latch with
internal counter 461. The valid lists contain 89 and 83 pieces respectively.
The first orbit difference is record 12,227 at that same latch. The six
additional pieces in the wide comparison have `$81:AF2D` (four) and
`$81:AEA1` (two) projection provenance. Five fall in bucket 2 and one in
bucket 6. A private callback probe logged the wrapper, live A/X/Y/S/DB/D
and pending count in the first differing interval. Five `$80:8B09`
generated callbacks were each followed by an interpreter callback with the
same PC and register values and an already incremented request count.
Generated code invokes the observer, then the tier-down stub dispatches
to the same PC. The `$8C12` observation in that sample was interpreter-only.

In the complete wide replay, 259 latches differ. The 19,010 raster metadata
rows agree. Of 38,614 margin commands in each mode, 3,805 have differing
insertion/order fields with all other fields equal at each command position.
The first changed margin command is at internal raster frame 462, line 35.
Orbit visitor totals are 18,166 and 17,878. The retained wide frames 459–478,
orbit frames 700–774 and final 20 frames in both views matched composed RGB
and full WRAM across modes. Final SRAM matched. Command parity therefore
fails on this route even where the inspected pixels agree.

### Reproduce the frozen host comparisons

Build the frozen commit with `./tools/setup.ps1 -SdlSource <SDL source>`.
For the stream capture, apply `tools/diagnostics/frozen-sprite-stream.patch`
in a separate ignored copy of that commit before building. The reported
diagnostic was linked by replacing the `course_sprites.c` object with one
compiled from the patched private copy. Ninja's
`-t compdb -x CXX_EXECUTABLE_LINKER__KirbyDreamCourseRecomp_Release` emits
the link command for that build.

Use a fresh PowerShell process and a fresh destination for each run. The
runner stages zero initial SRAM, no physical inputs, SDL dummy video/audio,
no sprite-limit override and default HD settings.

```powershell
python -c "import json,pathlib; s=json.loads(pathlib.Path('scenarios/hole-shot.json').read_text()); pathlib.Path('local/gh83/hole-shot-input.json').write_text(json.dumps({k:s[k] for k in ('frames','events')},indent=2)+'\n',encoding='ascii',newline='\n')"
$env:SNESRECOMP_APU_BEAM_ELAPSED = '1'
$env:SNESRECOMP_APU_HANDOFF = '1'
$env:SNESRECOMP_APU_ABSOLUTE_PREANCHOR = '1'
$env:SNESRECOMP_LLE_BOUNCE = '1'
$env:SNESRECOMP_FRAMEDUMP_START = '880'
$env:SNESRECOMP_FRAMEDUMP_END = '899'
$env:SNESRECOMP_FRAMEDUMP_PIXELS = '1'
$env:SNESRECOMP_SCREENSHOT_FRAME = '900'
$env:SNESRECOMP_SCREENSHOT = Join-Path (Get-Location) 'local/gh83/wide-a.ppm'
$env:KDC_FROZEN_SPRITE_STREAM = Join-Path (Get-Location) 'local/gh83/wide-a.log'
./tools/headless.ps1 -Rom $rom -Executable local/gh83-frozen/KirbyDreamCourseRecomp.exe `
    -Output local/gh83/wide-a -Frames 900 -InputRecording local/gh83/hole-shot-input.json `
    -DevHole normal:1:1 -Widescreen -WideExtra 86 -Unpaced -ExpectedUniqueFrames 20
```

Set `SNESRECOMP_LLE_BOUNCE=0` for the interpreter path. For orbit, omit
`-Widescreen -WideExtra 86` and set `KDC_CAMERA_SCRIPT` to an absolute path
for a text file containing `700 32767 -6000 0` and `780 0 0 0` on separate
lines. Use `-DevHole multi:1:1` for the 2P wide shot. For the wrapped-Y
case, use `-DevHole normal:5:3`, no input recording, 600 frames, and a capture
range of 580–599. Repeat with `KDC_SPRITE_VIEW=0` for the margin comparison.
Use `Get-FileHash` on repeated logs, PPMs, frame WRAM files and saves. Compare
full commands in order, with insertion/order differences retained.

For the ordinary 1P native comparison, use the stock executable and
`-Frames 3101 -InputRecording local/gh83/menu-input.json` with no developer
hole, widescreen flag or camera script. Capture the last 20 states and the
frame 3101 screenshot. For the 2P native comparison, use the 4000-frame
prefix of the private 2P recording with the same settings and a frame 4000
screenshot. Capture a separate 20-frame no-input prefix and compare host
frame `n` to Snes9x frame `n+1` for the first WRAM difference. The WRAM,
SRAM and PPM comparisons above retain all bytes in those outputs.

### Reload and rebuild

The frozen wide host saved at the end of the ordinary 3,101-frame route.
Two fresh processes loaded copies of that state and ran 20 idle frames.
The reloads matched each other in all 2,717 command records, images, full
WRAM and final SRAM. Against uninterrupted execution, all 20 WRAM dumps
and final SRAM matched. The first resumed latch at counter 3101 had zero
displayed pieces, and host image 3101 differed at 55 margin pixels, first
at `(427,4)`. The original center matched exactly. At the next latch,
counter 3102, the list had rebuilt. The remaining 2,716 commands and all
19 subsequent images through host frame 3120 matched uninterrupted
execution. The first-frame difference belongs to the frozen state-load
behavior and is part of the baseline.

To reproduce, stage the diagnostic executable, 86-column wide settings,
zero SRAM and the same input script in isolated executable directories.
Use the frozen host environment above with `SDL_VIDEODRIVER=dummy`,
`SDL_AUDIODRIVER=dummy` and `SNESRECOMP_WS_EXTRA=86`. Launch the 3,101-frame
save run with `Autosave = 1` and the 3,121-frame uninterrupted control with
`Autosave = 0`. Invoke each with `--no-launcher --rom PATH --script PATH
--framedump PATH` and its matching `SNESRECOMP_RUN_FRAMES`. The save run
writes `saves/save0.sav` at exit. In each fresh reload directory, disable
autosave and replace `--script PATH` with `--resume-state PATH` pointing to
its own disposable copy of `save0.sav`. Set `SNESRECOMP_RUN_FRAMES=20`,
`SNESRECOMP_FRAMEDUMP_START=0` and `SNESRECOMP_FRAMEDUMP_PIXELS=1`. Compare
against the uninterrupted dumps at 3101–3120 and locate the shared latch
at counter 3102 before comparing the recovery tail of the command logs.

## Observer and mod compatibility gates

| Boundary | Retained responsibility |
| --- | --- |
| `$8823/$8841/$8AE1`, `$8B09`, `$8C12` | Track presentation build lifetime and capture queued origins and provenance. Keep the two clear paths separate from the full OAM reset. Resolve layouts after the updater and drain the host queue once. |
| `$E55E/$E5A6/$E60B/$E635/$9E9E/$9EEC` | Capture requests rejected before native emission. Preserve hidden animation handling and the producer's world/screen distinction. |
| `$8A3D` | Capture direct layout data at the native expansion boundary after native bank selection. |
| `$82CA` and the PPU provider | Latch the display list at OAM upload. Compose margin objects with live raster state, including palette, windows and color math, and preserve native fallback pixels. |

The runtime registers the sprite observations in both [generated-entry
configuration](../recomp/bank00.cfg) and the [interpreter path](../src/course_sprites.c),
with a bank `$84` entry in [bank04.cfg](../recomp/bank04.cfg). In the pinned
`interp_bridge.c`, registration and lookup mask the code mirror bit, retain
the original PC for callbacks and visit callbacks in registration order.
A redirect ends processing at the original PC before later callbacks and
restarts decoding at the destination. A replacement must preserve this or
decline conflicts at original and interior entries, including a fallback to
the same address. The current preloaded mod catalog contains zero packages.
The stock captures do not qualify arbitrary layout mods, plugin hooks or
replacements in these ranges. The renderer and task/attachment provenance
carry their own observers. Profiling in
[#63](https://github.com/alondero/kirbysdreamcourse-recomp/issues/63) includes
`$808B09/$808C08/$808823` and would share these constraints if promoted.

The host decoder treats each queued `$7E` as a priority command, defers
layout reads until `$8C12`, and drains its pending list on the first consume.
Its independent bounds are 1,024 requests, 4,096 pieces and 1,024 steps per
layout. It rejects the whole list on invalid mapping, cycles or overflow
and uses the native fallback pixels in the margins. Any shared decoder
would need explicit inputs for the active layout map, provenance, read
time and culling policy. It would also need to represent writes that happen
before a piece is accepted and preserve queue counts, stack effects and
flags. This discovery leaves all observers and state in place.

## Qualification summary

The ten ROM-free tests below passed against the patched pinned framework.
They exercise wrapped scroll, negative origins, view provenance, fallback
margin pixels, host rejection/overflow, queue reset behavior and DB-aware
reads. The live host shot also passed all four checked-in scenario checks.

```powershell
$env:SNESRECOMP_ROOT = (Resolve-Path local/guarded-framework).Path
python -m pytest -q tests/test_course_sprites.py tests/test_course_sprite_view.py tests/test_course_orbit_render.py
python tools/run_game_scenario.py scenarios/hole-shot.json --exe local/guarded-build/KirbyDreamCourseRecomp.exe
```

Stock routes establish 1P and 2P queued paths, task/actor culling and one
2P hole transition. The fixtures and controlled calls qualify individual
edge inputs. Remaining gaps include stock direct emission, the `$8AE1`
caller, indirect/alternate entries, alternate priority values on live
routes, invalid/cyclic native layouts, queue overwrite behavior, exhaustive
HUD/scorecard classification, other special effects and runtime mod
redirects. The host reset and interactive rewind paths remain unqualified.
The independent host/reference comparison has a pre-game WRAM
divergence and no aligned interior stream comparison. The frozen enhancement
command divergence must be understood at any prospective shared boundary.
