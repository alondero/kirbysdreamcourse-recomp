/* ROM-free fixture for HD tile replacement in the shared PPU.
 *
 * Scenes are synthetic: random planar tiles, tilemaps, palettes and OAM, not
 * game data. The identity oracle requires a nearest-neighbour tile source to
 * reproduce the nearest-scaled native frame exactly while every line takes
 * the HD path. Explicit scenes then check that transparent HD subpixels
 * reveal the layer below, that opaque HD subpixels grow over natively
 * transparent pixels, and that unrecorded layers fall back to native. */
#include "snes/ppu.c"
#include <assert.h>
#include <stdio.h>

Snes *g_snes;
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;

enum { kWidth = 256, kHeight = 224, kMaxScale = 4 };
static uint32_t native[kHeight * kWidth];
static uint32_t hd[kHeight * kMaxScale * kWidth * kMaxScale];
static uint32_t rng = 0x12345678u;

static uint32_t Next(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static unsigned Decode(const uint16_t *words, unsigned bpp, unsigned c, unsigned r)
{
    unsigned w = words[r], v = (w >> (7 - c) & 1) | (w >> (15 - c) & 1) << 1;
    if (bpp == 4) {
        w = words[8 + r];
        v |= (w >> (7 - c) & 1) << 2 | (w >> (15 - c) & 1) << 3;
    }
    return v;
}

static bool Nearest(const PpuHdTileQuery *q, uint8_t *indices, void *context)
{
    (void)context;
    unsigned s = q->scale, side = 8 * s;
    for (unsigned y = 0; y < side; ++y)
        for (unsigned x = 0; x < side; ++x)
            indices[y * side + x] = (uint8_t)Decode(q->words, q->bpp, x / s, y / s);
    return true;
}

static Ppu *NewPpu(unsigned scale, PpuHdTileFill *fill)
{
    Ppu *p = ppu_init();
    assert(p);
    ppu_reset(p);
    p->inidisp = 0x0f;
    p->bgmode = 1;
    p->bgXsc[0] = 0x00; p->bgXsc[1] = 0x04; p->bgXsc[2] = 0x08;
    p->bgTileAdr = 0x0432;   /* BG1 chars $2000, BG2 $3000, BG3 $4000 */
    p->obsel = 3;            /* OBJ chars $6000, 8x8/16x16 */
    PpuBeginDrawing(p, (uint8_t *)native, kWidth * 4, kPpuRenderFlags_NewRenderer);
    memset(hd, 0, sizeof(hd));
    assert(PpuBindMode7HdSurface(p, hd, sizeof(hd), kWidth * scale * 4,
                                 kWidth, kHeight, scale));
    assert(PpuSetHdTiles(p, fill, NULL));
    return p;
}

static void Render(Ppu *p)
{
    ppu_drawCapturedLine(p, 0, true);
    for (int line = 1; line <= kHeight; ++line) ppu_drawCapturedLine(p, line, true);
}

static void AssertNearest(unsigned scale)
{
    for (unsigned y = 0; y < kHeight * scale; ++y)
        for (unsigned x = 0; x < kWidth * scale; ++x) {
            uint32_t want = native[(y / scale) * kWidth + x / scale];
            uint32_t got = hd[y * kWidth * scale + x];
            if (want != got) {
                fprintf(stderr, "mismatch at %u,%u: %06x != %06x\n", x, y,
                        (unsigned)got, (unsigned)want);
                assert(0);
            }
        }
}

static uint32_t HdPixel(unsigned scale, unsigned x, unsigned y, unsigned sx, unsigned sy)
{
    return hd[(y * scale + sy) * kWidth * scale + x * scale + sx];
}

static uint32_t Rgb(const Ppu *p, uint16_t color)
{
    return (uint32_t)p->brightnessMult[color & 31] << 16 |
           (uint32_t)p->brightnessMult[color >> 5 & 31] << 8 |
           p->brightnessMult[color >> 10 & 31];
}

static void RandomScene(Ppu *p)
{
    for (unsigned i = 0x2000; i < 0x8000; ++i) p->vram[i] = (uint16_t)Next();
    /* Some fully and partly transparent characters reveal lower layers. */
    for (unsigned i = 0; i < 16; ++i) {
        p->vram[0x2000 + i] = 0;
        p->vram[0x2010 + i] = (uint16_t)(i < 8 ? 0x0f0f : 0);
        p->vram[0x4000 + (i & 7)] = 0;
    }
    for (unsigned i = 0; i < 0x400; ++i) {
        uint16_t bg1 = (uint16_t)(Next() & 0xffff);
        if (Next() % 3 == 0) bg1 = (uint16_t)((bg1 & 0xfc00) | (Next() & 1));
        p->vram[i] = bg1;
        p->vram[0x400 + i] = (uint16_t)Next();
        uint16_t bg3 = (uint16_t)Next();
        if (Next() % 2) bg3 &= 0xfc00;  /* BG3 char 0 is transparent */
        p->vram[0x800 + i] = bg3;
    }
    for (unsigned i = 0; i < 256; ++i) p->cgram[i] = (uint16_t)(Next() & 0x7fff);
    for (unsigned i = 0; i < 256; i += 2) {
        p->oam[i] = (uint16_t)(0xf000 | 0x00);  /* parked below the field */
        p->oam[i + 1] = 0;
    }
    memset(p->highOam, 0, sizeof(p->highOam));
    for (unsigned n = 0; n < 24; ++n) {
        unsigned index = 2 * (n * 5 % 128);
        p->oam[index] = (uint16_t)((Next() % 240) | (Next() % 200) << 8);
        p->oam[index + 1] = (uint16_t)Next();
        if (Next() & 1) p->highOam[index >> 3] |= (uint8_t)(2u << (index & 7));
    }
    p->hScroll[0] = 3; p->vScroll[0] = 5;
    p->hScroll[1] = 250; p->vScroll[1] = 1;
    p->hScroll[2] = 17; p->vScroll[2] = 300;
    p->screenEnabled[0] = 0x17;
    p->screenEnabled[1] = 0x04;
    p->cgwsel = 0x02;             /* add the subscreen */
    p->cgadsub = 0x40 | 0x02 | 0x10; /* half math on BG2 and OBJ */
    p->fixedColor = 0x1234;
}

static void TestIdentity(unsigned scale, bool bg3_priority)
{
    Ppu *p = NewPpu(scale, Nearest);
    RandomScene(p);
    if (bg3_priority) p->bgmode |= 8;
    Render(p);
    PpuHdTileStats stats;
    PpuGetHdTileStats(p, &stats);
    assert(stats.hd_lines == kHeight);
    assert(stats.native_lines == 0);
    assert(stats.fallback_pixels == 0);
    /* A cached image whose subpixels all match the native index must not
     * enter the per-pixel exactness/composition path. */
    assert(stats.replaced_pixels == 0);
    AssertNearest(scale);
    /* Also exercise the full identity oracle, including the random scene's
     * colour math. Conservatively disable the cached unchanged summaries;
     * the images themselves still contain exact nearest-neighbour indices. */
    for (unsigned i = 0; i < kPpuHdCacheSlots; ++i)
        if (p->hdTiles->entries[i].image) p->hdTiles->entries[i].image->same = 0;
    Render(p);
    PpuGetHdTileStats(p, &stats);
    assert(stats.replaced_pixels > 0);
    assert(stats.fallback_pixels == 0);
    AssertNearest(scale);
    PpuSetHdTiles(p, NULL, NULL);
    ppu_free(p);
}

/* Replacement for BG1 character 1: the native left half is opaque, the right
 * half transparent. The HD tile is opaque except each texel's top-left
 * subpixel. OBJ character 0 loses each texel's (1,1) subpixel. */
static bool Explicit(const PpuHdTileQuery *q, uint8_t *indices, void *context)
{
    (void)context;
    unsigned s = q->scale, side = 8 * s;
    if (q->bpp == 4 && q->words[0] == 0x00f0 && q->words[8] == 0) {
        memset(indices, 1, side * side);
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x)
                indices[y * s * side + x * s] = 0;
        return true;
    }
    if (q->bpp == 4 && q->words[0] == 0xff00 && q->words[8] == 0) {
        memset(indices, 2, side * side);
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x)
                indices[(y * s + 1) * side + x * s + 1] = 0;
        return true;
    }
    return false;
}

