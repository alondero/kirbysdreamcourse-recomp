#include "course_hud.h"
#include "course_view.h"
#include "snes/ppu.h"
#include <string.h>

/* USA-only presentation. All artwork, digits and visibility are live.
 * No values are written into guest RAM, OAM, VRAM or registers. */
static struct {
    const uint8_t *ram;
    bool enabled, active, multi, expanded;
    int extra, shift[2];
} hud;

static uint16_t Map(const Ppu *ppu, int column, int row)
{
    return PpuRenderVram(ppu)[0x3c00 + row * 32 + column];
}

bool KdcHudActive(const Ppu *ppu)
{
    return ppu && hud.active && ppu->backgroundSample == KdcHudSample;
}

static bool Signature(const Ppu *ppu)
{
    return hud.enabled && hud.ram && hud.extra >= 43 && KdcIsCourse(ppu) &&
        (ppu->renderFlags & kPpuRenderFlags_NewRenderer) &&
        ppu->bgXsc[2] == 0x3c && PPU_bgTileAdr(ppu, 2) == 0x4000 &&
        !PPU_bigTiles(ppu, 2) && !PPU_forcedBlank(ppu) &&
        PPU_brightness(ppu) == 15 && PPU_mosaicSize(ppu) <= 1 &&
        !PPU_objInterlace(ppu) && PPU_objSize(ppu) == 0 &&
        ppu->hScroll[2] == 0 && ppu->vScroll[2] == 0 &&
        !(ppu->screenWindowed[0] & 4) && !(ppu->cgadsub & 4);
}

/* Four 16px pieces form each fixed conditions portrait. Matching the full
 * quartet (including palette, coordinates and size) avoids classifying a
 * world object from its screen position alone. Graphics inside these tiles
 * are updated by the game as the ability changes. */
static bool Portraits(Ppu *ppu)
{
    unsigned found[2] = {0, 0};
    unsigned slots[2][4] = {{0}};
    for (unsigned slot = 0; slot < 128; ++slot) {
        unsigned a = ppu->oam[slot * 2 + 1];
        int x = ppu->oam[slot * 2] & 255;
        int y = ppu->oam[slot * 2] >> 8;
        unsigned high = (ppu->highOam[slot / 4] >> ((slot & 3) * 2)) & 3;
        if (high != 2) continue; /* Positive X, 16px large object in size mode 0. */
        for (unsigned player = 0; player < (hud.multi ? 2u : 1u); ++player) {
            unsigned base = player ? 0x35a8 : 0x33a0;
            if (a < base || a > base + 6 || ((a - base) & 1)) continue;
            unsigned piece = (a - base) / 2;
            int ox = hud.multi ? (player ? 208 : 16) : 20;
            int oy = hud.multi ? 171 : 179;
            if (x != ox + (int)(piece & 1) * 16 ||
                y != oy + (int)(piece >> 1) * 16 || (found[player] & (1u << piece))) continue;
            found[player] |= 1u << piece;
            slots[player][piece] = slot;
        }
    }
    if (found[0] != 15 || (hud.multi && found[1] != 15)) return false;
    for (unsigned player = 0; player < (hud.multi ? 2u : 1u); ++player)
        for (unsigned i = 0; i < 4; ++i)
            ppu->presentationObjShift[slots[player][i]] = hud.shift[player];
    return true;
}

/* The native panel also masks terrain/objects and admits fixed-color math.
 * Move that backing with the tile panel; otherwise a white hole remains at
 * the old HUD position. BG3 itself never participates in this window. */
static bool Windows(const Ppu *ppu, unsigned layer, int y,
                    int16_t *edges, uint8_t *count, uint8_t *bits)
{
    if (!KdcHudActive(ppu) || !hud.expanded || layer == 2 || layer == 3) return false;
    int inset = hud.extra > 96 ? hud.extra - 96 : 0;
    int left = -hud.extra + inset + 12;
    int right = 256 + hud.extra - inset - 12 - 32;
    int ranges[4], n = 0;
    if (!hud.multi) {
        if (y >= 180 && y < 212) { ranges[n++] = left + 36; ranges[n++] = left + 112; }
        if (y >= 164 && y < 212) { ranges[n++] = right - 80; ranges[n++] = right + 32; }
    } else {
        if (y >= 12 && y < 32) { ranges[n++] = 48; ranges[n++] = 208; }
        if (y >= 180 && y < 196) {
            ranges[n++] = left + 36; ranges[n++] = left + 108;
            ranges[n++] = right - 76; ranges[n++] = right - 4;
        }
    }
    edges[0] = -ppu->extraLeftCur;
    *count = 1; *bits = 0;
    for (int i = 0; i < n; i += 2) {
        edges[(*count)++] = ranges[i];
        *bits |= 1u << (*count - 1);
        edges[(*count)++] = ranges[i + 1];
    }
    edges[*count] = 256 + ppu->extraRightCur;
    return true;
}

