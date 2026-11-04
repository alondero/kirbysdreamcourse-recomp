/* Exercise the real 16x16 drawer: provider registration must not activate
 * its world-history pixel-phase adjustment on non-course backgrounds. */
#include "course_view.h"
#include "snes/ppu.c"
#include <assert.h>

Snes *g_snes;
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;
static uint8_t ram[0x20000];

int main(void)
{
    Ppu *p = calloc(1, sizeof(*p));
    assert(p);
    p->bgmode = 0x21; /* Mode 1, BG2 uses 16x16 tiles. */
    p->screenEnabled[0] = 2;
    p->bgXsc[1] = 0x08;
    p->bgTileAdr = 0x10;
    p->vScroll[1] = 5;
    for (int i = 0; i < 1024; ++i) p->vram[0x800 + i] = i & 31;
    for (int i = 0; i < 1024; ++i) p->vram[0x1000 + i] = (uint16_t)(i * 257 + 0x1234);
    for (int scroll = 1; scroll < 8; ++scroll) {
        PpuPixelPrioBufs baseline, actual;
        p->hScroll[1] = scroll;
        PpuSetExtraSpace(p, 43);
        WsShadowReset();
        ClearBackdrop(p, &baseline);
        PpuDrawBackgroundBig(p, &baseline, 7, false, 1, 4, 0xb100, 0x7100, false);
        KdcPrepareCourseView(p, ram, 43);
        assert(WsShadowHasTileProvider(1) && !WsShadowLayerActive(1));
        ClearBackdrop(p, &actual);
        PpuDrawBackgroundBig(p, &actual, 7, false, 1, 4, 0xb100, 0x7100, false);
        assert(memcmp(baseline.data + kPpuExtraLeftRight - 43,
                      actual.data + kPpuExtraLeftRight - 43,
                      342 * sizeof(PpuZbufType)) == 0);
    }
    PpuSetExtraSpace(p, 272);
    p->oam[0] = 248;
    p->highOam[0] = 1; /* 9-bit X=504 is -8 on the original hardware. */
    KdcPrepareCourseView(p, ram, 272);
    assert(PpuDecodeOamX(p, 0) == -8);
    assert(PpuWidescreenOamLeftHintAllows(p, 0, -8, 16, 272));
    assert(!PpuWidescreenOamLeftHintAllows(p, 0, -256, 16, 272));
    /* Course BG3 combines decorative scenery and HUD tiles. Exercise
     * the real 2bpp drawer at both margins without changing center pixels. */
    p->bgmode = 9;
    p->bgXsc[0] = 0x51;
    p->bgXsc[1] = 0x59;
    p->bgXsc[2] = 0x3c;
    p->bgTileAdr = 0x400;
    p->screenEnabled[0] = 4;
    for (int i = 0; i < 1024; ++i) p->vram[0x3c00 + i] = 1;
    for (int i = 0; i < 1024; ++i) {
        ram[0xdadc + 2 * i] = 2; /* Clean scenery differs from the HUD map. */
        ram[0xdadc + 2 * i + 1] = 0;
    }
    for (int i = 0; i < 8; ++i) p->vram[0x4008 + i] = 0xffff;
    for (int i = 0; i < 8; ++i) p->vram[0x4010 + i] = 0x00ff;
    const int widths[] = {1, 43, 96, 213, 272};
    for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
        int extra = widths[i];
        for (int scroll = 0; scroll < 8; ++scroll) {
            PpuPixelPrioBufs baseline, actual;
            p->hScroll[2] = scroll;
            PpuSetExtraSpace(p, extra);
            WsShadowSetTileProvider(0, NULL, NULL);
            PpuSetWidescreenBg3Widen(p, 1);
            ClearBackdrop(p, &baseline);
            PpuDrawBackground_2bpp(p, &baseline, 185, false, 2, 0xb100, 0x7100);
            KdcPrepareCourseView(p, ram, extra);
            ClearBackdrop(p, &actual);
            PpuDrawBackground_2bpp(p, &actual, 185, false, 2, 0xb100, 0x7100);
            assert(memcmp(baseline.data + kPpuExtraLeftRight,
                          actual.data + kPpuExtraLeftRight,
                          256 * sizeof(PpuZbufType)) == 0);
            for (int x = -extra; x < 256 + extra; ++x) {
                /* Whole tiles overlapping the native field retain VRAM. */
                int tile_start = x - (((unsigned)x + scroll) & 7);
                bool native = tile_start < 256 && tile_start + 8 > 0;
                assert(actual.data[kPpuExtraLeftRight + x] ==
                       (native ? 0x7103 : 0x7101));
            }
        }
    }
    /* Non-tileable asymmetric artwork: every reflected pixel must match
     * its partner in the clean native strip, not the opposite map edge.
     * Include pre-flipped tiles, palette and vertical-flip attributes. */
    for (int i = 0; i < 1024; ++i) {
        uint16_t tile = (3 + (i & 31)) | ((i & 3) << 10) |
                        ((i & 1) ? 0xc000 : 0);
        p->vram[0x3c00 + i] = tile;
        ram[0xdadc + 2 * i] = tile;
        ram[0xdadc + 2 * i + 1] = tile >> 8;
    }
    for (int tile = 3; tile < 35; ++tile)
        for (int y = 0; y < 8; ++y)
            p->vram[0x4000 + tile * 8 + y] = (uint16_t)(tile * 1937 + y * 257);
    for (int scroll = 0; scroll < 1024; scroll += 17) {
        PpuPixelPrioBufs original, reflected;
        p->hScroll[2] = scroll;
        PpuSetExtraSpace(p, 272);
        WsShadowSetTileProvider(0, NULL, NULL);
        PpuSetWidescreenBg3Widen(p, 1);
        ClearBackdrop(p, &original);
        PpuDrawBackground_2bpp(p, &original, 73, false, 2, 0xb100, 0x7100);
        KdcPrepareCourseView(p, ram, 272);
        ClearBackdrop(p, &reflected);
        PpuDrawBackground_2bpp(p, &reflected, 73, false, 2, 0xb100, 0x7100);
        int left = -(scroll & 7);
        int span = (scroll & 7) ? 264 : 256;
        for (int x = -272; x < 528; ++x) {
            int offset = (x - left) % (2 * span);
            if (offset < 0) offset += 2 * span;
            int source = left + (offset < span ? offset : 2 * span - 1 - offset);
            assert(reflected.data[kPpuExtraLeftRight + x] ==
                   original.data[kPpuExtraLeftRight + source]);
        }
    }
    KdcPrepareCourseView(p, ram, 0);
    assert(PpuWidescreenLayerExtra(p, 2, 1, 43) == 0);
    /* Real 4bpp composition: a conspicuous synthetic frame occupies the
     * native title/intro maps. Only clean decorative tiles may reach the
     * margins; tiles straddling a native edge must remain authentic. */
    p->bgmode = 9;
    p->screenEnabled[0] = 2;
    p->vScroll[1] = 0;
    for (int y = 0; y < 8; ++y) {
        p->vram[16 + y] = 0xffff; /* Frame: colour 3. */
        p->vram[24 + y] = 0;
        p->vram[32 + y] = 0x00ff; /* Wallpaper: colour 1. */
        p->vram[40 + y] = 0;
    }
    for (int scene = 0; scene < 2; ++scene) {
        unsigned map = scene ? 0x5800 : 0x4800;
        p->bgXsc[0] = scene ? 0x50 : 0x40;
        p->bgXsc[1] = scene ? 0x58 : 0x48;
        p->bgXsc[2] = scene ? 0x3c : 0x58;
        p->bgTileAdr = scene ? 0x400 : 0x500;
        for (unsigned row = 0; row < 32; ++row)
            for (unsigned col = 0; col < 32; ++col) {
                bool frame = scene ? col >= 2 && col <= 29 && row >= 2 && row <= 20 :
                                     row >= 1 && row <= 16;
                p->vram[map + row * 32 + col] = frame ? 1 : 2;
            }
        for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
            int extra = widths[i];
            PpuSetExtraSpace(p, extra);
            for (int scroll = 0; scroll < 8; ++scroll) {
                p->hScroll[1] = scroll;
                for (int y = 1; y <= 224; ++y) {
                    PpuPixelPrioBufs original, decorated;
                    WsShadowSetTileProvider(0, NULL, NULL);
                    ClearBackdrop(p, &original);
                    PpuDrawBackground_4bpp(p, &original, y, false, 1, 0xb100, 0x7100);
                    KdcPrepareCourseView(p, ram, extra);
                    ClearBackdrop(p, &decorated);
                    PpuDrawBackground_4bpp(p, &decorated, y, false, 1, 0xb100, 0x7100);
                    assert(memcmp(original.data + kPpuExtraLeftRight,
                                  decorated.data + kPpuExtraLeftRight,
                                  256 * sizeof(PpuZbufType)) == 0);
                    for (int x = -extra; x < 256 + extra; ++x) {
                        int tile_start = x - (((unsigned)x + scroll) & 7);
                        if (tile_start < 256 && tile_start + 8 > 0) continue;
                        assert(decorated.data[kPpuExtraLeftRight + x] == 0x7101);
                    }
                }
            }
        }
    }
    free(p);
    return 0;
}