static void TestRevealAndGrowth(void)
{
    const unsigned s = 4;
    Ppu *p = NewPpu(s, Explicit);
    for (unsigned r = 0; r < 8; ++r) {
        p->vram[0x2010 + r] = 0x00f0;   /* BG1 char 1: index 1, columns 0-3 */
        p->vram[0x3020 + r] = 0x00ff;   /* BG2 char 2: index 1, all pixels */
        p->vram[0x3028 + r] = 0x00ff;   /* ... plus plane 2: index 5 */
        p->vram[0x6000 + r] = 0xff00;   /* OBJ char 0: index 2 everywhere */
    }
    for (unsigned i = 0; i < 0x400; ++i) {
        p->vram[i] = 0x0001;            /* BG1 char 1, palette 0 */
        p->vram[0x400 + i] = 0x0002;    /* BG2 char 2, palette 0 */
    }
    p->cgram[1] = 0x001f;               /* red */
    p->cgram[5] = 0x03e0;               /* green */
    p->cgram[0x82] = 0x7c00;            /* blue OBJ */
    for (unsigned i = 0; i < 256; i += 2) p->oam[i] = 0xf000;
    p->oam[0] = (uint16_t)(16 | 16 << 8);
    p->oam[1] = 0x3000;                 /* char 0, palette 0, priority 3 */
    p->oam[2] = (uint16_t)(40 | 16 << 8);
    p->oam[3] = 0x7000;                 /* the same, mirrored horizontally */
    p->screenEnabled[0] = 0x13;
    Render(p);
    uint32_t red = Rgb(p, 0x001f), green = Rgb(p, 0x03e0), blue = Rgb(p, 0x7c00);
    /* Output row 0 is scanline 1. BG1 is red in columns 0-3 of each tile;
     * its transparent right half shows BG2's green. */
    assert(native[0] == red);
    assert(native[5] == green);
    /* A transparent HD subpixel reveals BG2; the rest of the texel is BG1. */
    assert(HdPixel(s, 0, 0, 0, 0) == green);
    assert(HdPixel(s, 0, 0, 1, 0) == red);
    /* Opaque HD subpixels cover a natively transparent pixel. */
    for (unsigned sy = 0; sy < s; ++sy)
        for (unsigned sx = 0; sx < s; ++sx)
            assert(HdPixel(s, 5, 0, sx, sy) == (sx || sy ? red : green));
    /* OAM Y=16 is first displayed on scanline 17, output row 16. A
     * transparent OBJ subpixel reveals the HD background beneath it. */
    assert(native[16 * kWidth + 20] == blue);
    assert(HdPixel(s, 20, 16, 0, 0) == blue);
    assert(HdPixel(s, 20, 16, 1, 1) == red);
    /* Mirrored OBJ: the transparent subpixel moves to the mirrored column. */
    assert(HdPixel(s, 40, 16, 1, 1) == blue);
    assert(HdPixel(s, 40, 16, s - 2, 1) == red);
    PpuHdTileStats stats;
    PpuGetHdTileStats(p, &stats);
    assert(stats.fallback_pixels == 0);
    PpuSetHdTiles(p, NULL, NULL);
    ppu_free(p);
}

