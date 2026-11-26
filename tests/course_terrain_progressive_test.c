#include "course_terrain.h"
#include "hd_image.h"
#include "hd_scaler.h"
#include "snes/ppu.h"

#include <stdio.h>
#include <stdlib.h>
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

static void BuildLargeCourse(void)
{
    memset(ram, 0, sizeof(ram));
    memset(&ppu, 0, sizeof(ppu));
    for (unsigned row = 0; row < 64; ++row) {
        Word(0x8bb0 + 2 * row, row * 8);
        Word(0x93b0 + 2 * row, 0);
        Word(0x9bb0 + 2 * row, 63);
    }
    for (unsigned i = 0; i < 64 * 64; ++i) Word(0x13000 + 2 * i, 1);
    for (unsigned y = 0; y < 8; ++y) {
        unsigned low = 0, high = 0;
        for (unsigned x = 0; x < 8; ++x) {
            const unsigned pixel = x >= 2 && x < 6 && y >= 2 && y < 6 ? 2 : 0;
            low |= (pixel & 1) << (7 - x) | ((pixel >> 1) & 1) << (15 - x);
            high |= ((pixel >> 2) & 1) << (7 - x) | ((pixel >> 3) & 1) << (15 - x);
        }
        ppu.vram[16 + y] = (uint16_t)low;
        ppu.vram[16 + 8 + y] = (uint16_t)high;
    }
    ppu.cgram[1] = 0x0c63;
    ppu.cgram[2] = 0x739c;
}

int main(void)
{
    BuildLargeCourse();
    KdcTerrainReset();
    const KdcTerrain *terrain = KdcTerrainUpdate(&ppu, ram, true);
    CHECK(terrain != NULL, "no large terrain");
    CHECK(terrain && !terrain->hd, "large first enlargement was synchronous");
    if (!terrain) return 1;

    ppu.vram[16] ^= 0x0001;
    unsigned calls = 1;
    while (!terrain->hd && calls < 128) {
        terrain = KdcTerrainUpdate(&ppu, ram, true);
        ++calls;
    }
    CHECK(terrain->hd != NULL, "progressive enlargement never completed");
    CHECK(calls > 1 && calls < 128, "unexpected progressive enlargement call count %u", calls);
    if (!terrain->hd) return 1;

    const size_t source_pixels = (size_t)terrain->width * terrain->height;
    const size_t hd_bytes = source_pixels * kKdcTerrainScale * kKdcTerrainScale;
    uint8_t *expected = malloc(hd_bytes);
    CHECK(expected != NULL, "allocation failed");
    if (!expected) return 1;
    float luma[256] = {0};
    for (unsigned i = 0; i < 128; ++i)
        luma[i] = i & 15 ? KdcXbrLuma555(ppu.cgram[i]) : -1;
    KdcHdImage image = {.src = terrain->native, .width = terrain->width, .height = terrain->height,
        .src_stride = terrain->width, .dst = expected,
        .dst_stride = terrain->width * kKdcTerrainScale, .luma = luma};
    const int rows = terrain->height / 8;
    for (int strip = 0; strip * 4 < rows; ++strip) {
        const int y0 = strip * 32;
        const int y1 = (strip + 1) * 32 < terrain->height ? (strip + 1) * 32 : terrain->height;
        CHECK(KdcHdImageUpdate(&image, 0, y0, terrain->width, y1),
              "fresh comparison strip enlargement failed");
    }
    size_t differences = 0, first = 0;
    for (size_t i = 0; i < hd_bytes; ++i)
        if (expected[i] != terrain->hd[i]) {
            if (!differences) first = i;
            ++differences;
        }
    if (differences) {
        const unsigned hd_width = (unsigned)terrain->width * kKdcTerrainScale;
        const unsigned sx = (unsigned)(first % hd_width) / kKdcTerrainScale;
        const unsigned sy = (unsigned)(first / hd_width) / kKdcTerrainScale;
        fprintf(stderr, "differences=%zu first=(%u,%u) source=(%u,%u) native=%u expected=%u actual=%u\n",
                differences, (unsigned)(first % hd_width), (unsigned)(first / hd_width), sx, sy,
                terrain->native[sy * terrain->width + sx], expected[first], terrain->hd[first]);
    }
    CHECK(!differences,
          "progressive terrain differs from a fresh enlargement after a mid-build change");
    free(expected);
    KdcTerrainReset();
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
