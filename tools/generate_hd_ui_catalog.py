"""Rebuild the HD UI lookup tables from private scene VRAM dumps and catalog.json.
Inputs live under local/texture-dump. Only hashes, tile roles and positions are emitted.
"""
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
DUMP_DIR = ROOT / "local" / "texture-dump"
SCENES_DIR = DUMP_DIR / "scenes" / "dumps"


def read_u16(p):
    d = p.read_bytes()
    return list(struct.unpack(f"<{len(d)//2}H", d))


def fnv1a_words(words, n):
    h = 0xCBF29CE484222325
    for w in words[:n]:
        b0 = w & 0xFF
        b1 = (w >> 8) & 0xFF
        h = ((h ^ b0) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        h = ((h ^ b1) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


# Replicate KdcCpuLabelRender from src/cpu_label.c to get exact hashes for CPU labels!
CPU_GLYPHS = {
    'A': (3, [".X.", "X.X", "XXX", "X.X", "X.X"]),
    'C': (3, [".XX", "X..", "X..", "X..", ".XX"]),
    'D': (3, ["XX.", "X.X", "X.X", "X.X", "XX."]),
    'E': (3, ["XXX", "X..", "XX.", "X..", "XXX"]),
    'H': (3, ["X.X", "X.X", "XXX", "X.X", "X.X"]),
    'I': (3, ["XXX", ".X.", ".X.", ".X.", "XXX"]),
    'M': (5, ["X...X", "XX.XX", "X.X.X", "X...X", "X...X"]),
    'P': (3, ["XX.", "X.X", "XX.", "X..", "X.."]),
    'R': (3, ["XX.", "X.X", "XX.", "X.X", "X.X"]),
    'S': (3, [".XX", "X..", ".X.", "..X", "XX."]),
    'U': (3, ["X.X", "X.X", "X.X", "X.X", "XXX"]),
    'Y': (3, ["X.X", "X.X", ".X.", ".X.", ".X."]),
}


def cpu_label_render(name):
    tiles = [[0x00FF] * 8 for _ in range(8)]

    def clear(x, y):
        if 0 <= x < 32 and 0 <= y < 16:
            bit = 0x80 >> (x & 7)
            tiles[(y // 8) * 4 + (x // 8)][y & 7] &= (~bit) & 0xFFFF

    for x in range(32):
        clear(x, 0)
        clear(x, 15)
    for y in range(16):
        clear(0, y)
        clear(31, y)

    def draw_line(text, top):
        w = 0
        for ch in text:
            if ch in CPU_GLYPHS:
                w += CPU_GLYPHS[ch][0] + (1 if w else 0)
        if w > 30:
            return
        x = 1 + (30 - w) // 2
        for ch in text:
            if ch not in CPU_GLYPHS:
                continue
            gw, rows = CPU_GLYPHS[ch]
            for r in range(5):
                for c in range(gw):
                    if rows[r][c] == 'X':
                        clear(x + c, top + r)
            x += gw + 1

    draw_line("CPU", 2)
    draw_line(name, 9)
    return tiles


cat = json.loads((DUMP_DIR / "catalog.json").read_text())
vram_title = read_u16(SCENES_DIR / "title_screen.vram.bin")
vram_1p = read_u16(SCENES_DIR / "hud_1p_expanded.vram.bin")
vram_2p = read_u16(SCENES_DIR / "hud_2p_expanded.vram.bin")
vram_pwr = read_u16(SCENES_DIR / "hud_1p_power.vram.bin")

entries = {}


def add_entry(h, bpp, kind, param, tx, ty, desc):
    # Shared character halves and portrait corners use the first matching role.
    entries.setdefault((h,bpp),(kind,param,tx,ty,desc))


# 1. Standard 2bpp font (0x001..0x045, 0x051..0x06C) in Variant 1 and Variant 2
for ch in list(range(1, 0x46)) + list(range(0x51, 0x6D)):
    w1 = [vram_title[0x5000 + ch * 8 + r] for r in range(8)]
    if all(w == 0 for w in w1):
        continue
    w2 = [w | 0x00FF for w in w1]
    add_entry(fnv1a_words(w1, 8), 2, "kUiFontV1", ch, 0, 0, f"font_v1_{ch:02x}")
    add_entry(fnv1a_words(w2, 8), 2, "kUiFontV2", ch, 0, 0, f"font_v2_{ch:02x}")

# 1b. Title Screen Credits (0x0D0..0x0EF) in Variant 1 (title_screen) and Variant 2 (versus_handicap)
for ch in range(0x0D0, 0x0F0):
    w1 = [vram_title[0x5000 + ch * 8 + r] for r in range(8)]
    if all(w == 0 for w in w1):
        continue
    w2 = [w | 0x00FF for w in w1]
    add_entry(fnv1a_words(w1, 8), 2, "kUiCreditsV1", ch - 0x0D0, 0, 0, f"credits_v1_{ch:02x}")
    add_entry(fnv1a_words(w2, 8), 2, "kUiCreditsV2", ch - 0x0D0, 0, 0, f"credits_v2_{ch:02x}")

# 2. Text-box borders (0x080..0x085, 0x0BE..0x0BF in hud_1p_expanded)
for idx, ch in enumerate([0x080, 0x081, 0x082, 0x083, 0x084, 0x085, 0x0BE, 0x0BF]):
    w = [vram_1p[0x4000 + ch * 8 + r] for r in range(8)]
    add_entry(fnv1a_words(w, 8), 2, "kUiBoxBorder", idx, 0, 0, f"box_border_{ch:03x}")

# 3. 1P HUD HOLE/SHOT labels, Mini Kirby, '=', and analytic corners
for param, ch in enumerate([0x10F, 0x11A, 0x11C, 0x105]):
    w = [vram_1p[0x4000 + ch * 8 + r] for r in range(8)]
    add_entry(fnv1a_words(w, 8), 2, "kUiHud1pLabel", param, 0, 0, f"1p_label_{ch:03x}")

for param, ch in enumerate([0x11D, 0x11E, 0x11F]):
    w = [vram_1p[0x4000 + ch * 8 + r] for r in range(8)]
    add_entry(fnv1a_words(w, 8), 2, "kUiHud1pIcon", param, 0, 0, f"1p_icon_{ch:03x}")

for param, ch in enumerate([0x119, 0x112, 0x13C, 0x13D]):
    w = [vram_1p[0x4000 + ch * 8 + r] for r in range(8)]
    add_entry(fnv1a_words(w, 8), 2, "kUiHud1pCorner", param, 0, 0, f"1p_corner_{ch:03x}")

# 4. 2P HUD POINT banner (0x0F0..0x0F7), PLAYER 1/2 (0x100..0x10F), CPU labels, Capsules (0x111, 0x113), Corners
for i, ch in enumerate(range(0x0F0, 0x0F8)):
    w = [vram_2p[0x4000 + ch * 8 + r] for r in range(8)]
    add_entry(fnv1a_words(w, 8), 2, "kUiHud2pPoint", 0, i % 4, i // 4, f"2p_point_{ch:03x}")

for i, ch in enumerate(range(0x100, 0x108)):
    w = [vram_2p[0x4000 + ch * 8 + r] for r in range(8)]
    add_entry(fnv1a_words(w, 8), 2, "kUiHud2pPlayer", 0, i % 4, i // 4, f"2p_p1_{ch:03x}")

for i, ch in enumerate(range(0x108, 0x110)):
    w = [vram_2p[0x4000 + ch * 8 + r] for r in range(8)]
    h = fnv1a_words(w, 8)
    if (h, 2) not in entries:
        add_entry(h, 2, "kUiHud2pPlayer", 1, i % 4, i // 4, f"2p_p2_{ch:03x}")

for diff_idx, diff_name in enumerate(["EASY", "MEDIUM", "HARD"]):
    ctiles = cpu_label_render(diff_name)
    for i, w in enumerate(ctiles):
        h = fnv1a_words(w, 8)
        if (h, 2) not in entries:
            add_entry(h, 2, "kUiHud2pPlayer", 2 + diff_idx, i % 4, i // 4, f"2p_cpu_{diff_name}_{i}")

for param, ch in enumerate([0x111, 0x113]):
    w = [vram_2p[0x4000 + ch * 8 + r] for r in range(8)]
    add_entry(fnv1a_words(w, 8), 2, "kUiHud2pCapsule", param, 0, 0, f"2p_cap_{ch:03x}")

for param, ch in enumerate([0x114, 0x11D, 0x0E7, 0x116]):
    w = [vram_2p[0x4000 + ch * 8 + r] for r in range(8)]
    add_entry(fnv1a_words(w, 8), 2, "kUiHud2pCorner", param, 0, 0, f"2p_corner_{ch:03x}")

# 5. All 11 Ability Portraits + 2P Keeby Portrait
for ab in cat["abilities"]:
    aid = ab["ability"]
    for t in ab["tiles"]:
        h = int(t["hash"], 16)
        add_entry(h, 4, "kUiAbilityPortrait", aid, t["tx"], t["ty"], f"ab_{aid}_{t['tx']}_{t['ty']}")

for ty in range(4):
    for tx in range(4):
        t = 0x0A8 + (ty // 2) * 4 + (ty % 2) * 16 + tx
        w = [vram_2p[0x7000 + t * 16 + k] for k in range(16)]
        add_entry(fnv1a_words(w, 16), 4, "kUiAbilityPortrait", 11, tx, ty, f"ab_11_{tx}_{ty}")

# 6. Shot Spin Gauge (48x48 box + spin bar variants)
spin_rows_base = [
    [0x010, 0x011, 0x012, 0x013, 0x014, 0x015],
    [0x020, 0x021, 0x022, 0x023, 0x024, 0x025],
    [0x016, 0x017, 0x030, 0x031, 0x018, 0x019],
    [0x026, 0x027, 0x040, 0x041, 0x028, 0x029],
    [0x01A, 0x01B, 0x01C, 0x01D, 0x01E, 0x01F],
    [0x02A, 0x02B, 0x02C, 0x02D, 0x02E, 0x02F],
]
for ty in range(6):
    for tx in range(6):
        ch = spin_rows_base[ty][tx]
        w = [vram_pwr[0x6000 + ch * 16 + k] for k in range(16)]
        h = fnv1a_words(w, 16)
        if (h, 4) not in entries:
            add_entry(h, 4, "kUiSpinGauge", 0, tx, ty, f"spin_{tx}_{ty}_{ch:03x}")

# Also include the uncovered center spin-ball tiles 0x018, 0x019, 0x028, 0x029 when spin bar is hidden
for (tx, ty, ch) in [(2, 2, 0x017), (3, 2, 0x018), (2, 3, 0x027), (3, 3, 0x028)]:
    w = [vram_pwr[0x6000 + ch * 16 + k] for k in range(16)]
    h = fnv1a_words(w, 16)
    if (h, 4) not in entries:
        add_entry(h, 4, "kUiSpinGauge", 0, tx, ty, f"spin_alt_{tx}_{ty}_{ch:03x}")

# 7. Bounce Guide Numerals 1..9 (0x080..0x088 top, 0x090..0x098 bottom)
for d in range(9):
    for ty, ch in ((0, 0x080 + d), (1, 0x090 + d)):
        w = [vram_pwr[0x6000 + ch * 16 + k] for k in range(16)]
        h = fnv1a_words(w, 16)
        if (h, 4) not in entries:
            add_entry(h, 4, "kUiGuideNum", d, 0, ty, f"guide_{d+1}_{ty}_{ch:03x}")

# 8. "OB!" Popup (0x08C..0x08F top, 0x09C..0x09F bottom)
for tx in range(4):
    for ty, ch in ((0, 0x08C + tx), (1, 0x09C + tx)):
        w = [vram_pwr[0x6000 + ch * 16 + k] for k in range(16)]
        h = fnv1a_words(w, 16)
        if (h, 4) not in entries:
            add_entry(h, 4, "kUiObPopup", 0, tx, ty, f"ob_{tx}_{ty}_{ch:03x}")

# 9. 16x16 OBJ Vitality Tomatoes (0x060..0x063 top, 0x070..0x073 bottom in OBJ2 0x7000)
for var in range(2):
    for ty in range(2):
        for tx in range(2):
            ch = (0x060 if ty == 0 else 0x070) + var * 2 + tx
            w = [vram_pwr[0x7000 + ch * 16 + k] for k in range(16)]
            h = fnv1a_words(w, 16)
            if (h, 4) not in entries:
                add_entry(h, 4, "kUiObjTomato", var, tx, ty, f"otom_{var}_{tx}_{ty}_{ch:03x}")

# 10. Remaining Course HUD OBJ1 tiles (Power Bar 0x032..0x04F, Stars 0x001..0x00B, Markers/Reticle 0x060..0x07F, Dots 0x089..0x09A)
for ch in list(range(0x001, 0x010)) + list(range(0x032, 0x050)) + list(range(0x051, 0x053)) + list(range(0x060, 0x080)) + [0x089, 0x08A, 0x099, 0x09A]:
    w = [vram_pwr[0x6000 + ch * 16 + k] for k in range(16)]
    if all(v == 0 for v in w):
        continue
    h = fnv1a_words(w, 16)
    if (h, 4) not in entries:
        add_entry(h, 4, "kUiHudObj", ch, 0, 0, f"hud_obj_{ch:03x}")



def main():
    # Extra scene metadata. Only word hashes, layout and semantic roles leave local/.
    def record(words, bpp, kind, param, x=0, y=0):
        key = fnv1a_words(words, bpp * 4), bpp
        entries.setdefault(key, (kind, param, x, y, kind))
    def hud(ch, kind, param, x=0, y=0):
        addr = 0x4000 + ch * 8
        record(vram_1p[addr:addr+8], 2, kind, param, x, y)
    for ch in range(0x120, 0x12c):
        hud(ch, 'kUiVitality', ch - 0x120)
    for ch in range(0xe3, 0xed):
        hud(ch, 'kUiStock', (ch - 0xe2) % 10)
    shots = [(0xe1,0xe2),(0xd2,0xd3),(0xd4,0xd5),(0xd4,0xd6),(0xd7,0xd8),
             (0xd9,0xda),(0xdb,0xdc),(0xdd,0xde),(0xdf,0xdc),(0xdf,0xe0)]
    for digit, pair in enumerate(shots):
        for y, ch in enumerate(pair):
            hud(ch, 'kUiShotDigit', digit, 0, y)
    holes = [(0xed,0xee,0xef,0xf0),(0xf1,0xf2,0xf3,0xf4),(0xf5,0xf6,0,0),
             (0xf7,0xf8,0xf9,0xfa),(0xfb,0xfc,0xfd,0),(0,0xfe,0xff,0),
             (0x100,0x101,0x102,0x103),(0x104,0xf6,0xff,0)]
    for digit, quad in enumerate(holes, 1):
        for tile, ch in enumerate(quad):
            if ch: hud(ch, 'kUiHoleDigit', digit, tile % 2, tile // 2)
    v2 = struct.unpack("<32768H",(SCENES_DIR/"hud_2p_expanded.vram.bin").read_bytes())
    for n in range(20):
        addr = 0x4000 + (0xd2 + n)*8
        record(v2[addr:addr+8],2,"kUiPointsDigit",n//2,0,n%2)
    title = []
    for y in range(2, 26):
        for x in range(32):
            t = vram_title[0x4000 + 32*y + x]
            ch, pal = t & 1023, (t>>10)&7
            if ch and not (t>>14) and pal in (1,3,5):
                h = fnv1a_words(vram_title[ch*16:ch*16+16],16)
                title.append((ch,h,pal,x,y))
    lines = ['/* VRAM character address and content jointly identify title artwork. */',
             'static const UiTileEntry kTitleTiles[] = {']
    for ch,h,pal,x,y in sorted(title):
        lines.append(f'    [0x{ch:x}] = {{UINT64_C(0x{h:016x}), 4, kUiTitle, {pal}, {x}, {y}}},')
    lines.append('};')
    lines.append("static const UiTileEntry kTitleDecor[] = {")
    for ch in range(0x1c2, 0x1f5):
        if 0x1c2 <= ch <= 0x1ca:
            kind, param, x, y = 'kUiTitleRail', ch-0x1c2, 0, 0
        else:
            col, row = ch & 15, ch >> 4
            kind = 'kUiTitleStar'
            if 0 <= col <= 2 and row >= 0x1d:
                param, x, y = 0, col, row-0x1d
            elif col in (3,4) and row >= 0x1d:
                param, x, y = 1, col-3, row-0x1d
            elif col in (12,13) and row in (0x1c,0x1d):
                param, x, y = 2, col-12, row-0x1c
            else: continue
        h = fnv1a_words(vram_title[ch*16:ch*16+16],16)
        lines.append(f'    [0x{ch-0x1c2:x}] = {{UINT64_C(0x{h:016x}), 4, {kind}, {param}, {x}, {y}}},')
    lines.append("};")
    for table, scene, layer, rows, columns, role in [
        ("kMembersTitle", "member_register", 0, (4,5), range(6,26), 0),
        ("kEntryTitle", "name_entry", 1, (3,4), range(5,22), 1)]:
        regs = json.loads((SCENES_DIR / (scene + ".regs.json")).read_text())
        vram = struct.unpack("<32768H", (SCENES_DIR / (scene + ".vram.bin")).read_bytes())
        sc = regs["bgsc"][layer]
        base = ((regs["bg_tile_adr"] >> (4*layer)) & 15) << 12
        lines.append("static const UiTileEntry " + table + "[] = {")
        for ty, row in enumerate(rows):
            for tx, column in enumerate(columns):
                x = (8*column + regs["hscroll"][layer])//8
                y = (8*row + regs["vscroll"][layer]+1)//8
                page = (x//32%2 if sc&1 else 0) + ((1+(sc&1))* (y//32%2) if sc&2 else 0)
                tile = vram[((sc&252)*256 + 1024*page + 32*(y%32) + x%32)&32767]&1023
                addr = base + tile*16
                h = fnv1a_words(vram[addr:addr+16],16)
                lines.append(f"    [0x{tile:x}] = {{UINT64_C(0x{h:016x}), 4, kUiMenuBanner, {role}, {tx}, {ty}}},")
        lines.append("};")
    vm = struct.unpack("<32768H",(SCENES_DIR/"member_register.vram.bin").read_bytes())
    lines.append("static const UiTileEntry kMemberActions[] = {")
    for role,(first,width) in enumerate([(0x93,13),(0x73,16),(0xad,10),(0xc1,12)]):
        for n in range(2*width):
            ch = first+n
            h = fnv1a_words(vm[ch*16:ch*16+16],16)
            lines.append(f"    [0x{ch-0x73:x}] = {{UINT64_C(0x{h:016x}),4,kUiMemberAction,{role},{n%width},{n//width}}},")
    lines.append("};")
    (ROOT / "src/hd_ui_title.inc").write_text('\n'.join(lines)+'\n')
    # Sort entries by (hash, bpp) for binary search in C
    sorted_items = sorted(entries.items(), key=lambda kv: (kv[0][0], kv[0][1]))

    table_lines = []
    for (h, bpp), (kind, param, tx, ty, desc) in sorted_items:
        table_lines.append(
            f"    {{UINT64_C(0x{h:016x}), {bpp}, {kind}, {param}, {tx}, {ty}}}, /* {desc} */"
        )

    (ROOT / "src/hd_ui_catalog.inc").write_text(
        "static const UiTileEntry kUiEntries[] = {\n" + "\n".join(table_lines) + "\n};\n")
    print(f"Generated {len(table_lines)} interface character roles")


if __name__ == "__main__":
    main()
