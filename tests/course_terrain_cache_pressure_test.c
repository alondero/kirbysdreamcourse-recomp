#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
/* Reproduce the terrain block cache accounting race with real parallel workers.
 * Every cache allocation sleeps inside the wrapped allocator, stretching the
 * window between the old byte check and its recording so concurrent workers
 * overlap in it without any cross-thread synchronization. The byte limit is
 * reduced at compile time so the regression does not need a large course. */
#include "course_terrain.h"
#include "hd_image.h"
#include "host_parallel.h"
#include "snes/ppu.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void StretchWindow(void) { Sleep(2); }
#else
#include <time.h>
static void StretchWindow(void)
{
    const struct timespec pause = {.tv_nsec = 2 * 1000 * 1000};
    nanosleep(&pause, NULL);
}
#endif

static Ppu ppu;
static uint8_t ram[0x20000];
static atomic_bool observing;
static atomic_size_t observed_bytes;
static _Thread_local bool in_image_update;

extern void *__real_malloc(size_t size);
extern void __real_free(void *ptr);
extern int __real_KdcHdImageUpdate(const KdcHdImage *image, int x0, int y0, int x1, int y1);

static void Word(unsigned address, unsigned value)
{
    ram[address] = (uint8_t)value;
    ram[address + 1] = (uint8_t)(value >> 8);
}

void KdcTerrainCachePressureObserve(bool enable)
{
    atomic_store(&observing, enable);
}

size_t KdcTerrainCachePressureBytes(void)
{
    return atomic_load(&observed_bytes);
}

int __wrap_KdcHdImageUpdate(const KdcHdImage *image, int x0, int y0, int x1, int y1)
{
    in_image_update = true;
    int result = __real_KdcHdImageUpdate(image, x0, y0, x1, y1);
    in_image_update = false;
    return result;
}

typedef struct TrackedAllocation {
    void *ptr;
    size_t size;
} TrackedAllocation;
static TrackedAllocation tracked[4096];
static atomic_flag tracked_lock = ATOMIC_FLAG_INIT;

static void TrackLock(void)
{
    while (atomic_flag_test_and_set(&tracked_lock)) {
    }
}

static void TrackUnlock(void)
{
    atomic_flag_clear(&tracked_lock);
}

void *__wrap_malloc(size_t size)
{
    void *result = __real_malloc(size);
    if (!result) return NULL;
    const bool count = atomic_load(&observing) && !in_image_update;
    if (count) StretchWindow();
    if (count) {
        TrackLock();
        for (unsigned i = 0; i < 4096; ++i) {
            if (!tracked[i].ptr) {
                tracked[i].ptr = result;
                tracked[i].size = size;
                break;
            }
        }
        TrackUnlock();
        atomic_fetch_add(&observed_bytes, size);
    }
    return result;
}

void __wrap_free(void *ptr)
{
    if (ptr) {
        size_t size = 0;
        TrackLock();
        for (unsigned i = 0; i < 4096; ++i) {
            if (tracked[i].ptr == ptr) {
                size = tracked[i].size;
                tracked[i].ptr = NULL;
                tracked[i].size = 0;
                break;
            }
        }
        TrackUnlock();
        if (size && atomic_load(&observing)) atomic_fetch_sub(&observed_bytes, size);
    }
    __real_free(ptr);
}

/* A 64x64-cell grid (including the one-cell border) with every cell using
 * the same large uniform character. The first complete enlargement has no
 * cache; changing that character then enlarges every block through the cache. */
static void BuildCourse(void)
{
    memset(ram, 0, sizeof(ram));
    memset(&ppu, 0, sizeof(ppu));
    for (unsigned row = 0; row < 62; ++row) {
        Word(0x8bb0 + 2 * row, row * 8);
        Word(0x93b0 + 2 * row, 0);
        Word(0x9bb0 + 2 * row, 61);
    }
    for (unsigned i = 0; i < 0x6800; ++i) Word(0x13000 + 2 * i, 1);
    for (unsigned y = 0; y < 8; ++y) {
        ppu.vram[16 + y] = 0x00ff;
        ppu.vram[16 + 8 + y] = 0;
    }
    ppu.cgram[1] = 0x0c63;
}

int main(void)
{
    if (KdcParallelThreads() < 2) {
        fprintf(stderr, "cache pressure test requires at least two workers\n");
        return 1;
    }
    BuildCourse();
    KdcTerrainReset();
    const KdcTerrain *terrain = KdcTerrainUpdate(&ppu, ram, true);
    if (!terrain) {
        fprintf(stderr, "no terrain\n");
        return 1;
    }
    for (unsigned i = 0; !terrain->hd && i < 128; ++i) terrain = KdcTerrainUpdate(&ppu, ram, true);
    if (!terrain->hd) {
        fprintf(stderr, "initial terrain enlargement did not complete\n");
        return 1;
    }
    ppu.vram[16] ^= 0x0001;
    KdcTerrainCachePressureObserve(true);
    if (!KdcTerrainUpdate(&ppu, ram, true)) {
        fprintf(stderr, "cached terrain update failed\n");
        return 1;
    }
    KdcTerrainCachePressureObserve(false);
    const size_t bytes = KdcTerrainCachePressureBytes();
    const size_t limit = 256u * 1024u;
    if (bytes > limit) {
        fprintf(stderr, "terrain block cache allocated %zu bytes against a %zu-byte limit\n",
                bytes, limit);
        return 1;
    }
    printf("terrain cache pressure: %zu bytes within %zu-byte limit\n", bytes, limit);
    KdcTerrainReset();
    return 0;
}
