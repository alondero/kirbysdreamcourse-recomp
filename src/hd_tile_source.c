#include "hd_textures.h"
#include "hd_scaler.h"
#include "course_checker.h"
#include "snes/ppu.h"

static void DecodeTile(const PpuHdTileQuery *q, uint8_t pixels[64])
{
    for (unsigned r = 0; r < 8; ++r)
        for (unsigned c = 0; c < 8; ++c) {
            unsigned w = q->words[r];
            unsigned v = (w >> (7 - c) & 1) | (w >> (15 - c) & 1) << 1;
            if (q->bpp == 4) {
                w = q->words[8 + r];
                v |= (w >> (7 - c) & 1) << 2 | (w >> (15 - c) & 1) << 3;
            }
            pixels[r * 8 + c] = (uint8_t)v;
        }
}

bool KdcHdNearestTile(const PpuHdTileQuery *q, uint8_t *indices, void *context)
{
    (void)context;
    uint8_t pixels[64];
    DecodeTile(q, pixels);
    const unsigned s = q->scale, side = 8 * s;
    for (unsigned y = 0; y < side; ++y)
        for (unsigned x = 0; x < side; ++x)
            indices[y * side + x] = pixels[(y / s) * 8 + x / s];
    return true;
}

bool KdcHdSmoothTile(const PpuHdTileQuery *q, uint8_t *indices, void *context)
{
    (void)context;
    uint8_t pixels[64];
    DecodeTile(q, pixels);
    /* Uniform characters gain nothing; keep them native and cheap. */
    bool uniform = true;
    for (unsigned i = 1; i < 64 && uniform; ++i) uniform = pixels[i] == pixels[0];
    if (uniform) return false;
    KdcChecker checker;
    if (q->layer < 2 && q->bpp == 4 && KdcCheckerFit(q->words, q->border, &checker)) {
        const unsigned side = 8 * q->scale;
        for (unsigned y = 0; y < side; ++y)
            for (unsigned x = 0; x < side; ++x)
                indices[y * side + x] = KdcCheckerSample(&checker,
                    (x + 0.5) / q->scale, (y + 0.5) / q->scale);
        return true;
    }
    float luma[256] = {0};
    luma[0] = -1.0f;
    for (unsigned i = 1; i < (1u << q->bpp); ++i) luma[i] = KdcXbrLuma555(q->colors[i]);
    KdcXbrScale(pixels, 8, 8, luma, q->scale, indices);
    return true;
}
