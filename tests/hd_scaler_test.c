/* ROM-free checks for the palette-locked xBR port: no new indices, flat areas
 * unchanged, diagonals smoothed, and the shader's four-corner symmetry. A lane
 * or neighbour mix-up in the port breaks 90-degree rotation invariance. */
#include "hd_scaler.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { kMax = 16, kScale = 4 };
static float luma[256];

static void Rotate(const uint8_t *src, int n, uint8_t *dst)
{
    /* 90 degrees clockwise: (x, y) -> (n - 1 - y, x). */
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) dst[x * n + (n - 1 - y)] = src[y * n + x];
}

static void AssertRotationInvariant(const uint8_t *image, int n)
{
    uint8_t rotated[kMax * kMax];
    uint8_t a[kMax * kMax * kScale * kScale], b[kMax * kMax * kScale * kScale];
    uint8_t c[kMax * kMax * kScale * kScale];
    Rotate(image, n, rotated);
    KdcXbrScale(image, n, n, luma, kScale, a);
    KdcXbrScale(rotated, n, n, luma, kScale, b);
    Rotate(a, n * kScale, c);
    assert(memcmp(b, c, (size_t)n * n * kScale * kScale) == 0);
}

static int Differs(const uint8_t *image, int n, const uint8_t *scaled)
{
    for (int y = 0; y < n * kScale; ++y)
        for (int x = 0; x < n * kScale; ++x)
            if (scaled[y * n * kScale + x] != image[(y / kScale) * n + x / kScale]) return 1;
    return 0;
}

static uint32_t rng = 0x2468ace1u;
static uint32_t Next(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

int main(void)
{
    luma[0] = -1.0f;
    for (int i = 1; i < 256; ++i) luma[i] = (float)i / 255.0f;
    uint8_t image[kMax * kMax], out[kMax * kMax * kScale * kScale];

    memset(image, 7, sizeof(image));
    KdcXbrScale(image, 8, 8, luma, kScale, out);
    assert(!Differs(image, 8, out));

    /* A high-contrast 45-degree edge becomes a finer staircase: each output
     * row's index-200 run is a suffix whose start never moves left as rows go
     * down. (Edges weaker than the shader's 0.1 luma margin stay sharp.) */
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) image[y * 8 + x] = x > y ? 200 : 40;
    KdcXbrScale(image, 8, 8, luma, kScale, out);
    assert(Differs(image, 8, out));
    int previous = 8 * kScale;
    for (int y = 0; y < 8 * kScale; ++y) {
        int start = 8 * kScale;
        for (int x = 0; x < 8 * kScale; ++x)
            if (out[y * 8 * kScale + x] == 200) { start = x; break; }
        for (int x = start; x < 8 * kScale; ++x) assert(out[y * 8 * kScale + x] == 200);
        if (y) assert(start >= previous || previous == 8 * kScale);
        previous = start;
    }
    AssertRotationInvariant(image, 8);

    /* A filled disc on transparency gains rounded HD corners. */
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            int dx = 2 * x - 7, dy = 2 * y - 7;
            image[y * 8 + x] = dx * dx + dy * dy < 50 ? 3 : 0;
        }
    KdcXbrScale(image, 8, 8, luma, kScale, out);
    assert(Differs(image, 8, out));
    AssertRotationInvariant(image, 8);

    /* Random images: palette-locked and symmetric. */
    for (int trial = 0; trial < 200; ++trial) {
        int n = trial % 2 ? 8 : 13, colors = 2 + trial % 5;
        for (int i = 0; i < n * n; ++i) image[i] = (uint8_t)(Next() % colors * 40);
        KdcXbrScale(image, n, n, luma, kScale, out);
        for (int i = 0; i < n * n * kScale * kScale; ++i) {
            int found = 0;
            for (int j = 0; j < n * n && !found; ++j) found = out[i] == image[j];
            assert(found);
        }
        AssertRotationInvariant(image, n);
    }
    puts("xbr ok");
    return 0;
}
