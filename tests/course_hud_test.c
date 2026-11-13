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

static bool WindowMasked(Ppu *p, int x, int y)
{
    PpuWindows window;
    PpuWindows_Calc(&window, p, 0, y);
    for (unsigned i = 0; i < window.nr; ++i)
        if (x >= window.edges[i] && x < window.edges[i + 1])
            return (window.bits & (1u << i)) != 0;
    return false;
}

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
        p->vram[0x3c00 + 22 * 32 + 7] = 0x351a;
        p->vram[0x3c00 + 26 * 32 + 7] = 0x351b;
        p->vram[0x3c00 + 22 * 32 + 20] = 0x3521;
        p->vram[0x3c00 + 26 * 32 + 20] = 0x3522;
        p->vram[0x3c00 + 23 * 32 + 13] = 0x3d0e;
        p->vram[0x3c00 + 25 * 32 + 20] = 0x3d3c;
        p->vram[0x3c00 + 25 * 32 + 6] = 0x3d40;
        p->vram[0x3c00 + 28 * 32 + 16] = 0x34e7;
        for (unsigned row = 0; row < 8; ++row) {
            p->vram[0x4000 + (0x34e7 & 1023) * 8 + row] = 0xffff;
            /* Padding and outer rails Map(3, 22/26) have transparent interior rows. */
            p->vram[0x4000 + (p->vram[0x3c00 + 22 * 32 + 3] & 1023) * 8 + row] = 0;
            p->vram[0x4000 + (p->vram[0x3c00 + 26 * 32 + 3] & 1023) * 8 + row] = 0;
        }
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
                int right = 256 + (extra > 96 ? 96 : extra) - 44;
                KdcHudSample(p, 2, 44, 8, &sampled, &tx, &ty, NULL);
                assert(sampled == 0x34e7 && tx == 0 && ty == 0);
                KdcHudSample(p, 2, 208, 8, &sampled, &tx, &ty, NULL);
                assert(sampled == p->vram[0x3c00 + 21 * 32 + 27] && tx == 4 && ty == 0);
                KdcHudSample(p, 2, 64, 32, &sampled, &tx, &ty, NULL);
                assert(sampled == (p->vram[0x3c00 + 21 * 32 + 5] ^ 0x8000) && ty == 4);
                KdcHudSample(p, 2, left + 112, 184, &sampled, &tx, &ty, NULL);
                assert(sampled == p->vram[0x3c00 + 26 * 32 + 26] && tx == 4);
                KdcHudSample(p, 2, right - 84, 184, &sampled, &tx, &ty, NULL);
                assert(sampled == p->vram[0x3c00 + 26 * 32 + 5] && tx == 0);
                for (int cap_x = 44; cap_x < 48; ++cap_x)
                    assert(KdcHudOverlayPixel(p, cap_x, 8));
                assert(!WindowMasked(p, 128, 11) && !WindowMasked(p, 128, 32));
                assert(!WindowMasked(p, 47, 12) && WindowMasked(p, 48, 12));
                assert(WindowMasked(p, 207, 12) && !WindowMasked(p, 208, 12));
                assert(!WindowMasked(p, 47, 31) && WindowMasked(p, 48, 31));
                assert(WindowMasked(p, 207, 31) && !WindowMasked(p, 208, 31));
                assert(!WindowMasked(p, left + 31, 185) && WindowMasked(p, left + 32, 185));
                assert(WindowMasked(p, left + 35, 185) && WindowMasked(p, left + 108, 185));
                assert(WindowMasked(p, left + 111, 185) && !WindowMasked(p, left + 112, 185));
                assert(!WindowMasked(p, right - 81, 185) && WindowMasked(p, right - 80, 185));
                assert(WindowMasked(p, right - 77, 185) && WindowMasked(p, right - 4, 185));
                assert(WindowMasked(p, right - 1, 185) && !WindowMasked(p, right, 185));
                /* Direct KdcHudOverlayPixel checks on window-masked interior edges
                 * where the sampled BG3 tile equals clean (transparent without window). */
                assert(!KdcHudOverlayPixel(p, 48, 11) && !KdcHudOverlayPixel(p, 207, 11));
                assert(KdcHudOverlayPixel(p, 48, 12) && KdcHudOverlayPixel(p, 207, 12));
                assert(KdcHudOverlayPixel(p, 48, 31) && KdcHudOverlayPixel(p, 207, 31));
                assert(!KdcHudOverlayPixel(p, left + 32, 179) && KdcHudOverlayPixel(p, left + 32, 180));
                assert(KdcHudOverlayPixel(p, left + 111, 195) && !KdcHudOverlayPixel(p, left + 111, 196));
                assert(!KdcHudOverlayPixel(p, right - 80, 179) && KdcHudOverlayPixel(p, right - 80, 180));
                assert(KdcHudOverlayPixel(p, right - 1, 195) && !KdcHudOverlayPixel(p, right - 1, 196));
            } else {
                int edge = 256 + (extra > 96 ? 96 : extra) - 8;
                /* Empty space is transparent over the white window, never
                 * the yellow HOLE/SHOT fill. The card gets its own outer cap. */
                KdcHudSample(p, 2, left + 36, 200, &sampled, &tx, &ty, NULL);
                assert(sampled == p->vram[0x3c00 + 22 * 32 + 3] && ty == 4);
                KdcHudSample(p, 2, left + 36, 180, &sampled, &tx, &ty, NULL);
                assert(sampled == 0x351a && tx == 0 && ty == 4);
                KdcHudSample(p, 2, left + 36, 211, &sampled, &tx, &ty, NULL);
                assert(sampled == 0x351b && tx == 0 && ty == 3);
                KdcHudSample(p, 2, edge - 4, 184, &sampled, &tx, &ty, NULL);
                assert(sampled == p->vram[0x3c00 + 23 * 32 + 29]);
                KdcHudSample(p, 2, edge - 116, 165, &sampled, &tx, &ty, NULL);
                assert(sampled == (p->vram[0x3c00 + 22 * 32 + 29] ^ 0x4000));
                KdcHudSample(p, 2, edge - 120 + 8, 166, &sampled, &tx, &ty, NULL);
                assert(sampled == p->vram[0x3c00 + 22 * 32 + 3] && ty == 6);
                KdcHudSample(p, 2, edge - 120 + 8, 167, &sampled, &tx, &ty, NULL);
                assert(sampled == 0x351a && tx == 0 && ty == 7);
                KdcHudSample(p, 2, edge - 120 + 8, 192, &sampled, &tx, &ty, NULL);
                assert(sampled == 0x351b && tx == 0 && ty == 0);
                KdcHudSample(p, 2, edge - 120 + 8, 193, &sampled, &tx, &ty, NULL);
                assert(sampled == p->vram[0x3c00 + 22 * 32 + 3] && ty == 4);
                KdcHudSample(p, 2, edge - 120 + 8 + 48, 168, &sampled, &tx, &ty, NULL);
                assert(sampled == 0x3d0e);
                KdcHudSample(p, 2, edge - 120 + 24, 200, &sampled, &tx, &ty, NULL);
                assert(sampled == 0x3d3c);
                /* Direct KdcHudOverlayPixel checks on 1P transparent padding/clean
                 * pixels backed by the left energy and right SoloCard windows. */
                assert(!KdcHudOverlayPixel(p, left + 44, 179) && KdcHudOverlayPixel(p, left + 44, 180));
                assert(KdcHudOverlayPixel(p, left + 36, 200) && KdcHudOverlayPixel(p, left + 111, 200));
                assert(!KdcHudOverlayPixel(p, left + 112, 200));
                assert(KdcHudOverlayPixel(p, left + 111, 211) && !KdcHudOverlayPixel(p, left + 111, 212));
                assert(!KdcHudOverlayPixel(p, edge - 112, 163) && KdcHudOverlayPixel(p, edge - 112, 164));
                assert(KdcHudOverlayPixel(p, edge - 112, 195) && KdcHudOverlayPixel(p, edge - 9, 195));
                assert(KdcHudOverlayPixel(p, edge - 9, 211) && !KdcHudOverlayPixel(p, edge - 9, 212));
            }
            assert(!KdcHudOverlayPixel(p, 128, 100));
            assert(!KdcHudOverlayPixel(p, 128, 185));
            /* Drawn scanline 224 (bottom visible row) must still evaluate BG3 overlay tiles. */
            assert(KdcHudOverlayPixel(p, 128, 224));
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