/* Only the top-left subpixel of character pixel (0,0) is transparent, so
 * flips must move it to the mirrored corner of the mirrored native pixel. */
static bool Corner(const PpuHdTileQuery *q, uint8_t *indices, void *context)
{
    (void)context;
    unsigned side = 8 * q->scale;
    if (q->bpp != 4 || q->words[0] != 0x00ff || q->words[8] != 0) return false;
    memset(indices, 1, side * side);
    indices[0] = 0;
    return true;
}

static void TestFlips(void)
{
    const unsigned s = 4;
    Ppu *p = NewPpu(s, Corner);
    for (unsigned r = 0; r < 8; ++r) {
        p->vram[0x2010 + r] = 0x00ff;   /* BG1 char 1: index 1 everywhere */
        p->vram[0x3020 + r] = 0x00ff;
        p->vram[0x3028 + r] = 0x00ff;   /* BG2 char 2: index 5 */
    }
    for (unsigned ty = 0; ty < 32; ++ty)
        for (unsigned tx = 0; tx < 32; ++tx) {
            p->vram[ty * 32 + tx] = (uint16_t)(0x0001 | (tx & 1) << 14 | (ty & 1) << 15);
            p->vram[0x400 + ty * 32 + tx] = 0x0002;
        }
    p->cgram[1] = 0x001f;
    p->cgram[5] = 0x03e0;
    for (unsigned i = 0; i < 256; i += 2) p->oam[i] = 0xf000;
    p->vScroll[0] = p->vScroll[1] = 0x3ff; /* output row r shows map row r */
    p->screenEnabled[0] = 0x03;
    Render(p);
    uint32_t red = Rgb(p, 0x001f), green = Rgb(p, 0x03e0);
    for (unsigned ty = 0; ty < 2; ++ty)
        for (unsigned tx = 0; tx < 2; ++tx) {
            unsigned x = 8 * tx + (tx ? 7 : 0), y = 8 * ty + (ty ? 7 : 0);
            unsigned cx = tx ? s - 1 : 0, cy = ty ? s - 1 : 0;
            for (unsigned sy = 0; sy < s; ++sy)
                for (unsigned sx = 0; sx < s; ++sx)
                    assert(HdPixel(s, x, y, sx, sy) ==
                           (sx == cx && sy == cy ? green : red));
        }
    PpuSetHdTiles(p, NULL, NULL);
    ppu_free(p);
}

