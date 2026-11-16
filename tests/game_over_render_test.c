/* ROM-free Game Over layout, exercising the real background drawer. */
#include "course_view.h"
#include "snes/ppu.c"
#include "snes/ws_shadow.h"
#include <assert.h>

Snes *g_snes;
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;
static uint8_t ram[0x20000];

static void Lettering(Ppu *p, bool enabled)
{
    for (unsigned row = 12; row <= 13; ++row)
        for (unsigned col = 8; col < 24; ++col)
            p->vram[0x5000 + row * 32 + col] = enabled ? 63 :
                (1 + (col & 15)) | ((row & 3) << 10) |
                ((col & 1) ? 0x4000 : 0) | ((row & 1) ? 0x8000 : 0);
}

int main(void)
{
    Ppu *p = calloc(1, sizeof(*p));
    assert(p);
    p->bgmode = 9;
    p->bgXsc[0] = 0x52; p->bgXsc[1] = 0x58; p->bgXsc[2] = 0x3c;
    p->bgTileAdr = 0x400;
    p->screenEnabled[0] = 1; p->screenEnabled[1] = 17;
    for (unsigned i = 0; i < 2048; ++i) {
        unsigned col = i & 31, row = i / 32;
        p->vram[0x5000 + i] = (1 + (col & 15)) | ((row & 3) << 10) |
            ((col & 1) ? 0x4000 : 0) | ((row & 1) ? 0x8000 : 0);
    }
    for (unsigned tile = 1; tile <= 63; ++tile)
        for (unsigned y = 0; y < 8; ++y) {
            p->vram[tile * 16 + y] = (uint16_t)(tile * 1937 + y * 257);
            p->vram[tile * 16 + 8 + y] = (uint16_t)(tile * 379 + y * 997);
        }
    const int widths[] = {0, 1, 43, 96, 213, 272};
    for (unsigned w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w) {
        int extra = widths[w];
        PpuSetExtraSpace(p, extra);
        for (int fine = 0; fine < 8; ++fine) {
            p->hScroll[0] = fine;
            p->vScroll[0] = fine;
            for (int line = 1; line <= 224; ++line) {
                PpuPixelPrioBufs wallpaper, native, actual;
                Lettering(p, false);
                WsShadowSetTileProvider(0, NULL, NULL);
                PpuSetWidescreenLayerClamp(p, 0);
                ClearBackdrop(p, &wallpaper);
                PpuDrawBackground_4bpp(p, &wallpaper, line, false, 0, 0xb100, 0x7100);
                Lettering(p, true);
                PpuSetWidescreenLayerClamp(p, 1);
                ClearBackdrop(p, &native);
                PpuDrawBackground_4bpp(p, &native, line, false, 0, 0xb100, 0x7100);
                KdcPrepareCourseView(p, ram, extra);
                assert(!KdcIsCourse(p));
                for (int sub = 0; sub < 2; ++sub) {
                    ClearBackdrop(p, &actual);
                    PpuDrawBackground_4bpp(p, &actual, line, sub != 0, 0, 0xb100, 0x7100);
                    for (int x = -extra; x < 256 + extra; ++x) {
                        bool center = x >= 0 && x < 256;
                        assert(actual.data[kPpuExtraLeftRight + x] ==
                            (center ? native : wallpaper).data[kPpuExtraLeftRight + x]);
                    }
                }
            }
        }
    }
    /* The provider is inert during mosaic and on unrelated layers. */
    KdcPrepareCourseView(p, ram, 43);
    p->mosaic = 0x21;
    assert(WsShadowResolveTile8x8(0, 256, 97, 0, 0, 0x6789) == 0x6789);
    p->mosaic = 0;
    assert(WsShadowResolveTile8x8(1, 256, 97, 0, 0, 0x6789) == 0x6789);
    assert(WsShadowResolveTile8x8(2, 256, 97, 0, 0, 0x6789) == 0x6789);
    KdcPrepareCourseView(p, ram, 0);
    assert(p->wsLayerClamp == 0 && !WsShadowHasTileProvider(0));
    /* Nearby results/preview layouts and unsupported tile modes stay bounded. */
    p->bgXsc[1] = 0x59;
    KdcPrepareCourseView(p, ram, 43);
    assert(PpuWidescreenLayerExtra(p, 0, 97, 43) == 0);
    p->bgXsc[1] = 0x58; p->bgmode |= 0x10;
    KdcPrepareCourseView(p, ram, 43);
    assert(PpuWidescreenLayerExtra(p, 0, 97, 43) == 0);
    free(p);
    return 0;
}
