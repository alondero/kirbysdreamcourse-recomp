#include "course_hud.h"
#include "course_view.h"
#include "snes/ppu.h"
#include <string.h>

/* USA-only presentation. All artwork, digits and visibility are live.
 * No values are written into guest RAM, OAM, VRAM or registers. */
static struct {
    const uint8_t *ram;
    bool enabled, active, multi, expanded, saw_top_lines, top_strip;
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

/* Practice's top-center badge is drawn after the full frame finishes, so
 * query the 2P top-strip state recorded on the top strip's own scanlines
 * (lines 7..34 prepare drawn rows 8..35) rather than relying on whatever
 * window state the final scanline of the frame left behind. Re-check
 * Signature(ppu) so a subsequent forced-blank frame (where ppu_runLine skips
 * sprite evaluation) cannot report a stale latch. */
bool KdcHudTopStripActive(const Ppu *ppu)
{
    if (!ppu || !Signature(ppu)) return false;
    return hud.saw_top_lines ? hud.top_strip
                             : (KdcHudActive(ppu) && hud.multi && hud.expanded);
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
        if (y >= kKdcHudMultiTopY0 + kKdcHudMultiTopPad &&
            y < kKdcHudMultiTopY1 - kKdcHudMultiTopPad) {
            ranges[n++] = kKdcHudMultiTopX0 + kKdcHudMultiTopPad;
            ranges[n++] = kKdcHudMultiTopX1 - kKdcHudMultiTopPad;
        }
        if (y >= 180 && y < 196) {
            ranges[n++] = left + 32; ranges[n++] = left + 112;
            ranges[n++] = right - 80; ranges[n++] = right;
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
    /* This selector enables only window 1. Window 2's unused bounds differ
     * between normal menu entry (255..0) and developer starts (0..0). */
    if (ppu->windowsel != 0x220000 || ppu->wbgobjlog ||
        ppu->cgwsel != 0x10 ||
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
    if (line == 0) {
        hud.saw_top_lines = false;
        hud.top_strip = false;
    }
    bool on_top = line >= kKdcHudMultiTopY0 - 1 && line < kKdcHudMultiTopY1 - 1;
    if (on_top) hud.saw_top_lines = true;
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
    if (hud.active) {
        PpuSetBackgroundSample(ppu, KdcHudSample, NULL);
        if (on_top && hud.multi && hud.expanded) hud.top_strip = true;
    }
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
        *tile = Map(ppu, column == 2 ? 29 : column, y < 168 ? 22 : 26) ^ (column == 2 ? 0x4000 : 0);
        *px = dx & 7; *py = y & 7;
    } else if (column != 3) {
        *tile = Map(ppu, column, 23); *px = dx & 7; *py = y & 7;
    }
    /* Keep the complete name drawing and counters, including the drawing's
     * lower pink border. Stock occupies a separate row after a clear gap. */
    if (dx >= 8 && dx < 112 && y >= 167 && y < 193) {
        int sx = 56 + dx - 8, sy = y + 16;
        *tile = Map(ppu, sx >> 3, sy >> 3); *px = sx & 7; *py = sy & 7;
    }
    if (dx >= 24 && dx < 96 && y >= 200 && y < 208) {
        int sx = 160 + dx - 24;
        *tile = Map(ppu, sx >> 3, 25); *px = sx & 7; *py = y & 7;
    }
}

/* 4px end caps for the split 2P cards. In the USA ROM's BG3 HUD charset,
 * the 4 cap columns of tiles $0E7 (row 21 cols 4/27) and $0F7 (row 26
 * cols 5/26) are 100% opaque (2bpp color indices 1 and 2, zero transparent
 * pixels), so the window mask starts 4px inward at the white interior. */
static void MultiCap(const Ppu *ppu, bool right_cap, int dx, int y, int y0, int y1,
                     uint16_t *tile, int *px, int *py)
{
    *px = (right_cap ? 4 : 0) + dx;
    if (y < y0 + 4) {
        *tile = Map(ppu, right_cap ? 27 : 4, 21);
        *py = y - y0;
    } else if (y >= y1 - 4) {
        *tile = Map(ppu, right_cap ? 26 : 5, 26);
        *py = 4 + (y - (y1 - 4));
    } else {
        *tile = Map(ppu, right_cap ? 26 : 5, 26);
        *py = y & 3;
    }
}

bool KdcHudSample(const Ppu *ppu, unsigned layer, int x, int y,
                  uint16_t *tile, int *px, int *py, void *context)
{
    (void)context;
    if (layer != 2 || !KdcHudActive(ppu)) return false;
    /* Suppress only the original expanded status band; shot art is separate.
     * Native screen y here is the SNES scanline, including its +1 phase. */
    if (hud.expanded && y >= (hud.multi ? 168 : 176) && y < kKdcHudCardY1)
        *tile = Clean(x, y, px, py);
    if (!hud.expanded) return true;
    int left = (hud.multi ? 16 : 20) + hud.shift[0];
    int right = 208 + hud.shift[1];
    int sx = -1, sy = y;
    if (!hud.multi) {
        /* Original portrait enclosure + energy tiles. Stock is removed from
         * this extension and reattached below the original identity/counters. */
        int dx = x - (left - 4);
        if (y >= 176 && y < kKdcHudCardY1 && dx >= 0 && dx < 120) {
            /* dx == 40 (left + 36, native sx = 56, tile col 7 px 0) is the
             * inner-left edge of the white energy window (y = 180..211). Its
             * top and bottom pixels (y = 180 at row 22 py 4, y = 211 at row 26
             * py 3) carry the 1px purple corner rounding matching dx = 111
             * (tile col 28 px 7), whereas rows 181..210 of tile col 7 hold the
             * player name box border and must sample the energy area at sx = 160. */
            sx = (dx < 40 || (dx == 40 && (y == 180 || y == 211))) ? 16 + dx : 160 + dx - 40;
            if (y >= 200 && y < 208 && dx >= 40 && dx < 112) {
                Padding(ppu, x, tile, px, py);
                return true;
            }
        }
        int edge = 256 + hud.extra - (hud.extra > 96 ? hud.extra - 96 : 0) - 8;
        dx = x - (edge - 120);
        if (dx >= 0 && dx < 120 && y >= kKdcHudCardY0 && y < kKdcHudCardY1) {
            SoloCard(ppu, dx, y, tile, px, py);
            return true;
        }
    } else {
        /* Energy retains the original pink/yellow frame; the original player
         * labels and POINT counters form a centered upper strip. */
        int dx = x - (left + 32);
        if (dx >= 0 && dx < 84 && y >= 176 && y < 200) {
            if (dx >= 80) {
                MultiCap(ppu, true, dx - 80, y, 176, 200, tile, px, py);
                return true;
            }
            sx = 48 + dx; sy = y + 16;
        }
        dx = x - (right - 84);
        if (dx >= 0 && dx < 84 && y >= 176 && y < 200) {
            if (dx < 4) {
                MultiCap(ppu, false, dx, y, 176, 200, tile, px, py);
                return true;
            }
            sx = 128 + dx - 4; sy = y + 16;
        }
        if (x >= kKdcHudMultiTopX0 && x < kKdcHudMultiTopX1 &&
            y >= kKdcHudMultiTopY0 && y < kKdcHudMultiTopY1) {
            if (x < kKdcHudMultiTopX0 + kKdcHudMultiTopPad ||
                x >= kKdcHudMultiTopX1 - kKdcHudMultiTopPad) {
                MultiCap(ppu, x >= kKdcHudMultiTopX1 - kKdcHudMultiTopPad,
                         x < kKdcHudMultiTopX0 + kKdcHudMultiTopPad
                             ? x - kKdcHudMultiTopX0
                             : x - (kKdcHudMultiTopX1 - kKdcHudMultiTopPad),
                         y, kKdcHudMultiTopY0, kKdcHudMultiTopY1, tile, px, py);
                return true;
            }
            if (y >= kKdcHudMultiTopY1 - kKdcHudMultiTopPad) {
                *tile = Map(ppu, 5, 21) ^ 0x8000;
                *px = x & 7; *py = 4 + (y - (kKdcHudMultiTopY1 - kKdcHudMultiTopPad));
                return true;
            }
            sx = x; sy = y + 160;
        }
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
    bool in_hud = false;
    if (hud.expanded) {
        if (hud.multi && y >= kKdcHudMultiTopY0 && y < kKdcHudMultiTopY1 &&
            x >= kKdcHudMultiTopX0 && x < kKdcHudMultiTopX1) {
            if (y >= kKdcHudMultiTopY0 + kKdcHudMultiTopPad &&
                y < kKdcHudMultiTopY1 - kKdcHudMultiTopPad &&
                x >= kKdcHudMultiTopX0 + kKdcHudMultiTopPad &&
                x < kKdcHudMultiTopX1 - kKdcHudMultiTopPad)
                return true;
            in_hud = true;
        } else if (y >= (hud.multi ? 168 : kKdcHudCardY0) && y < kKdcHudCardY1) {
            int left = (hud.multi ? 16 : 20) + hud.shift[0];
            int right = 208 + hud.shift[1];
            if (hud.multi) {
                if (y >= 180 && y < 196 &&
                    ((x >= left + 32 && x < left + 112) || (x >= right - 80 && x < right)))
                    return true;
                in_hud = y >= 176 && y < 200 &&
                    ((x >= left + 32 && x < left + 116) || (x >= right - 84 && x < right));
            } else {
                if ((y >= 180 && y < 212 && x >= left + 36 && x < left + 112) ||
                    (y >= 164 && y < 212 && x >= right - 80 && x < right + 32))
                    return true;
                in_hud = (y >= 176 && x >= left - 4 && x < left + 116) ||
                    (x >= right - 84 && x < right + 36);
            }
        }
    }
    if (!in_hud) {
        if (x < 0 || x >= 256 ||
            (hud.expanded && y >= (hud.multi ? 168 : 176) && y < kKdcHudCardY1))
            return false;
        uint16_t tile = Map(ppu, x >> 3, y >> 3);
        uint16_t clean = KdcReadWord(hud.ram, 0xdadc + 2 * (((y >> 3) & 31) * 32 + (x >> 3)));
        if (tile == clean) return false;
        int px = (tile & 0x4000) ? ((x & 7) ^ 7) : (x & 7);
        int py = (tile & 0x8000) ? ((y & 7) ^ 7) : (y & 7);
        unsigned bits = PpuRenderVram(ppu)[(0x4000 + (tile & 1023) * 8 + py) & 0x7fff] >> (7 - px);
        return ((bits & 1) | ((bits >> 7) & 2)) != 0;
    }
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
    return false;
}

void KdcPrepareHud(Ppu *ppu, const uint8_t *ram, int extra, bool enabled)
{
    hud.ram = ram; hud.extra = extra; hud.enabled = enabled;
    hud.active = false; hud.saw_top_lines = false; hud.top_strip = false;
    memset(ppu->presentationObjShift, 0, sizeof(ppu->presentationObjShift));
    ppu->presentationPrepare = enabled && extra ? PrepareLine : NULL;
    ppu->presentationContext = NULL;
    ppu->presentationWindows = enabled && extra ? Windows : NULL;
    PpuSetBackgroundSample(ppu, NULL, NULL);
}
