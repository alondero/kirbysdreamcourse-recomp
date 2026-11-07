/* ROM-free presentation diagnostic. Reuse the renderer's synthetic scene
 * and checks; this is a probe, not a new game host or a refresh-rate test. */
#define main OrbitFixtureMain
#include "../tests/course_orbit_render_test.c"
#undef main
#include "desktop/host_clock.h"
#include <time.h>

static uint64_t HashPixels(void)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            hash ^= pixels[y * stride + x];
            hash *= UINT64_C(1099511628211);
        }
    return hash;
}

int main(void)
{
    if (OrbitFixtureMain()) return 1;
    const unsigned rates[] = {60, 120, 144, 240};
    puts("hz,simulations,presentations,intermediate_images,guest_changes,simulation_image_hash,draw_clock_ms");
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        KdcOrbitConfigure(true, true);
        NativeAngle();
        Snapshot();
        SnesHostClock timeline;
        snes_host_clock_reset(&timeline, 0, SNES_HOST_NTSC_HZ, rates[r]);
        unsigned intermediate = 0, guest_changes = 0;
        uint64_t sequence = 0;
        clock_t draw_ticks = 0;
        for (;;) {
            double now = snes_host_clock_next_deadline(&timeline);
            if (now >= 2) break;
            if (snes_host_clock_simulation_due(&timeline, now)) {
                /* Deterministic camera motion once per simulated tick. */
                KdcOrbitInput(timeline.simulation_frames < 60 ? 32767 : -32767, 0, false);
                KdcOrbitCapture((unsigned)timeline.simulation_frames + 1, width, height);
                Draw();
                memcpy(first, pixels, sizeof(first));
                sequence = sequence * 31 + HashPixels();
                snes_host_clock_simulation_done(&timeline, now, false, 1);
            }
            if (snes_host_clock_presentation_due(&timeline, now)) {
                clock_t start = clock();
                assert(KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL,
                    width, height, snes_host_clock_alpha(&timeline, now)));
                draw_ticks += clock() - start;
                intermediate += memcmp(first, pixels, sizeof(first)) != 0;
                guest_changes += memcmp(g_ram, saved_ram, sizeof(g_ram)) != 0 ||
                                 memcmp(&ppu, &saved_ppu, sizeof(ppu)) != 0;
                CheckPadding(pixels, width);
                snes_host_clock_presentation_done(&timeline, now);
            }
        }
        printf("%u,%llu,%llu,%u,%u,%016llx,%.3f\n", rates[r],
            (unsigned long long)timeline.simulation_frames,
            (unsigned long long)timeline.presentations, intermediate, guest_changes,
            (unsigned long long)sequence,
            1000.0 * draw_ticks / CLOCKS_PER_SEC / timeline.presentations);
    }
    return 0;
}
