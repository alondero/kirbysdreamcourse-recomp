/* ROM-free check of the CPU name box drawn over player 2's HUD label. */
#include "cpu_label.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static uint16_t vram[0x8000];

static unsigned Pixel(uint16_t tiles[KDC_CPU_LABEL_TILES][8], unsigned x, unsigned y)
{
    uint16_t word = tiles[(y / 8) * 4 + x / 8][y & 7];
    unsigned bit = 7 - (x & 7);
    return ((word >> bit) & 1) | (((word >> (8 + bit)) & 1) << 1);
}

int main(void)
{
    uint16_t tiles[KDC_CPU_LABEL_TILES][8];
    KdcCpuLabelRender("MEDIUM", tiles);
    /* Colour 1 fill with a colour 0 outline, as the game's own boxes. */
    for (unsigned x = 0; x < 32; ++x) CHECK(!Pixel(tiles, x, 0) && !Pixel(tiles, x, 15));
    for (unsigned y = 0; y < 16; ++y) CHECK(!Pixel(tiles, 0, y) && !Pixel(tiles, 31, y));
    unsigned top = 0, bottom = 0;
    for (unsigned y = 1; y < 15; ++y)
        for (unsigned x = 1; x < 31; ++x) {
            unsigned p = Pixel(tiles, x, y);
            CHECK(p <= 1);
            if (!p) *(y < 8 ? &top : &bottom) += 1;
        }
    CHECK(top > 10 && bottom > 20); /* "CPU" above, the longer name below. */

    const unsigned base = 0x4000, p1 = base + KDC_CPU_LABEL_P1_TILE * 8;
    const unsigned p2 = base + KDC_CPU_LABEL_P2_TILE * 8;
    /* Blank tiles are not the game's artwork. */
    CHECK(!KdcCpuLabelPatch(vram, base, "EASY"));
    for (unsigned i = 0; i < 64; ++i) {
        vram[p1 + i] = (uint16_t)(0x1100 + i);
        vram[p2 + i] = (uint16_t)(i < 32 ? 0x1100 + i : 0x2200 + i);
    }
    uint16_t before[0x8000];
    memcpy(before, vram, sizeof(vram));
    CHECK(KdcCpuLabelPatch(vram, base, "EASY"));
    KdcCpuLabelRender("EASY", tiles);
    CHECK(!memcmp(vram + p2, tiles, sizeof(tiles)));
    /* Nothing outside player 2's eight tiles changes. */
    CHECK(!memcmp(vram, before, p2 * sizeof(uint16_t)));
    CHECK(!memcmp(vram + p2 + 64, before + p2 + 64, (0x8000 - p2 - 64) * sizeof(uint16_t)));
    /* Once replaced it stays put until the game uploads its box again. */
    CHECK(!KdcCpuLabelPatch(vram, base, "EASY"));
    return 0;
}
