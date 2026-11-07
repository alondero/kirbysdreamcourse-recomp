/* Synthetic maps and planar graphics only; exercises the real PPU drawer. */
#include "course_hud.h"
#include "course_view.h"
#include "snes/ppu.c"
#include <assert.h>

Snes *g_snes;
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;
static uint8_t ram[0x20000], before_ram[0x20000];
static uint16_t before_vram[0x8000], before_oam[256];

static bool Pass(const Ppu *p, unsigned l, int x, int y, uint16_t *t, int *px, int *py, void *ctx)
{ (void)p; (void)l; (void)x; (void)y; (void)t; (void)px; (void)py; (void)ctx; return false; }

static void Portrait(Ppu *p, bool multi)
{
    memset(p->oam, 0, sizeof(p->oam));
    memset(p->highOam, 0, sizeof(p->highOam));
    for (unsigned player = 0; player < (multi ? 2u : 1u); ++player)
        for (unsigned i = 0; i < 4; ++i) {
            unsigned slot = player * 4 + i;
            int x = multi ? (player ? 208 : 16) : 20;
            int y = multi ? 171 : 179;
            p->oam[slot * 2] = x + (i & 1) * 16 + ((y + (i >> 1) * 16) << 8);
            p->oam[slot * 2 + 1] = (player ? 0x35a8 : 0x33a0) + i * 2;
            p->highOam[slot / 4] |= 2 << ((slot & 3) * 2);
        }
}