/* Character pixel (0,0) stays opaque but its top-left subpixel takes another
 * colour, so the winning layer alone decides every subpixel there. */
static bool Recolour(const PpuHdTileQuery *q, uint8_t *indices, void *context)
{
    (void)context;
    unsigned side = 8 * q->scale;
    if (q->bpp != 4 || q->words[0] != 0x00ff || q->words[8] != 0) return false;
    memset(indices, 1, side * side);
    indices[0] = 2;
    return true;
}

static void TestOpaqueWinnerSubpixels(void)
{
    const unsigned s = 4;
    Ppu *p = NewPpu(s, Recolour);
    for (unsigned r = 0; r < 8; ++r) {
        p->vram[0x2010 + r] = 0x00ff;   /* BG1 char 1: index 1 everywhere */
        p->vram[0x3020 + r] = 0x00ff;
        p->vram[0x3028 + r] = 0x00ff;   /* BG2 char 2: index 5 */
    }
    for (unsigned ty = 0; ty < 32; ++ty)
        for (unsigned tx = 0; tx < 32; ++tx) {
            p->vram[ty * 32 + tx] = (uint16_t)(0x0001 | (tx & 1) << 14 | (ty & 1) << 15);
            p->vram[0x400 + ty * 32 + tx] = 0x0002;
            p->vram[0x800 + ty * 32 + tx] = 0x2000; /* BG3 char 0, transparent, high */
        }
    p->bgmode = 0x09;                   /* Mode 1, BG3 priority above BG1 */
    p->cgram[1] = 0x001f;
    p->cgram[2] = 0x7c00;
    p->cgram[5] = 0x03e0;
    for (unsigned i = 0; i < 256; i += 2) p->oam[i] = 0xf000;
    p->vScroll[0] = p->vScroll[1] = p->vScroll[2] = 0x3ff;
    p->screenEnabled[0] = 0x07;
    Render(p);
    uint32_t red = Rgb(p, 0x001f), blue = Rgb(p, 0x7c00);
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x) {
            unsigned tx = x / 8, ty = y / 8;
            bool corner = x == 8 * tx + (tx ? 7 : 0) && y == 8 * ty + (ty ? 7 : 0);
            unsigned cx = tx ? s - 1 : 0, cy = ty ? s - 1 : 0;
            for (unsigned sy = 0; sy < s; ++sy)
                for (unsigned sx = 0; sx < s; ++sx)
                    assert(HdPixel(s, x, y, sx, sy) ==
                           (corner && sx == cx && sy == cy ? blue : red));
        }
    PpuHdTileStats stats;
    PpuGetHdTileStats(p, &stats);
    assert(stats.fallback_pixels == 0);
    /* One changed native pixel per 8x8 tile, including both flip axes;
     * all other cached texels must bypass exactness/composition. */
    assert(stats.replaced_pixels == kWidth * kHeight / 64);
    PpuSetHdTiles(p, NULL, NULL);
    ppu_free(p);
}

