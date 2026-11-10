/* Exercise real raster output with synthetic actors, not interpolation math. */
#include "course_view.h"
#define main OrbitFixtureMain
#include "course_orbit_render_test.c"
#undef main

static double RedAt(double alpha)
{
    Fill(pixels, sizeof(pixels) / sizeof(*pixels));
    assert(KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, alpha));
    CheckPadding(pixels, width);
    assert(!memcmp(g_ram, saved_ram, sizeof(g_ram)));
    assert(!memcmp(&ppu, &saved_ppu, sizeof(ppu)));
    double x, y;
    assert(CountColor(pixels, red, &x, &y) > 0);
    return x;
}

static double HdRedAt(double alpha)
{
    enum { w = width * 4, h = height * 4, pitch = (w + padding) * 4 };
    static uint32_t output[(w + padding) * h];
    hd_enabled = true;
    Fill(output, sizeof(output) / sizeof(*output));
    assert(KdcOrbitDrawHd((uint8_t *)output, pitch, NULL, width, height, alpha, 4));
    double sum = 0;
    unsigned count = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x)
            if (output[y * (w + padding) + x] == red) { sum += x; ++count; }
        for (int x = w; x < w + padding; ++x) assert(output[y * (w + padding) + x] == 0xdeadbeef);
    }
    assert(count);
    assert(!memcmp(g_ram, saved_ram, sizeof(g_ram)));
    assert(!memcmp(&ppu, &saved_ppu, sizeof(ppu)));
    hd_enabled = false;
    return (sum / count - 1.5) / 4;
}

int main(void)
{
    assert(!OrbitFixtureMain());
    use_custom = true;
    custom_count = 1;
    custom[0] = Body(3, 2, 3, -8);
    custom[0].motion_id = 42;
    custom[0].piece_id = 100;
    Ball(3, 2, 0);
    Scroll(0, 0);
    Snapshot();
    KdcOrbitConfigure(true, true);
    KdcOrbitInput(0, 0, false);
    KdcOrbitCapture(1, width, height);
    double start = RedAt(1);
    assert(RedAt(0) == start); /* No history yet. */
    assert(fabs(HdRedAt(1) - start) < 0.5);
    assert(RedAt(1) == start); /* Changing output size invalidates the background cache. */
    custom[0].x += 0.5;
    KdcOrbitInput(0, 0, false);
    KdcOrbitCapture(2, width, height);
    double end = RedAt(1), middle = RedAt(0.5);
    assert(end > start + 10 && middle > start + 3 && middle < end - 3);
    assert(RedAt(0) == start);
    assert(fabs(HdRedAt(0.5) - middle) < 0.5);
    assert(RedAt(1) == end); /* Repeated presents do not advance the camera. */

    /* Actor movement survives an animation-layout change. The current
     * artwork stays current while its request origin travels smoothly. */
    custom[0].piece_id = 101;
    custom[0].motion_x = 12;
    custom[0].offset_x += 12;
    KdcOrbitCapture(3, width, height);
    double moved = RedAt(1), half = RedAt(0.5);
    assert(moved > end + 8 && half > end + 3 && half < moved - 3);

    /* Same slot with a different allocation generation must not travel from
     * the old actor. Neither may an unknown actor or a teleported one. */
    custom[0].motion_id += UINT64_C(1) << 32;
    custom[0].x -= 0.5;
    KdcOrbitCapture(4, width, height);
    assert(RedAt(0) == RedAt(1));
    custom[0].motion_id = 0;
    custom[0].x += 0.5;
    KdcOrbitCapture(5, width, height);
    assert(RedAt(0) == RedAt(1));

    /* Camera motion alone creates intermediate images using one shared
     * transform, including when the sprite identity is unknown. */
    for (unsigned frame = 6; frame < 20; ++frame) {
        KdcOrbitInput(32767, 0, false);
        KdcOrbitCapture(frame, width, height);
    }
    assert(isfinite(HdRedAt(0.5)));
    RedAt(0); memcpy(first, pixels, sizeof(first));
    RedAt(0.5);
    assert(memcmp(first, pixels, sizeof(first)));
    RedAt(1);
    assert(memcmp(first, pixels, sizeof(first)));

    KdcOrbitCapture(30, width, height); /* Timeline gap. */
    RedAt(0); memcpy(first, pixels, sizeof(first));
    RedAt(1); assert(!memcmp(first, pixels, sizeof(first)));
    Scroll(256, 0); Snapshot(); /* Look-ahead cut without a ball teleport. */
    KdcOrbitCapture(31, width, height);
    Fill(pixels, sizeof(pixels) / sizeof(*pixels));
    assert(KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 0));
    memcpy(first, pixels, sizeof(first));
    assert(KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 1));
    assert(!memcmp(first, pixels, sizeof(first)));
    Scroll(0, 0); Snapshot();
    ppu.inidisp = 14;
    KdcOrbitCapture(32, width, height);
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 0.5));
    ppu.inidisp = 15;
    Snapshot();
    KdcOrbitCapture(33, width, height);
    RedAt(0); memcpy(first, pixels, sizeof(first));
    RedAt(1); assert(!memcmp(first, pixels, sizeof(first)));
    KdcOrbitConfigure(true, false); /* Disabling controls restores the native angle. */
    KdcOrbitInput(0, 0, false);
    KdcOrbitCapture(34, width, height);
    RedAt(1); memcpy(first, pixels, sizeof(first));
    KdcOrbitReset(); KdcOrbitInput(0, 0, false);
    KdcOrbitCapture(35, width, height);
    RedAt(1); assert(!memcmp(first, pixels, sizeof(first)));
    KdcOrbitCapture(36, 342, height);
    Fill(wide_pixels, sizeof(wide_pixels) / sizeof(*wide_pixels));
    assert(KdcOrbitDraw((uint8_t *)wide_pixels, (342 + padding) * 4, NULL, 342, height, 0.5));
    CheckPadding(wide_pixels, 342);
    assert(KdcOrbitComposing());
    /* A declined draw shows the PPU frame, so its HD tiles must not stay
     * paused although the view is otherwise ready. */
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 0.5));
    assert(!KdcOrbitComposing());
    Word(0xabb2, KdcReadWord(g_ram, 0xabb2) + 1);
    KdcOrbitCapture(37, width, height); /* Map changed during this simulation. */
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 0.5));
    assert(!KdcOrbitComposing());
    KdcOrbitInput(0, 0, false); Snapshot();
    KdcOrbitCapture(38, width, height);
    RedAt(0); memcpy(first, pixels, sizeof(first));
    RedAt(1); assert(!memcmp(first, pixels, sizeof(first)));
    Word(0x1803, 2);
    KdcOrbitCapture(39, width, height); /* Active player changed after input sampling. */
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 0.5));
    KdcOrbitConfigure(false, true);
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 0.5));
    return 0;
}
