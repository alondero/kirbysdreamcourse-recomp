/* ROM-free checks for the incremental terrain texture (course_terrain.c):
 * unchanged state costs nothing, a palette that keeps animating does not
 * regenerate the enlargement on every frame yet always shows its live
 * colours, and once a palette settles the enlargement catches up once. */
#include "course_terrain.h"
#include "hd_image.h"
#include "hd_scaler.h"
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

static void CheckFreshScenery(const KdcScenery *scenery)
{
    static uint8_t expected[1024 * 1024];
    float luma[256] = {0};
    for (unsigned i = 0; i < 32; ++i)
        luma[i] = i & 3 ? KdcXbrLuma555(ppu.cgram[i]) : -1;
    KdcHdImage image = {.src = scenery->native, .width = 256, .height = 256,
        .src_stride = 256, .dst = expected, .dst_stride = 1024, .luma = luma};
    CHECK(KdcHdImageUpdate(&image, 0, 0, 256, 256), "fresh enlargement failed");
    CHECK(scenery->hd && !memcmp(expected, scenery->hd, sizeof(expected)),
          "cached/partial scenery differs from a fresh enlargement");
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

    /* A looping backdrop must reuse its enlarged animation frames. Compare
     * every hit with a fresh build, then change the map and settled palette
     * to ensure neither can accidentally retrieve old artwork. */
    static uint8_t phases[2][1024 * 1024];
    for (unsigned phase = 0; phase < 2; ++phase) {
        KdcTerrainReset();
        ppu.vram[0x4000 + 16] = phase ? 0x00ff : 0xffff;
        scenery = KdcSceneryUpdate(&ppu, ram, true);
        memcpy(phases[phase], scenery->hd, sizeof(phases[phase]));
    }
    KdcTerrainReset();
    for (unsigned frame = 0; frame < 20; ++frame) {
        ppu.vram[0x4000 + 16] = frame & 1 ? 0x00ff : 0xffff;
        scenery = KdcSceneryUpdate(&ppu, ram, true);
        CHECK(!memcmp(phases[frame & 1], scenery->hd, sizeof(phases[0])),
              "animation frame %u differs from fresh enlargement", frame);
        if (frame == 1) {
            KdcTerrainGetStats(&stats);
            before = stats.scenery_enlargements;
        }
    }
    KdcTerrainGetStats(&stats);
    CHECK(stats.scenery_enlargements == before, "looping scenery was enlarged again");
    CHECK(stats.scenery_cache_hits >= 18, "animation did not reuse cached frames");

    for (unsigned frame = 0; frame < 10; ++frame) {
        ppu.cgram[1] = frame & 1 ? 0x001f : 0x7fff;
        ppu.vram[0x4000 + 16] = frame & 1 ? 0x00ff : 0xffff;
        scenery = KdcSceneryUpdate(&ppu, ram, true);
        CHECK(scenery->colors[1] == (frame & 1 ? 0xffff0000u : 0xffffffffu),
              "cached scenery did not use live colours");
        CHECK(!memcmp(phases[frame & 1], scenery->hd, sizeof(phases[0])),
              "cycling palette changed the unsettled edge decisions");
    }
    KdcTerrainGetStats(&stats);
    CHECK(stats.scenery_enlargements == before, "cycling palette defeated animation reuse");

    /* Eviction plus small dirty rectangles: the chosen slot may contain an
     * unrelated old frame, so unchanged regions must come from the last one. */
    for (unsigned phase = 0; phase < 7; ++phase) {
        Word(0xdadc + 2 * (phase * 137), 1 | ((phase & 1) ? 0xc000 : 0));
        scenery = KdcSceneryUpdate(&ppu, ram, true);
        CheckFreshScenery(scenery);
    }
    ppu.cgram[1] = 0x7fff;
    for (unsigned i = 0; i < 31; ++i) scenery = KdcSceneryUpdate(&ppu, ram, true);
    CheckFreshScenery(scenery);
    scenery = KdcSceneryUpdate(&ppu, ram, false);
    CHECK(!scenery->hd, "HD off retained a cached image");
    ppu.vram[0x4000 + 8] ^= 0x00ff;
    KdcSceneryUpdate(&ppu, ram, false);
    scenery = KdcSceneryUpdate(&ppu, ram, true);
    CheckFreshScenery(scenery);

    KdcTerrainReset();
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