static bool Opaque(const PpuHdTileQuery *q, uint8_t *indices, void *context)
{
    (void)context;
    memset(indices, 1, 64u * q->scale * q->scale);
    return true;
}

/* Keep screen columns 100..139 native everywhere and all of scanline 50. */
static bool KeepSpan(const Ppu *ppu, unsigned line, int first_x, unsigned count,
                     uint8_t *keep_native, void *context)
{
    (void)ppu;
    ++*(unsigned *)context;
    if (line == 50) return false;
    for (unsigned k = 0; k < count; ++k) {
        int x = first_x + (int)k;
        keep_native[k] = x >= 100 && x < 140;
    }
    return true;
}

static void TestLinePolicy(void)
{
    const unsigned s = 2;
    Ppu *p = NewPpu(s, Opaque);
    RandomScene(p);
    unsigned calls = 0;
    assert(PpuSetHdTilePolicy(p, KeepSpan, &calls));
    Render(p);
    assert(calls >= kHeight - 1);
    PpuHdTileStats stats;
    PpuGetHdTileStats(p, &stats);
    assert(stats.native_lines == 1 && stats.hd_lines == kHeight - 1);
    unsigned changed = 0;
    for (unsigned y = 0; y < kHeight; ++y)
        for (unsigned x = 0; x < kWidth; ++x) {
            bool kept = y == 49 || (x >= 100 && x < 140);
            for (unsigned sy = 0; sy < s; ++sy)
                for (unsigned sx = 0; sx < s; ++sx) {
                    uint32_t want = native[y * kWidth + x], got = HdPixel(s, x, y, sx, sy);
                    if (kept) assert(got == want);
                    else changed += got != want;
                }
        }
    assert(changed > 0); /* the opaque source still applies elsewhere */
    /* Clearing the policy restores replacement for every column. */
    assert(PpuSetHdTilePolicy(p, NULL, NULL));
    Render(p);
    unsigned span_changed = 0;
    for (unsigned y = 0; y < kHeight; ++y)
        for (unsigned x = 100; x < 140; ++x)
            span_changed += HdPixel(s, x, y, 0, 0) != native[y * kWidth + x];
    assert(span_changed > 0);
    PpuSetHdTiles(p, NULL, NULL);
    ppu_free(p);
}

static void TestUnsupportedLinesStayNative(void)
{
    /* 16x16 BG2 characters are drawn by a path that does not record tile
     * sources; mosaic is not reproduced. Both keep exact native pixels even
     * with a tile source that would visibly change every tile. */
    for (unsigned variant = 0; variant < 2; ++variant) {
        Ppu *p = NewPpu(2, Opaque);
        RandomScene(p);
        if (variant == 0) p->bgmode |= 0x20;
        else p->mosaic = 0x31;
        Render(p);
        PpuHdTileStats stats;
        PpuGetHdTileStats(p, &stats);
        assert(stats.hd_lines == 0);
        assert(stats.native_lines == kHeight);
        AssertNearest(2);
        PpuSetHdTiles(p, NULL, NULL);
        ppu_free(p);
    }
}

