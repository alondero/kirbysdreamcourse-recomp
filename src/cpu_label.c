/* Host-drawn replacement for player 2's HUD name box during CPU matches.
 * The glyphs below are this project's own 5-pixel lettering; no game
 * artwork is copied or shipped. */
#include "cpu_label.h"
#include <string.h>

typedef struct { char c; unsigned width; const char *rows[5]; } Glyph;

static const Glyph glyphs[] = {
    {'A', 3, {".X.", "X.X", "XXX", "X.X", "X.X"}},
    {'C', 3, {".XX", "X..", "X..", "X..", ".XX"}},
    {'D', 3, {"XX.", "X.X", "X.X", "X.X", "XX."}},
    {'E', 3, {"XXX", "X..", "XX.", "X..", "XXX"}},
    {'H', 3, {"X.X", "X.X", "XXX", "X.X", "X.X"}},
    {'I', 3, {"XXX", ".X.", ".X.", ".X.", "XXX"}},
    {'M', 5, {"X...X", "XX.XX", "X.X.X", "X...X", "X...X"}},
    {'P', 3, {"XX.", "X.X", "XX.", "X..", "X.."}},
    {'R', 3, {"XX.", "X.X", "XX.", "X.X", "X.X"}},
    {'S', 3, {".XX", "X..", ".X.", "..X", "XX."}},
    {'U', 3, {"X.X", "X.X", "X.X", "X.X", "XXX"}},
    {'Y', 3, {"X.X", "X.X", ".X.", ".X.", ".X."}},
};

static const Glyph *Find(char c)
{
    for (unsigned i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i)
        if (glyphs[i].c == c) return &glyphs[i];
    return NULL;
}

static unsigned Width(const char *text)
{
    unsigned width = 0;
    for (const char *p = text; *p; ++p) {
        const Glyph *g = Find(*p);
        if (g) width += g->width + (width ? 1u : 0u);
    }
    return width;
}

/* Clears a lettering pixel back to colour 0 in a 32x16 colour-1 box. */
static void Clear(uint16_t tiles[KDC_CPU_LABEL_TILES][8], unsigned x, unsigned y)
{
    if (x >= 32 || y >= 16) return;
    uint16_t bit = (uint16_t)(0x80u >> (x & 7));
    tiles[(y / 8) * 4 + x / 8][y & 7] &= (uint16_t)~bit;
}

static void Line(uint16_t tiles[KDC_CPU_LABEL_TILES][8], const char *text, unsigned top)
{
    unsigned width = Width(text);
    if (width > 30) return; /* Outline columns stay intact. */
    unsigned x = 1 + (30 - width) / 2;
    for (const char *p = text; *p; ++p) {
        const Glyph *g = Find(*p);
        if (!g) continue;
        for (unsigned row = 0; row < 5; ++row)
            for (unsigned col = 0; col < g->width; ++col)
                if (g->rows[row][col] == 'X') Clear(tiles, x + col, top + row);
        x += g->width + 1;
    }
}

void KdcCpuLabelRender(const char *name, uint16_t tiles[KDC_CPU_LABEL_TILES][8])
{
    /* Colour 1 is the low plane alone: a set low byte, an empty high byte. */
    for (unsigned t = 0; t < KDC_CPU_LABEL_TILES; ++t)
        for (unsigned y = 0; y < 8; ++y) tiles[t][y] = 0x00ff;
    for (unsigned x = 0; x < 32; ++x) { Clear(tiles, x, 0); Clear(tiles, x, 15); }
    for (unsigned y = 0; y < 16; ++y) { Clear(tiles, 0, y); Clear(tiles, 31, y); }
    Line(tiles, "CPU", 2);
    Line(tiles, name ? name : "", 9);
}

bool KdcCpuLabelPatch(uint16_t *vram, unsigned base, const char *name)
{
    if (!vram) return false;
    uint16_t *p1 = vram + ((base + KDC_CPU_LABEL_P1_TILE * 8) & 0x7fff);
    uint16_t *p2 = vram + ((base + KDC_CPU_LABEL_P2_TILE * 8) & 0x7fff);
    if (p2 + KDC_CPU_LABEL_TILES * 8 > vram + 0x8000 || p1 + 32 > vram + 0x8000)
        return false;
    /* The top row spells the same word in both boxes only while the game's
     * own artwork is present. Blank tiles are not artwork. */
    if (memcmp(p1, p2, 32 * sizeof(uint16_t))) return false;
    bool drawn = false;
    for (unsigned i = 0; i < 32; ++i) drawn |= p2[i] != 0;
    if (!drawn) return false;
    uint16_t tiles[KDC_CPU_LABEL_TILES][8];
    KdcCpuLabelRender(name, tiles);
    memcpy(p2, tiles, sizeof(tiles));
    return true;
}