/* Native HUD HDMA window profile measured from the supported USA ROM.
 * Anything else may be a transition/effect: retain the hardware layout. */
static bool SupportedWindow(const Ppu *ppu, int line)
{
    if (ppu->windowsel != 0x220000 || ppu->wbgobjlog ||
        ppu->window2left || ppu->window2right || ppu->cgwsel != 0x10 ||
        ppu->cgadsub != 0x33 || ppu->fixedColor != 0x7fff ||
        ppu->screenWindowed[0] != 0x13 || ppu->screenWindowed[1]) return false;
    int left = 128, right = 127;
    if (hud.expanded) {
        if (!hud.multi && line >= 179 && line < 211) { left = 56; right = 235; }
        if (hud.multi) {
            if (line == 171 || line == 202) { left = 46; right = 209; }
            if (line == 172 || line == 201) { left = 47; right = 208; }
            if (line >= 173 && line < 201) { left = 48; right = 207; }
            if (line >= 203 && line < 213) { left = 44; right = 212; }
        }
    }
    return ppu->window1left == left && ppu->window1right == right;
}

static void PrepareLine(Ppu *ppu, int line, void *context)
{
    (void)context;
    memset(ppu->presentationObjShift, 0, sizeof(ppu->presentationObjShift));
    PpuSetBackgroundSample(ppu, NULL, NULL);
    hud.active = false;
    if (!Signature(ppu)) return;
    unsigned mode = KdcReadWord(hud.ram, 0x17ff);
    if (mode > 1) return;
    hud.multi = mode != 0;
    /* The expanded panel's border is a live tile, not a guessed time/phase. */
    uint16_t border = Map(ppu, hud.multi ? 4 : 2, hud.multi ? 21 : 22);
    hud.expanded = border == (hud.multi ? 0x34e7 : 0x3519);
    if (!SupportedWindow(ppu, line)) return;
    int inset = hud.extra > 96 ? hud.extra - 96 : 0;
    int left = -hud.extra + inset + 12;
    int right = 256 + hud.extra - inset - 12 - 32;
    hud.shift[0] = left - (hud.multi ? 16 : 20);
    hud.shift[1] = right - 208;
    hud.active = Portraits(ppu);
    if (hud.active) PpuSetBackgroundSample(ppu, KdcHudSample, NULL);
}

int KdcHudPortraitShift(const Ppu *ppu, int x, int y, uint16_t attributes)
{
    if (!KdcHudActive(ppu)) return 0;
    for (unsigned p = 0; p < (hud.multi ? 2u : 1u); ++p) {
        unsigned base = p ? 0x35a8 : 0x33a0;
        unsigned piece = (attributes - base) / 2;
        if (attributes < base || attributes > base + 6 || ((attributes - base) & 1)) continue;
        if (x == (hud.multi ? (p ? 208 : 16) : 20) + (int)(piece & 1) * 16 &&
            y == (hud.multi ? 171 : 179) + (int)(piece >> 1) * 16)
            return hud.shift[p];
    }
    return 0;
}

static uint16_t Clean(int x, int y, int *px, int *py)
{
    bool flip;
    unsigned column = KdcReflectedColumn(x, 0, &flip) & 31;
    *px = x & 7; *py = y & 7;
    return KdcReadWord(hud.ram, 0xdadc + 2 * (((y >> 3) & 31) * 32 + column)) ^ (flip ? 0x4000 : 0);
}

/* The straight top border has a transparent interior row. Reuse that
 * row for padding so the native white backing shows through; the blank
 * cell between HOLE and SHOT is solid yellow, not neutral whitespace. */
static void Padding(const Ppu *ppu, int x, uint16_t *tile, int *px, int *py)
{
    *tile = Map(ppu, 3, 22); *px = x & 7; *py = 4;
}

static void SoloCard(const Ppu *ppu, int dx, int y,
                     uint16_t *tile, int *px, int *py)
{
    Padding(ppu, dx, tile, px, py);
    /* A complete independent outer frame, assembled from the original
     * outer corners/rails rather than the counter panel's inner cells. */
    int column = dx < 8 ? 2 : dx >= 112 ? 29 : 3;
    if (y < 168 || y >= 208) {
        *tile = Map(ppu, column, y < 168 ? 22 : 26);
        *px = dx & 7; *py = y & 7;
    } else if (column != 3) {
        *tile = Map(ppu, column, 23); *px = dx & 7; *py = y & 7;
    }
    /* Keep the complete name drawing and counters, including the drawing's
     * lower pink border. Stock occupies a separate row after a clear gap. */
    if (dx >= 8 && dx < 112 && y >= 164 && y < 194) {
        int sx = 56 + dx - 8, sy = y + 16;
        *tile = Map(ppu, sx >> 3, sy >> 3); *px = sx & 7; *py = sy & 7;
    }
    if (dx >= 24 && dx < 96 && y >= 200 && y < 208) {
        int sx = 160 + dx - 24;
        *tile = Map(ppu, sx >> 3, 25); *px = sx & 7; *py = y & 7;
    }
}