int main(void)
{
    Ppu *p = calloc(1, sizeof(*p));
    assert(p);
    p->bgmode = 9; p->bgXsc[0] = 0x51; p->bgXsc[1] = 0x59;
    p->bgXsc[2] = 0x3c; p->bgTileAdr = 0x400;
    p->screenEnabled[0] = 23; p->screenWindowed[0] = 19;
    p->inidisp = 15; p->obsel = 3;
    p->windowsel = 0x220000; p->cgwsel = 0x10;
    p->cgadsub = 0x33; p->fixedColor = 0x7fff;
    p->window1left = 128; p->window1right = 127;
    p->renderFlags = kPpuRenderFlags_NewRenderer;
    for (unsigned i = 0; i < 1024; ++i) {
        uint16_t tile = 1 + i % 10 + ((i & 7) << 10) + ((i & 3) << 14);
        p->vram[0x3c00 + i] = tile;
        ram[0xdadc + i * 2] = tile; ram[0xdadc + i * 2 + 1] = tile >> 8;
    }
    for (unsigned i = 0; i < 88; ++i) p->vram[0x4000 + i] = i * 1937;
    /* Installing a no-op sampler must retain the optimized drawer's colors,
     * flips, clipping and priority at every pixel, including reflected sides. */
    PpuSetExtraSpace(p, 96);
    KdcPrepareCourseView(p, ram, 96);
    for (unsigned y = 1; y < 224; y += 7) {
        PpuPixelPrioBufs a, b;
        PpuSetBackgroundSample(p, NULL, NULL);
        ClearBackdrop(p, &a);
        PpuDrawBackground_2bpp(p, &a, y, false, 2, 0xf200, 0x1200);
        PpuSetBackgroundSample(p, Pass, NULL);
        ClearBackdrop(p, &b);
        PpuDrawBackground_2bpp(p, &b, y, false, 2, 0xf200, 0x1200);
        for (int i = kPpuExtraLeftRight - 96; i < kPpuExtraLeftRight + 256 + 96; ++i) if (a.data[i] != b.data[i]) { fprintf(stderr, "y=%u x=%d native=%x sample=%x\n", y, i-kPpuExtraLeftRight, a.data[i], b.data[i]); return 1; }
    }
    for (unsigned multi = 0; multi < 2; ++multi) {
        ram[0x17ff] = multi;
        Portrait(p, multi != 0);
        unsigned border = 0x3c00 + (multi ? 21 * 32 + 4 : 22 * 32 + 2);
        p->vram[border] = multi ? 0x34e7 : 0x3519;
        p->window1left = multi ? 48 : 56; p->window1right = multi ? 207 : 235;
        p->vram[0x3c00 + 23 * 32 + 20] = 0x3520;
        p->vram[0x3c00 + 23 * 32 + 13] = 0x3d0e;
        p->vram[0x3c00 + 25 * 32 + 20] = 0x3d3c;
        p->vram[0x3c00 + 25 * 32 + 6] = 0x3d40;
        memcpy(before_ram, ram, sizeof(ram));
        memcpy(before_vram, p->vram, sizeof(p->vram));
        memcpy(before_oam, p->oam, sizeof(p->oam));
        const int widths[] = {0, 42, 43, 96, 213, 272};
        for (unsigned w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w) {
            int extra = widths[w];
            PpuSetExtraSpace(p, extra);
            KdcPrepareHud(p, ram, extra, true);
            if (p->presentationPrepare) p->presentationPrepare(p, 185, NULL);
            assert(KdcHudActive(p) == (extra >= 43));
            if (extra < 43) continue;
            int left = 12 - (extra > 96 ? 96 : extra);
            assert(p->presentationObjShift[0] == left - (multi ? 16 : 20));
            assert(!p->presentationObjShift[12]); /* Unrelated/shot OAM. */
            int expanded_shift = p->presentationObjShift[0];
            /* Normal menu-to-course entry leaves window 2 at 255..0;
             * developer starts leave 0..0. The qualified selector never
             * enables window 2, so neither range may disable edge HUD. */
            p->window2left = 255;
            p->presentationPrepare(p, 185, NULL);
            assert(KdcHudActive(p));
            assert(p->presentationObjShift[0] == expanded_shift);
            p->windowsel |= 8; /* An enabled window 2 is still unsupported. */
            p->presentationPrepare(p, 185, NULL);
            assert(!KdcHudActive(p));
            p->windowsel &= ~8;
            p->window2left = 0;
            p->presentationPrepare(p, 185, NULL);
            uint16_t sampled = 0; int tx = 0, ty = 0;
            KdcHudSample(p, 2, left + (multi ? 32 : 36), 184, &sampled, &tx, &ty, NULL);
            assert(sampled == (multi ? 0x3d40 : 0x3520) && tx == 0 && ty == 0);
            if (multi) {
                KdcHudSample(p, 2, 32, 8, &sampled, &tx, &ty, NULL);
                assert(sampled == 0x34e7);
            } else {
                int edge = 256 + (extra > 96 ? 96 : extra) - 8;
                /* Empty space is transparent over the white window, never
                 * the yellow HOLE/SHOT fill. The card gets its own outer cap. */
                KdcHudSample(p, 2, left + 36, 200, &sampled, &tx, &ty, NULL);
                assert(sampled == p->vram[0x3c00 + 22 * 32 + 3] && ty == 4);
                KdcHudSample(p, 2, edge - 4, 184, &sampled, &tx, &ty, NULL);
                assert(sampled == p->vram[0x3c00 + 23 * 32 + 29]);
                KdcHudSample(p, 2, edge - 120 + 8 + 48, 168, &sampled, &tx, &ty, NULL);
                assert(sampled == 0x3d0e);
                KdcHudSample(p, 2, edge - 120 + 24, 200, &sampled, &tx, &ty, NULL);
                assert(sampled == 0x3d3c);
            }
            sampled = 0x4567; tx = 3; ty = 4;
            KdcHudSample(p, 2, 128, 130, &sampled, &tx, &ty, NULL);
            assert(sampled == 0x4567 && tx == 3 && ty == 4);
            PpuWindows window;
            PpuWindows_Calc(&window, p, 0, 185);
            for (unsigned i = 0; i < window.nr; ++i)
                if (128 >= window.edges[i] && 128 < window.edges[i + 1])
                    assert(!(window.bits & (1u << i)));
            /* The game removing its border is the sole quiet transition. */
            p->vram[border] = 0;
            p->window1left = 128; p->window1right = 127;
            p->presentationPrepare(p, 185, NULL);
            assert(KdcHudActive(p) && p->presentationObjShift[0] == expanded_shift);
            uint16_t tile = 0x4567; int px = 3, py = 4;
            KdcHudSample(p, 2, 128, 130, &tile, &px, &py, NULL);
            assert(tile == 0x4567 && px == 3 && py == 4);
            p->vram[border] = multi ? 0x34e7 : 0x3519;
        p->window1left = multi ? 48 : 56; p->window1right = multi ? 207 : 235;
            p->inidisp = 14;
            p->presentationPrepare(p, 185, NULL);
            assert(!KdcHudActive(p) && !p->presentationObjShift[0]);
            p->inidisp = 15;
            p->windowsel ^= 2; /* unrelated terrain window must retain native rendering */
            p->presentationPrepare(p, 185, NULL);
            assert(!KdcHudActive(p) && !p->presentationObjShift[0]);
            p->windowsel ^= 2;
        }
        assert(!memcmp(before_ram, ram, sizeof(ram)));
        assert(!memcmp(before_vram, p->vram, sizeof(p->vram)));
        assert(!memcmp(before_oam, p->oam, sizeof(p->oam)));
        /* Missing quartet and non-course screens must fail closed. */
        p->oam[1] = 0;
        KdcPrepareHud(p, ram, 96, true); p->presentationPrepare(p, 185, NULL);
        assert(!KdcHudActive(p));
        Portrait(p, multi != 0); p->screenEnabled[0] = 4;
        p->presentationPrepare(p, 185, NULL); assert(!KdcHudActive(p));
        p->screenEnabled[0] = 23;
    }
    KdcPrepareHud(p, ram, 96, false);
    assert(!p->presentationPrepare && !p->presentationWindows && !p->backgroundSample);
    free(p);
    return 0;
}