static void TestDisableRestoresMode7Surface(void)
{
    Ppu *p = NewPpu(2, Nearest);
    RandomScene(p);
    assert(PpuSetHdTiles(p, NULL, NULL));
    Render(p);
    PpuHdTileStats stats;
    PpuGetHdTileStats(p, &stats);
    assert(stats.hd_lines == 0 && stats.native_lines == 0);
    AssertNearest(2);
    ppu_free(p);
}

static bool LayerColor(const PpuHdTileQuery *q, uint8_t *indices, void *context)
{
    (void)context;
    memset(indices, q->layer + 1, 64 * q->scale * q->scale);
    return true;
}

static void TestLayerCacheIsolation(void)
{
    Ppu *p = NewPpu(4, LayerColor);
    PpuHdTilesBeginLine(p);
    PpuHdTileRecord record = {.flags = kPpuHdBpp4};
    /* Identical words and palette on terrain, scenery and OBJ must not
     * share a replacement when the provider makes a layer-specific choice. */
    for (unsigned layer = 0; layer < kPpuHdSources; ++layer) {
        unsigned entry = PpuHdLookup(p, layer, &record, 0, 0);
        assert(entry);
        assert(p->hdTiles->entries[entry - 1].image->indices[0] == layer + 1);
        assert(PpuHdLookup(p, layer, &record, 0, 0) == entry);
    }
    ppu_free(p);
}

static PpuHdTileQuery observed;
static bool RequestBorder(const PpuHdTileQuery *q, void *context)
{
    (void)q;
    (void)context;
    return true;
}
static bool ObserveBorder(const PpuHdTileQuery *q, uint8_t *indices, void *context)
{
    observed = *q;
    return Nearest(q, indices, context);
}

static void TestNeighbourBorder(void)
{
    Ppu *p = NewPpu(4, ObserveBorder);
    assert(PpuSetHdTileContextPolicy(p, RequestBorder));
    for (unsigned i = 0; i < 1024; ++i) p->vram[i] = (uint16_t)(1 | (i % 4) << 14);
    for (unsigned i = 0; i < 16; ++i) p->vram[0x2010 + i] = (uint16_t)Next();
    PpuHdTilesBeginLine(p);
    for (unsigned flip = 0; flip < 4; ++flip) {
        p->vram[5 * 32 + 4] = (uint16_t)(1 | flip << 14);
        PpuHdTileRecord record = {.address = 0x2010,
            .flags = (uint8_t)(kPpuHdBpp4 | (flip & 1 ? kPpuHdFlipX : 0) |
                                            (flip & 2 ? kPpuHdFlipY : 0))};
        PpuInvalidateHdTiles(p);
        assert(PpuHdLookup(p, 0, &record, 32, 40));
        for (int k = 0; k < 80; ++k) {
            int x = k < 48 ? k % 12 - 2 : (k - 48) / 8 < 2 ? (k - 48) / 8 - 2 : (k - 48) / 8 + 6;
            int y = k < 48 ? (k / 12 < 2 ? k / 12 - 2 : k / 12 + 6) : (k - 48) % 8;
            x = 32 + (flip & 1 ? 7 - x : x);
            y = 40 + (flip & 2 ? 7 - y : y);
            unsigned tile = p->vram[(y / 8) * 32 + x / 8];
            unsigned px = x & 7, py = y & 7;
            if (tile & 0x4000) px = 7 - px;
            if (tile & 0x8000) py = 7 - py;
            assert(observed.border[k] == Decode(p->vram + 0x2010, 4, px, py));
        }
    }
    PpuHdTileRecord plain = {.address = 0x2010, .flags = kPpuHdBpp4};
    PpuHdTilesBeginLine(p);
    unsigned before = PpuHdLookup(p, 0, &plain, 32, 40);
    p->vram[4 * 32 + 4] |= 0x0400; /* neighbour changes palette only */
    p->vramWriteCount++;           /* as a $2118/$2119 or DMA write counts it */
    PpuHdTilesBeginLine(p);
    unsigned after = PpuHdLookup(p, 0, &plain, 32, 40);
    assert(before != after);
    /* Biased anchor/margin spans have no supported neighbour context. */
    plain.flags |= kPpuHdNoBorder;
    assert(PpuHdLookup(p, 0, &plain, 32, 40));
    for (unsigned i = 0; i < sizeof(observed.border); ++i) assert(!observed.border[i]);
    ppu_free(p);
}

