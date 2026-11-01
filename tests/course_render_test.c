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
    free(p);
    return 0;
}
