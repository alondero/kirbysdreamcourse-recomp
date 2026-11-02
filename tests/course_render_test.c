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
    /* Course BG3 is decorative scenery, not the object-based HUD. Exercise
     * the real 2bpp drawer at both margins without changing center pixels. */
    p->bgmode = 9;
    p->bgXsc[0] = 0x51;
    p->bgXsc[1] = 0x59;
    p->bgXsc[2] = 0x3c;
    p->bgTileAdr = 0x400;
    p->screenEnabled[0] = 4;
    for (int i = 0; i < 1024; ++i) p->vram[0x3c00 + i] = 1;
    for (int i = 0; i < 8; ++i) p->vram[0x4008 + i] = 0xffff;
    for (int extra = 43; extra <= 272; extra += 229) {
        PpuPixelPrioBufs baseline, actual;
        PpuSetExtraSpace(p, extra);
        PpuSetWidescreenBg3Widen(p, 0);
        ClearBackdrop(p, &baseline);
        PpuDrawBackground_2bpp(p, &baseline, 1, false, 2, 0xb100, 0x7100);
        KdcPrepareCourseView(p, ram, extra);
        ClearBackdrop(p, &actual);
        PpuDrawBackground_2bpp(p, &actual, 1, false, 2, 0xb100, 0x7100);
        assert(memcmp(baseline.data + kPpuExtraLeftRight,
                      actual.data + kPpuExtraLeftRight,
                      256 * sizeof(PpuZbufType)) == 0);
        for (int x = -extra; x < 256 + extra; ++x)
            assert(actual.data[kPpuExtraLeftRight + x] == 0x7103);
    }
    KdcPrepareCourseView(p, ram, 0);
    assert(PpuWidescreenLayerExtra(p, 2, 1, 43) == 0);
    free(p);
    return 0;
}