static unsigned provider_calls;
static uint16_t UnwrappedProvider(int layer, int x, uint32_t y,
                                 uint16_t scroll, uint16_t real, void *context)
{
    (void)real;
    (void)context;
    assert(layer == 0 && scroll == 0);
    assert(x >= 504 && x <= 520);
    assert(y >= 760 && y <= 776);
    ++provider_calls;
    return 4;
}

static void TestBorderWrapping(void)
{
    Ppu *p = NewPpu(4, ObserveBorder);
    PpuHdTileRecord record = {.address = 0x2010, .flags = kPpuHdBpp4};
    for (unsigned page = 0; page < 4; ++page) {
        for (unsigned i = 0; i < 1024; ++i) p->vram[page * 1024 + i] = page + 1;
        for (unsigned y = 0; y < 8; ++y) {
            unsigned color = page + 1;
            p->vram[0x2010 + page * 16 + y] =
                (color & 1 ? 0xff : 0) | (color & 2 ? 0xff00 : 0);
            p->vram[0x2018 + page * 16 + y] = color & 4 ? 0xff : 0;
        }
    }
    static const int origins[] = {-520, -264, -8, 0, 248, 256, 504, 512, 768};
    for (unsigned size = 0; size < 4; ++size) {
        p->bgXsc[0] = size;
        int width = size & 1 ? 512 : 256, height = size & 2 ? 512 : 256;
        for (unsigned a = 0; a < sizeof(origins) / sizeof(*origins); ++a)
            for (unsigned b = 0; b < sizeof(origins) / sizeof(*origins); ++b) {
                uint8_t border[80];
                PpuHdBorder(p, 0, &record, (unsigned)origins[a], (unsigned)origins[b], border);
                for (int k = 0; k < 80; ++k) {
                    int x = k < 48 ? k % 12 - 2 : (k - 48) / 8 < 2 ? (k - 48) / 8 - 2 : (k - 48) / 8 + 6;
                    int y = k < 48 ? (k / 12 < 2 ? k / 12 - 2 : k / 12 + 6) : (k - 48) % 8;
                    x = ((origins[a] + x) % width + width) % width;
                    y = ((origins[b] + y) % height + height) % height;
                    unsigned page = x / 256 + (y / 256) * (width / 256);
                    assert(border[k] == page + 1);
                }
            }
    }
    WsShadowSetTileProvider(1, UnwrappedProvider, NULL);
    uint8_t border[80];
    PpuHdBorder(p, 0, &record, 512, 768, border);
    assert(provider_calls == 9);
    for (unsigned k = 0; k < 80; ++k) assert(border[k] == 4);
    WsShadowSetTileProvider(0, NULL, NULL);
    ppu_free(p);
}

/* Content lookups (and neighbour-border decoding) are the dominant HD cost.
 * Every scanline of a character cell asks for the same image, so a frame
 * must resolve each visible cell once, not once per scanline. */
static unsigned context_calls, fill_calls;
static bool CountContext(const PpuHdTileQuery *q, void *context)
{
    (void)q;
    (void)context;
    ++context_calls;
    return true;
}
static bool CountFill(const PpuHdTileQuery *q, uint8_t *indices, void *context)
{
    ++fill_calls;
    return Nearest(q, indices, context);
}

static Ppu *SingleCharacterScene(void)
{
    Ppu *p = NewPpu(4, CountFill);
    assert(PpuSetHdTileContextPolicy(p, CountContext));
    for (unsigned i = 0; i < 0x400; ++i) p->vram[i] = 1; /* BG1: character 1, palette 0 */
    for (unsigned i = 0; i < 16; ++i) p->vram[0x2010 + i] = (uint16_t)Next();
    for (unsigned i = 0; i < 256; ++i) p->cgram[i] = (uint16_t)(Next() & 0x7fff);
    p->screenEnabled[0] = 0x01;
    p->screenEnabled[1] = 0x00;
    p->hScroll[0] = 3; p->vScroll[0] = 5;
    return p;
}