bool KdcHudSample(const Ppu *ppu, unsigned layer, int x, int y,
                  uint16_t *tile, int *px, int *py, void *context)
{
    (void)context;
    if (layer != 2 || !KdcHudActive(ppu)) return false;
    /* Suppress only the original expanded status band; shot art is separate.
     * Native screen y here is the SNES scanline, including its +1 phase. */
    if (hud.expanded && y >= (hud.multi ? 168 : 176) && y < 216)
        *tile = Clean(x, y, px, py);
    if (!hud.expanded) return true;
    int left = (hud.multi ? 16 : 20) + hud.shift[0];
    int right = 208 + hud.shift[1];
    int sx = -1, sy = y;
    if (!hud.multi) {
        /* Original portrait enclosure + energy tiles. Stock is removed from
         * this extension and reattached below the original identity/counters. */
        int dx = x - (left - 4);
        if (y >= 176 && y < 216 && dx >= 0 && dx < 120) {
            sx = dx < 40 ? 16 + dx : 160 + dx - 40;
            if (y >= 200 && y < 208 && dx >= 40 && dx < 112) {
                Padding(ppu, x, tile, px, py);
                return true;
            }
        }
        int edge = 256 + hud.extra - (hud.extra > 96 ? hud.extra - 96 : 0) - 8;
        dx = x - (edge - 120);
        if (dx >= 0 && dx < 120 && y >= 160 && y < 216) {
            SoloCard(ppu, dx, y, tile, px, py);
            return true;
        }
    } else {
        /* Energy retains the original pink/yellow frame; the original player
         * labels and POINT counters form a centered upper strip. */
        int dx = x - (left + 32);
        if (dx >= 0 && dx < 80 && y >= 176 && y < 200) { sx = 48 + dx; sy = y + 16; }
        dx = x - (right - 80);
        if (dx >= 0 && dx < 80 && y >= 176 && y < 200) { sx = 128 + dx; sy = y + 16; }
        dx = x - 32;
        if (dx >= 0 && dx < 192 && y >= 8 && y < 32) { sx = 32 + dx; sy = y + 160; }
    }
    if (sx >= 0) {
        uint16_t source = Map(ppu, sx >> 3, sy >> 3);
        uint16_t clean = KdcReadWord(hud.ram, 0xdadc + 2 * ((sy >> 3) * 32 + (sx >> 3)));
        if (source != clean) { *tile = source; *px = sx & 7; *py = sy & 7; }
    }
    return true;
}

bool KdcHudOverlayPixel(const Ppu *ppu, int x, int y)
{
    if (!KdcHudActive(ppu)) return false;
    int px, py;
    uint16_t clean = Clean(x, y, &px, &py);
    uint16_t tile = x >= 0 && x < 256 ? Map(ppu, x >> 3, y >> 3) : clean;
    KdcHudSample(ppu, 2, x, y, &tile, &px, &py, NULL);
    if (tile != clean) {
        if (tile & 0x4000) px ^= 7;
        if (tile & 0x8000) py ^= 7;
        unsigned bits = PpuRenderVram(ppu)[(0x4000 + (tile & 1023) * 8 + py) & 0x7fff] >> (7 - px);
        if ((bits & 1) | ((bits >> 7) & 2)) return true;
    }
    int16_t edges[8]; uint8_t count, mask;
    if (Windows(ppu, 0, y, edges, &count, &mask))
        for (unsigned i = 0; i < count; ++i)
            if ((mask & (1u << i)) && x >= edges[i] && x < edges[i + 1]) return true;
    return false;
}

void KdcPrepareHud(Ppu *ppu, const uint8_t *ram, int extra, bool enabled)
{
    hud.ram = ram; hud.extra = extra; hud.enabled = enabled; hud.active = false;
    memset(ppu->presentationObjShift, 0, sizeof(ppu->presentationObjShift));
    ppu->presentationPrepare = enabled && extra ? PrepareLine : NULL;
    ppu->presentationContext = NULL;
    ppu->presentationWindows = enabled && extra ? Windows : NULL;
    PpuSetBackgroundSample(ppu, NULL, NULL);
}
