/* Synthetic pressure through the real PPU cache and game image source. */
#include <stdlib.h>
#include <stdbool.h>
static bool fail_next_allocation;
static void *TestCalloc(size_t count, size_t bytes)
{
    if (fail_next_allocation) {
        fail_next_allocation = false;
        return NULL;
    }
    return calloc(count, bytes);
}
#define calloc TestCalloc
#include "snes/ppu.c"
#undef calloc
#include "hd_textures.h"
#include "course_checker.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

Snes *g_snes;
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;
static uint32_t native[256 * 224], hd[1024 * 896], expected[1024 * 896];
static uint32_t seed = 0x12345678;
static uint16_t Next(void)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (uint16_t)seed;
}
static double Seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    return (double)now.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + now.tv_nsec * 1e-9;
#endif
}
static bool Context(const PpuHdTileQuery *query, void *context)
{
    (void)context;
    return KdcCheckerNeedsContext(query->words);
}
static void FillCache(Ppu *p, unsigned target, unsigned palette, unsigned position)
{
    for (unsigned i = 0; p->hdTiles->count < target; ++i) {
        assert(i < kPpuHdCacheSlots * 2);
        p->cgram[1] = (uint16_t)(palette + (i >> 10));
        PpuHdTileRecord rec = {0};
        rec.address = (uint16_t)(0x2000 + (i & 1023) * 16);
        rec.flags = kPpuHdBpp4;
        PpuHdLookup(p, 0, &rec, (i + position) * 8, 0);
    }
}
static void Frame(Ppu *p, unsigned run, const char *phase)
{
    ++snes_frame_counter;
    PpuHdTileStats before, after;
    PpuGetHdTileStats(p, &before);
    double start = Seconds();
    ppu_drawCapturedLine(p, 0, true);
    for (int line = 1; line <= 224; ++line) ppu_drawCapturedLine(p, line, true);
    double elapsed = (Seconds() - start) * 1000;
    PpuGetHdTileStats(p, &after);
    printf("{\"run\":%u,\"phase\":\"%s\",\"ms\":%.6f,\"misses\":%u,\"clears\":%u,"
           "\"trims\":%u,\"evictions\":%u}\n",
           run, phase, elapsed, (unsigned)(after.misses - before.misses),
           (unsigned)(after.cache_clears - before.cache_clears),
           (unsigned)(after.cache_trims - before.cache_trims),
           (unsigned)(after.cache_evictions - before.cache_evictions));
}
int main(void)
{
    for (unsigned run = 0; run < 5; ++run) {
        Ppu *p = ppu_init();
        assert(p); ppu_reset(p);
        p->inidisp = 15; p->bgmode = 1; p->bgTileAdr = 2;
        p->screenEnabled[0] = 1;
        for (unsigned i = 0; i < 1024; ++i) p->vram[i] = (uint16_t)i;
        for (unsigned i = 0x2000; i < 0x6000; ++i) p->vram[i] = Next();
        for (unsigned i = 0; i < 256; ++i) p->cgram[i] = Next() & 0x7fff;
        PpuBeginDrawing(p, (uint8_t *)native, 256 * 4, kPpuRenderFlags_NewRenderer);
        assert(PpuBindMode7HdSurface(p, hd, sizeof(hd), 1024 * 4, 256, 224, 4));
        assert(PpuSetHdTiles(p, KdcHdSmoothTile, NULL));
        assert(PpuSetHdTileContextPolicy(p, Context));
        Frame(p, run, "cold");
        memcpy(expected, hd, sizeof(hd));
        Frame(p, run, "warm");
        assert(!memcmp(expected, hd, sizeof(hd)));
        uint16_t color = p->cgram[1];
        FillCache(p, kPpuHdCacheSlots * 3 / 4 - 32, 1000, 0);
        p->cgram[1] = color;
        Frame(p, run, "aged-warm");
        Frame(p, run, "aged-warm-2");
        FillCache(p, kPpuHdCacheSlots * 3 / 4, 30000, 32768);
        p->cgram[1] = color;
        unsigned misses = p->hdTiles->stats.misses;
        unsigned trims = p->hdTiles->stats.cache_trims;
        Frame(p, run, "capacity");
        assert(p->hdTiles->stats.cache_trims == trims + 1);
        assert(p->hdTiles->stats.misses == misses); /* hot images survive pressure */
        assert(!memcmp(expected, hd, sizeof(hd)));
        Frame(p, run, "following");
        assert(!memcmp(expected, hd, sizeof(hd)));
        /* A pathological working set cannot retain more than half the table.
         * Eviction must still reproduce the same live image after refilling. */
        FillCache(p, kPpuHdCacheSlots * 3 / 4, 20000, 65536);
        p->cgram[1] = color;
        PpuHdTilesBeginLine(p);
        assert(p->hdTiles->count == kPpuHdCacheSlots / 2);
        Frame(p, run, "oversized-working-set");
        assert(!memcmp(expected, hd, sizeof(hd)));
        /* Force only the compaction allocation to fail. The original clear
         * path must rebuild live images and invalidate moved-slot memos. */
        FillCache(p, kPpuHdCacheSlots * 3 / 4, 25000, 98304);
        p->cgram[1] = color;
        unsigned clears = p->hdTiles->stats.cache_clears;
        fail_next_allocation = true;
        Frame(p, run, "allocation-failure");
        assert(!fail_next_allocation);
        assert(p->hdTiles->stats.cache_clears == clears + 1);
        assert(!memcmp(expected, hd, sizeof(hd)));
        ppu_free(p);
    }
    return 0;
}
