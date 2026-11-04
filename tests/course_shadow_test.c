#include "course_view.h"
#include "snes/ppu.h"
#include "snes/ws_shadow.h"
#include <assert.h>

int snes_frame_counter;
static uint8_t ram[0x20000];
/* Policy setter only; tile-provider dispatch uses the patched runtime. */
void PpuSetWidescreenLayerClamp(Ppu *ppu, uint8_t mask) { ppu->wsLayerClamp = mask; }
void PpuSetWidescreenBg3Widen(Ppu *ppu, uint8_t y) { ppu->wsBg3WidenY = y; }
void PpuWsSetOamRightHints(Ppu *ppu, const uint8_t *hints) { (void)ppu; (void)hints; }
void PpuWsSetOamLeftHints(Ppu *ppu, const uint8_t *hints) { (void)ppu; (void)hints; }
static void word(unsigned a, uint16_t v) { ram[a] = v; ram[a + 1] = v >> 8; }

int main(void)
{
    Ppu *p = calloc(1, sizeof(*p));
    assert(p);
    p->bgmode = 1;
    p->bgXsc[0] = 0x51;
    p->bgXsc[1] = 0x59;
    word(0x93b0 + 48, 1); word(0x9bb0 + 48, 200);
    word(0x13000 + 110, 0x1234); /* row 24, column 56 */
    WsShadowReset();
    KdcPrepareCourseView(p, ram, 272);
    assert(WsShadowHasTileProvider(0));
    /* Scenery is a separate 32x32 source, including rows occupied by the
     * native HUD. Cover every margin, fine-scroll phase and wrapped row. */
    p->bgXsc[2] = 0x3c;
    p->bgTileAdr = 0x400;
    for (unsigned i = 0; i < 1024; ++i) word(0xdadc + 2 * i, i | 0xa400);
    assert(WsShadowHasTileProvider(2));
    for (int scroll = 0; scroll < 8; ++scroll) {
        for (int x = -272; x < 528; ++x) {
            unsigned row = 1023;
            int chunk = 8 - (((unsigned)x + scroll) & 7);
            if (x < 256 && x + chunk > 0)
                assert(WsShadowResolveTile8x8(2, x, row, scroll, 0, 0x1234) == 0x1234);

        }
    }
    const struct { int x, column; bool flip; } seams[] = {
        {-264, 31, false}, {-256, 31, true}, {-8, 0, true},
        {256, 31, true}, {504, 0, true}, {512, 0, false},
    };
    for (unsigned i = 0; i < sizeof(seams) / sizeof(seams[0]); ++i) {
        uint16_t tile = 0xa400 | (22 * 32 + seams[i].column);
        assert(WsShadowResolveTile8x8(2, seams[i].x, 176, 0, 0, 0x1234) ==
               (tile ^ (seams[i].flip ? 0x4000 : 0)));
    }
    p->bgXsc[2] = 0x40; /* Unrecognised map: leave hardware data alone. */
    assert(WsShadowResolveTile8x8(2, -8, 8, 0, 0, 0x1234) == 0x1234);
    p->bgXsc[2] = 0x3c;
    p->bgTileAdr = 0;
    /* A camera above Y=0 wraps in the PPU's 10-bit scroll register. The
     * next-frame camera may already be zero: use it only to select the
     * 1024-pixel period, preserving the live raster's fine scroll. */
    word(0x93b0 + 2, 1); word(0x9bb0 + 2, 200);
    word(0x13000 + 110, 0x1234);
    p->vScroll[0] = 1023;
    assert(WsShadowResolveTile8x8(0, -8, 1039, 460, 0, 0) == 0x1234);
    assert(WsShadowResolveTile8x8(0, -8, 1023, 460, 0, 0xffff) == 0);
    p->vScroll[0] = 0;
    p->vScroll[1] = 1023;
    word(0x19800 + 110, 0x2345);
    assert(WsShadowResolveTile8x8(1, -8, 1039, 460, 0, 0) == 0x2345);
    p->vScroll[1] = 0;
    /* Likewise recover wrapped negative X, without treating valid positive
     * scrolls above 511 as negative. Support origins beyond one PPU period. */
    word(0x86, 0);
    word(0x13000 + 62, 0x3456); /* row 1, column 32 */
    assert(WsShadowResolveTile8x8(0, 264, 8, 1023, 0, 0xffff) == 0x3456);
    word(0x86, 1024);
    word(0x13000 + 252, 0x4567); /* row 1, column 127 */
    assert(WsShadowResolveTile8x8(0, -8, 8, 0, 0, 0) == 0x4567);
    word(0x86, 0);
    assert(PpuWidescreenLayerExtra(p, 2, 1, 272) == 272);
    word(0x86, 600);
    word(0x13000 + 146, 0x5678); /* row 1, column 74 */
    assert(WsShadowResolveTile8x8(0, -8, 8, 600, 0, 0) == 0x5678);
    word(0x86, 0);
    /* Live raster scroll 460/196; WRAM's pending camera is deliberately 0. */
    assert(WsShadowResolveTile8x8(0, -8, 196, 460, 0, 0xabcd) == 0x1234);
    /* Preserve whole tiles straddling either original edge without history. */
    assert(WsShadowResolveTile8x8(0, -4, 196, 460, 0, 0xabcd) == 0xabcd);
    assert(WsShadowResolveTile8x8(0, 252, 196, 460, 0, 0x4567) == 0x4567);
    assert(WsShadowResolveTile8x8(0, 0, 196, 460, 0, 0x5678) == 0x5678);
    word(0x13000 + 110, 0x2345);
    assert(WsShadowResolveTile8x8(0, -8, 196, 460, 0, 0) == 0x2345);
    /* Per-line vertical scroll and high rows bypass shadow-store capacity. */
    word(0x93b0 + 2000, 1); word(0x9bb0 + 2000, 200);
    assert(WsShadowResolveTile8x8(0, -8, 8000, 460, 0, 0) == 0x2345);
    assert(WsShadowResolveTile8x8(0, -8, 65528, 460, 0, 0xffff) == 0);
    assert(WsShadowResolveTile8x8(0, -8, 196, 0, 0, 0xffff) == 0);
    assert(WsShadowResolveTile8x8(0, -7, 196, 0, 0, 0xffff) == 0); /* clipped margin tile */
    p->mosaic = 0x11;
    assert(WsShadowResolveTile8x8(0, -8, 196, 460, 0, 0x9876) == 0x9876);
    p->mosaic = 0;
    /* A mid-raster scene signature change cannot read stale course data. */
    p->bgXsc[0] = 0x50;
    assert(WsShadowResolveTile8x8(0, -8, 196, 460, 0, 0x6789) == 0x6789);
    KdcPrepareCourseView(p, ram, 43);
    assert(p->wsLayerClamp == 1);
    assert(PpuWidescreenLayerExtra(p, 2, 1, 43) == 0);
    KdcPrepareCourseView(p, ram, 0);
    assert(!WsShadowHasTileProvider(0));
    p->bgXsc[0] = 0x51;
    KdcPrepareCourseView(p, ram, 43);
    WsShadowReset();
    assert(!WsShadowHasTileProvider(0));
    /* Course select streams a 3x3-page island and cloud plane. */
    p->bgmode = 9;
    p->bgXsc[0] = 0x51; p->bgXsc[1] = 0x59; p->bgXsc[2] = 0x61;
    p->bgTileAdr = 0x230;
    word(0x86, 344); word(0x88, 328);
    p->vScroll[0] = p->vScroll[1] = 328;
    /* Screen -8 at scroll344 is world tile42, row41: page4. */
    unsigned select_offset = 2 * (4 * 1024 + 9 * 32 + 10);
    word(0x10000 + select_offset, 0x8123);
    word(0x15000 + select_offset, 0x4567);
    KdcPrepareCourseView(p, ram, 272);
    assert(p->wsLayerClamp == 0);
    assert(PpuWidescreenLayerExtra(p, 2, 1, 272) == 272);
    assert(WsShadowResolveTile8x8(0, -8, 328, 344, 0, 0xffff) == 0x8123);
    assert(WsShadowResolveTile8x8(1, -8, 328, 344, 0, 0xffff) == 0x4567);
    /* Cloud animation swaps character banks without changing the maps. */
    p->bgTileAdr = 0x240;
    KdcPrepareCourseView(p, ram, 272);
    assert(p->wsLayerClamp == 0);
    assert(WsShadowResolveTile8x8(1, -8, 328, 344, 0, 0xffff) == 0x4567);
    /* The native course label must not appear in the sky margins. */
    p->vram[0x6000 + 22 * 32 + 31] = 0x1234;
    assert(WsShadowResolveTile8x8(2, 256, 176, 256, 0, 0xffff) == (0x1234 ^ 0x4000));
    assert(WsShadowResolveTile8x8(2, 0, 176, 256, 0, 0xffff) == 0xffff);
    /* Leaving selection restores bounded menu policy. */
    p->bgXsc[2] = 0x60;
    KdcPrepareCourseView(p, ram, 272);
    assert(p->wsLayerClamp == 1);
    assert(PpuWidescreenLayerExtra(p, 2, 1, 272) == 0);
    KdcPrepareCourseView(p, ram, 0);
    assert(p->wsLayerClamp == 0);
    assert(!WsShadowHasTileProvider(2));
    p->bgXsc[2] = 0x3c; p->bgTileAdr = 0x400;
    KdcPrepareCourseView(p, ram, 43);
    assert(p->wsLayerClamp == 0);
    assert(PpuWidescreenLayerExtra(p, 2, 1, 43) == 43);
    free(p);
    return 0;
}