static void TestOneLookupPerCell(void)
{
    Ppu *p = SingleCharacterScene();
    /* BG3 (2bpp) alternates characters, so a key without the cell position
     * would miss on every span. Sprites are parked off screen. */
    for (unsigned i = 0; i < 0x400; ++i) p->vram[0x800 + i] = (uint16_t)(1 + (i & 1));
    for (unsigned i = 0; i < 16; ++i) p->vram[0x4008 + i] = (uint16_t)Next();
    for (unsigned i = 0; i < 256; i += 2) p->oam[i] = 0xf000;
    p->screenEnabled[0] = 0x05;
    p->hScroll[2] = 3; p->vScroll[2] = 5;
    context_calls = 0;
    Render(p);
    AssertNearest(4);
    /* 256x224 with a 3,5 scroll touches 33x29 cells per layer. */
    assert(context_calls > 0 && context_calls <= 33 * 29);
    PpuHdTileStats stats;
    PpuGetHdTileStats(p, &stats);
    assert(stats.lookups <= 2 * 33 * 29);
    ppu_free(p);
}

static void TestCellLookupsFollowWrites(void)
{
    Ppu *p = SingleCharacterScene();
    Render(p);
    /* A counted VRAM write in the same frame (as $2118/$2119 or DMA make)
     * must not reuse a cell's earlier image. */
    for (unsigned i = 0; i < 16; ++i) p->vram[0x2010 + i] = (uint16_t)Next();
    p->vramWriteCount++;
    Render(p);
    AssertNearest(4);
    /* A palette change between lines re-keys the following lines. */
    ppu_drawCapturedLine(p, 0, true);
    for (int line = 1; line <= 100; ++line) ppu_drawCapturedLine(p, line, true);
    const unsigned before = fill_calls;
    p->cgram[1] ^= 0x1234;
    for (int line = 101; line <= kHeight; ++line) ppu_drawCapturedLine(p, line, true);
    assert(fill_calls > before);
    AssertNearest(4);
    /* A new frame never trusts the previous frame's cells: hosts may replace
     * VRAM wholesale between frames (state loads, soft reset). */
    for (unsigned i = 0; i < 16; ++i) p->vram[0x2010 + i] = (uint16_t)Next();
    ++snes_frame_counter;
    Render(p);
    AssertNearest(4);
    ppu_free(p);
}

/* While the host will not present the tiles (the game composes its own
 * frame, or the policy would keep every line native), no recording, lookup
 * or composition runs and the surface is the nearest-scaled native frame. */
static void TestPauseSkipsTileWork(void)
{
    Ppu *p = NewPpu(4, Opaque);
    RandomScene(p);
    PpuPauseHdTiles(p, true);
    Render(p);
    AssertNearest(4);
    PpuHdTileStats stats;
    PpuGetHdTileStats(p, &stats);
    assert(stats.lookups == 0 && stats.hd_lines == 0 && stats.cached_tiles == 0);
    PpuPauseHdTiles(p, false);
    Render(p);
    PpuGetHdTileStats(p, &stats);
    assert(stats.lookups > 0 && stats.hd_lines > 0);
    ppu_free(p);
}

int main(void)
{
    TestPauseSkipsTileWork();
    TestOneLookupPerCell();
    TestCellLookupsFollowWrites();
    TestLayerCacheIsolation();
    TestNeighbourBorder();
    TestBorderWrapping();
    TestIdentity(4, false);
    TestIdentity(4, true);
    TestIdentity(2, false);
    TestRevealAndGrowth();
    TestFlips();
    TestOpaqueWinnerSubpixels();
    TestLinePolicy();
    TestUnsupportedLinesStayNative();
    TestDisableRestoresMode7Surface();
    puts("hd tiles ok");
    return 0;
}
