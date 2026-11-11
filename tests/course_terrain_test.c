/* ROM-free checks for the incremental terrain texture (course_terrain.c):
 * unchanged state costs nothing, a palette that keeps animating does not
 * regenerate the enlargement on every frame yet always shows its live
 * colours, and once a palette settles the enlargement catches up once. */
#include "course_terrain.h"
#include "snes/ppu.h"

#include <stdio.h>
#include <string.h>

static Ppu ppu;
static uint8_t ram[0x20000];
static int failures;

#define CHECK(condition, ...) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); ++failures; } } while (0)

static void Word(unsigned address, unsigned value)
{
    ram[address] = (uint8_t)value;
    ram[address + 1] = (uint8_t)(value >> 8);
}

/* An 8x4 character course on layer 0. Character 1 has a light diagonal
 * half (index 2) over a dark one (index 1), so the image has edges. */
static void BuildCourse(void)
{
    memset(ram, 0, sizeof(ram));
    memset(&ppu, 0, sizeof(ppu));
    for (unsigned row = 0; row < 4; ++row) {
        Word(0x8bb0 + 2 * row, row * 8);
        Word(0x93b0 + 2 * row, 0);
        Word(0x9bb0 + 2 * row, 7);
        for (unsigned column = 0; column < 8; ++column) Word(0x13000 + 2 * (row * 8 + column), 1);
    }
    for (unsigned y = 0; y < 8; ++y) {
        unsigned low = 0, high = 0;
        for (unsigned x = 0; x < 8; ++x) {
            const unsigned pixel = x >= y ? 2 : 1;
            low |= (pixel & 1) << (7 - x) | ((pixel >> 1) & 1) << (15 - x);
            high |= ((pixel >> 2) & 1) << (7 - x) | ((pixel >> 3) & 1) << (15 - x);
        }
        ppu.vram[16 + y] = (uint16_t)low;
        ppu.vram[16 + 8 + y] = (uint16_t)high;
    }
    ppu.cgram[1] = 0x0c63;
    ppu.cgram[2] = 0x739c;
}

static uint64_t Scaled(void)
{
    KdcTerrainStats stats;
    KdcTerrainGetStats(&stats);
    return stats.scaled_tiles;
}

int main(void)
{
    BuildCourse();
    KdcTerrainReset();
    const KdcTerrain *terrain = KdcTerrainUpdate(&ppu, ram, true);
    CHECK(terrain && terrain->hd, "no enlarged terrain");
    if (!terrain) return 1;
    uint64_t before = Scaled();
    KdcTerrainUpdate(&ppu, ram, true);
    CHECK(Scaled() == before, "an unchanged update regenerated %llu characters",
          (unsigned long long)(Scaled() - before));

    /* A palette entry cycling every frame, as course animations do. */
    before = Scaled();
    for (int frame = 0; frame < 200; ++frame) {
        ppu.cgram[1] = frame & 1 ? 0x7fff : 0x0c63;
        terrain = KdcTerrainUpdate(&ppu, ram, true);
        const uint32_t expected = frame & 1 ? 0xffffffffu : 0xff181818u;
        CHECK(terrain->colors[1] == expected, "frame %d shows colour %08x, not the live %08x",
              frame, (unsigned)terrain->colors[1], (unsigned)expected);
        if (terrain->colors[1] != expected) break;
    }
    CHECK(Scaled() == before, "an animating palette regenerated %llu characters",
          (unsigned long long)(Scaled() - before));

    /* Once it holds still, edge decisions follow the settled colours. */
    before = Scaled();
    for (int frame = 0; frame < 120; ++frame) KdcTerrainUpdate(&ppu, ram, true);
    const uint64_t settled = Scaled() - before;
    CHECK(settled > 0, "the settled palette never regenerated the enlargement");
    before = Scaled();
    for (int frame = 0; frame < 120; ++frame) KdcTerrainUpdate(&ppu, ram, true);
    CHECK(Scaled() == before, "a settled palette kept regenerating");

    /* Scenery: the HUD uploads its own BG3 characters during play. Only
     * characters the clean scenery map uses may redraw the scenery. */
    for (unsigned cell = 0; cell < 1024; ++cell) Word(0xdadc + 2 * cell, cell < 512 ? 1 : 2);
    for (unsigned y = 0; y < 8; ++y) {
        ppu.vram[0x4000 + 8 + y] = 0x00f0;
        ppu.vram[0x4000 + 16 + y] = 0xff00;
    }
    ppu.cgram[1] = 0x001f;
    ppu.cgram[2] = 0x7c00;
    const KdcScenery *scenery = KdcSceneryUpdate(&ppu, ram, true);
    CHECK(scenery && scenery->hd, "no enlarged scenery");
    KdcTerrainStats stats;
    KdcTerrainGetStats(&stats);
    uint64_t builds = stats.scenery_builds;
    for (int frame = 0; frame < 10; ++frame) {
        ppu.vram[0x4000 + 40 * 8 + (unsigned)frame % 8] ^= 0x0101; /* character 40: unused */
        KdcSceneryUpdate(&ppu, ram, true);
    }
    KdcTerrainGetStats(&stats);
    CHECK(stats.scenery_builds == builds, "unused BG3 characters redrew the scenery %llu times",
          (unsigned long long)(stats.scenery_builds - builds));
    /* A used character still redraws it, matching a complete rebuild. */
    ppu.vram[0x4000 + 16] = 0xffff;
    scenery = KdcSceneryUpdate(&ppu, ram, true);
    KdcTerrainGetStats(&stats);
    CHECK(stats.scenery_builds == builds + 1, "a used character did not redraw the scenery");
    static uint8_t incremental[1024 * 1024];
    memcpy(incremental, scenery->hd, sizeof(incremental));
    KdcTerrainReset();
    scenery = KdcSceneryUpdate(&ppu, ram, true);
    CHECK(!memcmp(incremental, scenery->hd, sizeof(incremental)),
          "the incremental scenery differs from a complete rebuild");

    KdcTerrainReset();
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
