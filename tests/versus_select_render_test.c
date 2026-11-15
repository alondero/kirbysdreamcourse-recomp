/* ROM-free reproduction of the USA 2P course picker's three planes. */
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
    p->bgmode = 9;
    p->bgXsc[0] = 0x20; p->bgXsc[1] = 0x28; p->bgXsc[2] = 0x50;
    p->bgTileAdr = 0x400;
    p->screenEnabled[0] = 0x17;
    for (unsigned i = 0; i < 1024; ++i) {
        p->vram[0x2000 + i] = 1; /* Card borders. */
        p->vram[0x2800 + i] = 1; /* Course artwork and lettering. */
        /* BG3 combines a clean gradient column with a bounded score panel. */
        p->vram[0x5000 + i] = (i & 31) ? 1 : 2 + ((i / 32) & 1);
    }
    for (unsigned y = 0; y < 8; ++y) {
        p->vram[16 + y] = 0x00ff;
        p->vram[0x4008 + y] = 0xffff;
        p->vram[0x4010 + y] = 0x00ff;
        p->vram[0x4018 + y] = 0xff00;
    }
    const int widths[] = {0, 1, 43, 96, 213, 272};
    for (unsigned w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w) {
        int extra = widths[w];
        PpuSetExtraSpace(p, extra);
        for (int scroll = 0; scroll < 8; ++scroll) {
            for (unsigned layer = 0; layer < 3; ++layer)
                p->hScroll[layer] = scroll;
            KdcPrepareCourseView(p, ram, extra);
            for (int line = 1; line <= 224; ++line) {
                for (unsigned layer = 0; layer < 3; ++layer) {
                    PpuPixelPrioBufs native, wide;
                    uint8_t clamp = p->wsLayerClamp, widen = p->wsBg3WidenY;
                    p->wsLayerClamp = 7;
                    ClearBackdrop(p, &native);
                    if (layer == 2)
                        PpuDrawBackground_2bpp(p, &native, line, false, layer, 0xb100, 0x7100);
                    else
                        PpuDrawBackground_4bpp(p, &native, line, false, layer, 0xb100, 0x7100);
                    p->wsLayerClamp = clamp; p->wsBg3WidenY = widen;
                    ClearBackdrop(p, &wide);
                    if (layer == 2)
                        PpuDrawBackground_2bpp(p, &wide, line, false, layer, 0xb100, 0x7100);
                    else
                        PpuDrawBackground_4bpp(p, &wide, line, false, layer, 0xb100, 0x7100);
                    assert(memcmp(native.data + kPpuExtraLeftRight,
                                  wide.data + kPpuExtraLeftRight,
                                  256 * sizeof(PpuZbufType)) == 0);
                    for (int x = -extra; x < 256 + extra; ++x) {
                        if (x >= 0 && x < 256) continue;
                        PpuZbufType pixel = wide.data[kPpuExtraLeftRight + x];
                        if (layer < 2) {
                            /* No repeated course cards, labels or portraits. */
                            assert(pixel == native.data[kPpuExtraLeftRight + x]);
                        } else {
                            int start = x - (((unsigned)x + scroll) & 7);
                            if (start < 256 && start + 8 > 0) continue;
                            /* Full-width gradient, without the score panel. */
                            assert(pixel == (PpuZbufType)(0x7101 + ((line / 8) & 1)));
                        }
                    }
                }
            }
        }
    }
    /* Leaving this scene, or disabling widescreen, restores normal policy. */
    KdcPrepareCourseView(p, ram, 0);
    assert(p->wsLayerClamp == 0 && p->wsBg3WidenY == 0);
    p->bgXsc[2] = 0x54;
    KdcPrepareCourseView(p, ram, 43);
    assert(p->wsLayerClamp == 1 && p->wsBg3WidenY == 0);
    free(p);
    return 0;
}
