/* Presentation artwork. The game supplies tile identity, palette and layout. */
#include "hd_ui.h"
#include "hd_pack.h"
#include "snes/ppu.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

enum UiTileKind {
    kUiFontV1 = 1,
    kUiFontV2,
    kUiCreditsV1,
    kUiCreditsV2,
    kUiBoxBorder,
    kUiHud1pLabel,
    kUiHud1pIcon,
    kUiHud1pCorner,
    kUiHud2pPoint,
    kUiHud2pPlayer,
    kUiHud2pCapsule,
    kUiHud2pCorner,
    kUiAbilityPortrait,
    kUiSpinGauge,
    kUiGuideNum,
    kUiObPopup,
    kUiObjTomato,
    kUiHudObj,
    kUiVitality,
    kUiStock,
    kUiShotDigit,
    kUiHoleDigit,
    kUiTitle,
    kUiMenuBanner,
    kUiMemberAction,
    kUiPointsDigit,
    kUiTitleRail,
    kUiTitleStar,
};

typedef struct UiTileEntry {
    uint64_t hash;
    uint8_t bpp;
    uint8_t kind;
    uint8_t param;
    uint8_t tx;
    uint8_t ty;
} UiTileEntry;

#include "hd_ui_catalog.inc"

enum { kUiEntryCount = sizeof(kUiEntries) / sizeof(kUiEntries[0]) };

static uint64_t HashTileWords(const uint16_t *words, unsigned count)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (unsigned i = 0; i < count; ++i) {
        uint8_t b0 = (uint8_t)(words[i] & 0xffu);
        uint8_t b1 = (uint8_t)((words[i] >> 8) & 0xffu);
        h = (h ^ b0) * UINT64_C(0x100000001b3);
        h = (h ^ b1) * UINT64_C(0x100000001b3);
    }
    return h;
}

static const UiTileEntry *LookupUiEntry(uint64_t hash, uint8_t bpp)
{
    unsigned lo = 0, hi = kUiEntryCount;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (kUiEntries[mid].hash < hash || (kUiEntries[mid].hash == hash && kUiEntries[mid].bpp < bpp)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < kUiEntryCount && kUiEntries[lo].hash == hash && kUiEntries[lo].bpp == bpp)
        return &kUiEntries[lo];
    return NULL;
}

static void DecodeWords(const uint16_t *words, unsigned bpp, uint8_t pixels[64])
{
    for (unsigned r = 0; r < 8; ++r) {
        unsigned w0 = words[r];
        unsigned w1 = bpp == 4 ? words[r + 8] : 0;
        for (unsigned c = 0; c < 8; ++c) {
            unsigned s = 7 - c;
            unsigned v = ((w0 >> s) & 1u) | (((w0 >> (s + 8)) & 1u) << 1);
            if (bpp == 4) v |= (((w1 >> s) & 1u) << 2) | (((w1 >> (s + 8)) & 1u) << 3);
            pixels[r * 8 + c] = (uint8_t)v;
        }
    }
}

static float SdSegment(float px, float py, float ax, float ay, float bx, float by)
{
    float pax = px - ax, pay = py - ay;
    float bax = bx - ax, bay = by - ay;
    float denom = bax * bax + bay * bay;
    float h = denom > 0.0f ? (pax * bax + pay * bay) / denom : 0.0f;
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    return hypotf(pax - bax * h, pay - bay * h);
}

static float SdBox(float px, float py, float cx, float cy, float hw, float hh, float r)
{
    float dx = fabsf(px - cx) - (hw - r);
    float dy = fabsf(py - cy) - (hh - r);
    float ox = dx > 0.0f ? dx : 0.0f;
    float oy = dy > 0.0f ? dy : 0.0f;
    float in_d = dx > dy ? dx : dy;
    if (in_d > 0.0f) in_d = 0.0f;
    return hypotf(ox, oy) + in_d - r;
}

/* Authored artwork in original UI coordinates. Tile identity and placement
 * come from the catalog. All paint, including highlights, uses live CGRAM. */

typedef struct Brush {
    uint32_t pixels[16];
    uint32_t *out;
    float left, top;
} Brush;

static unsigned UnitByte(float x) { return x <= 0 ? 0 : x >= 1 ? 255 : (unsigned)(x * 255 + 0.5f); }

static inline unsigned Div255(unsigned v)
{
    v += 128u;
    return (v + (v >> 8)) >> 8;
}

static uint32_t MixRgb(uint32_t a, uint32_t b, unsigned weight)
{
    const unsigned inv = 255u - weight;
    const unsigned r = Div255(((a >> 16) & 255u) * inv + ((b >> 16) & 255u) * weight);
    const unsigned g = Div255(((a >> 8) & 255u) * inv + ((b >> 8) & 255u) * weight);
    const unsigned bl = Div255((a & 255u) * inv + (b & 255u) * weight);
    return (r << 16) | (g << 8) | bl;
}

static void Ink(uint32_t *dst, uint32_t color, unsigned alpha)
{
    if (!alpha) return;
    if (color == 0x01000000u) {
        unsigned a = ((*dst >> 24) * (255 - alpha) + 127) / 255;
        *dst = a ? (a << 24) | (*dst & 0xffffff) : 0;
        return;
    }
    unsigned behind = ((*dst >> 24) * (255 - alpha) + 127) / 255;
    unsigned total = alpha + behind;
    *dst = (total << 24) | MixRgb(*dst, color, (255 * alpha + total / 2) / total);
}

/* Paint a rounded rectangle or an ellipse with a continuous diagonal wash.
 * At scale four a native-coordinate edge covers one quarter of a unit. */
static void Shape(Brush *b, bool oval, float x, float y, float w, float h, float radius, unsigned hi,
                  unsigned lo)
{
    int x0 = (int)fmaxf(0, floorf((x - b->left) * 4) - 1);
    int y0 = (int)fmaxf(0, floorf((y - b->top) * 4) - 1);
    int x1 = (int)fminf(32, ceilf((x + w - b->left) * 4) + 1);
    int y1 = (int)fminf(32, ceilf((y + h - b->top) * 4) + 1);
    for (int j = y0; j < y1; ++j)
        for (int i = x0; i < x1; ++i) {
            float u = b->left + (i + .5f) / 4 - x, v = b->top + (j + .5f) / 4 - y;
            float d = oval ? (hypotf(2 * u / w - 1, 2 * v / h - 1) - 1) * fminf(w, h) * .5f
                           : SdBox(u, v, w / 2, h / 2, w / 2, h / 2, radius);
            uint32_t color = hi == lo
                                 ? b->pixels[hi]
                                 : MixRgb(b->pixels[hi], b->pixels[lo], UnitByte(.3f * u / w + .7f * v / h));
            Ink(&b->out[j * 32 + i], color, UnitByte(.5f - 4 * d));
        }
}

static void Oval(Brush *b, float x, float y, float w, float h, unsigned hi, unsigned lo)
{
    Shape(b, true, x, y, w, h, 0, hi, lo);
}

static void Box(Brush *b, float x, float y, float w, float h, float r, unsigned hi, unsigned lo)
{
    Shape(b, false, x, y, w, h, r, hi, lo);
}

static void Stroke(Brush *b, float ax, float ay, float bx, float by, float r, unsigned color)
{
    int left = (int)fmaxf(0, floorf((fminf(ax, bx) - r - b->left) * 4) - 1);
    int top = (int)fmaxf(0, floorf((fminf(ay, by) - r - b->top) * 4) - 1);
    int right = (int)fminf(32, ceilf((fmaxf(ax, bx) + r - b->left) * 4) + 1);
    int bottom = (int)fminf(32, ceilf((fmaxf(ay, by) + r - b->top) * 4) + 1);
    for (int j = top; j < bottom; ++j)
        for (int i = left; i < right; ++i) {
            float d = SdSegment(b->left + (i + .5f) / 4, b->top + (j + .5f) / 4, ax, ay, bx, by) - r;
            Ink(&b->out[j * 32 + i], b->pixels[color], UnitByte(.5f - 4 * d));
        }
}

#include "hd_ui_font.inc"

static const struct FontGlyph *Glyph(unsigned char ch)
{
    if (ch == 169) return &kFontGlyphs[95];
    return ch >= 32 && ch <= 126 ? &kFontGlyphs[ch - 32] : NULL;
}

/* Integrate horizontal outline spans over four sample rows per output pixel.
 * Doing this in full-label coordinates keeps kerning and tile seams stable. */
static void Letter(Brush *b, unsigned char ch, float x, float y, float w, float h, float weight, unsigned ink)
{
    const struct FontGlyph *g = Glyph(ch);
    if (!g || !g->count || x + w + 1 < b->left || x - 1 > b->left + 8) return;
    float scale_x = w / g->advance, scale_y = h / 118;
    for (int row = 0; row < 32; ++row) {
        float coverage[32] = {0};
        for (int sample = 0; sample < 4; ++sample) {
            float line = (y + h * .81f - b->top - (row + (sample + .5f) / 4) / 4) / scale_y;
            float hits[64];
            unsigned count = 0, start = 0;
            for (unsigned k = 1; k < g->count; ++k) {
                const int16_t *v = kFontPoints[g->first + k];
                const int16_t *u = kFontPoints[g->first + k - 1];
                if (u[0] == -128) {
                    start = k;
                    continue;
                }
                if (v[0] == -128) v = kFontPoints[g->first + start];
                if ((u[1] > line) == (v[1] > line)) continue;
                float hit =
                    4 * (x - b->left + scale_x * (u[0] + (v[0] - u[0]) * (line - u[1]) / (v[1] - u[1])));
                unsigned j = count++;
                while (j && hits[j - 1] > hit) {
                    hits[j] = hits[j - 1];
                    --j;
                }
                hits[j] = hit;
            }
            for (unsigned k = 1; k < count; k += 2) {
                float lo = fmaxf(0, hits[k - 1] - weight), hi = fminf(32, hits[k] + weight);
                for (int i = (int)lo; i < hi; ++i)
                    coverage[i] += .25f * (fminf(i + 1, hi) - fmaxf(i, lo));
            }
        }
        for (int i = 0; i < 32; ++i)
            Ink(&b->out[row * 32 + i], b->pixels[ink], UnitByte(coverage[i]));
    }
}

/* The guest advances every character by eight pixels. Center the original
 * outline advance inside that cell so narrow letters keep their proportions. */
static void MenuGlyph(Brush *b, unsigned char ch)
{
    const struct FontGlyph *glyph = Glyph(ch);
    if (!glyph) return;
    float width = fminf(7.1f, glyph->advance * .077f);
    float left = (7.6f - width) * .5f;
    Letter(b, ch, left + .37f, .58f, width, 7, .48f, 1);
    Letter(b, ch, left, .12f, width, 7, .43f, 3);
}

static void Word(Brush *b, const char *s, float x, float y, float w, float h, float weight, unsigned ink)
{
    unsigned total = 0;
    for (const char *p = s; *p; ++p)
        total += Glyph((unsigned char)*p)->advance + 7;
    for (; *s; ++s) {
        float step = w * Glyph((unsigned char)*s)->advance / total;
        Letter(b, (unsigned char)*s, x, y, step, h, weight, ink);
        x += step + 7 * w / total;
    }
}

static void Face(Brush *b, float x, float y, float size, unsigned light, unsigned shade, unsigned dark)
{
    Oval(b, x, y, size, size, dark, dark);
    Oval(b, x + .5f, y + .45f, size - 1, size - 1, light, shade);
    for (int side = 0; side < 2; ++side) {
        float ex = x + size * (side ? .57f : .33f);
        Oval(b, ex, y + size * .31f, size * .095f, size * .25f, dark, dark);
        Oval(b, ex + size * .017f, y + size * .33f, size * .043f, size * .077f, light, light);
        Oval(b, x + size * (side ? .70f : .16f), y + size * .57f, size * .15f, size * .074f, shade, shade);
    }
    Oval(b, x + size * .45f, y + size * .59f, size * .105f, size * .105f, dark, dark);
}

static void Spark(Brush *b, float x, float y, float r, unsigned ink)
{
    Stroke(b, x - r, y, x + r, y, r * .16f, ink);
    Stroke(b, x, y - r * 1.25f, x, y + r * 1.25f, r * .18f, ink);
    Oval(b, x - r * .45f, y - r * .45f, r * .9f, r * .9f, ink, ink);
}

/* Elliptical forms with a light above the left shoulder. Keep the sampled
 * palette endpoints so Kirby and Keeby share a pose through palette swaps. */
static void PaintedOval(Brush *b, float cx, float cy, float rx, float ry, float tilt, unsigned light,
                        unsigned base, unsigned shadow)
{
    float cs = cosf(tilt), sn = sinf(tilt);
    for (int j = 0; j < 32; ++j)
        for (int i = 0; i < 32; ++i) {
            float x = b->left + (i + .5f) / 4 - cx, y = b->top + (j + .5f) / 4 - cy;
            float u = (cs * x + sn * y) / rx, v = (cs * y - sn * x) / ry;
            float d = (hypotf(u, v) - 1) * fminf(rx, ry);
            if (d > .125f) continue;
            uint32_t *out = &b->out[j * 32 + i];
            Ink(out, b->pixels[1], UnitByte(.5f - 4 * d));
            float t = hypotf(.8f * (u + .33f), .8f * (v + .55f));
            uint32_t skin = t < .65f
                                ? MixRgb(b->pixels[light], b->pixels[base], UnitByte(t / .65f))
                                : MixRgb(b->pixels[base], b->pixels[shadow], UnitByte((t - .65f) * 1.25f));
            Ink(out, skin, UnitByte(.5f - 4 * (d + .48f)));
        }
}

static void RunningKirby(Brush *b)
{
    Oval(b, 9, 22.5f, 17.5f, 3.4f, 14, 13);
    PaintedOval(b, 11.2f, 21.5f, 4.7f, 2.7f, -.75f, 13, 11, 10);
    PaintedOval(b, 8.2f, 13.6f, 3.9f, 2.85f, .48f, 15, 14, 12);
    PaintedOval(b, 22.7f, 13.1f, 3.25f, 3.1f, -.5f, 15, 14, 12);
    PaintedOval(b, 15.6f, 14.8f, 8.45f, 8.5f, -.12f, 15, 14, 12);
    for (int i = 0; i < 2; ++i) {
        float x = 15.6f + 2.8f * i;
        Oval(b, x, 10.3f, 1, 3.7f, 1, 1);
        Oval(b, x + .17f, 10.55f, .52f, 1.1f, 15, 15);
    }
    Oval(b, 12.8f, 14.4f, 2.4f, .95f, 13, 12);
    Oval(b, 20.1f, 14.2f, 1.8f, .85f, 13, 12);
    Stroke(b, 16.5f, 15.6f, 17.5f, 16.2f, .27f, 1);
    Stroke(b, 17.5f, 16.2f, 18.7f, 15.6f, .27f, 1);
    PaintedOval(b, 21.8f, 20.5f, 4.85f, 2.9f, -.53f, 13, 11, 10);
}

static void Portrait(Brush *b, unsigned ability)
{
    Box(b, .3f, .3f, 31.4f, 31.4f, 2, 1, 1);
    Box(b, 1.1f, 1.1f, 29.8f, 29.8f, 1.5f, 13, 11);
    Box(b, 2.1f, 2.1f, 27.8f, 27.8f, 1.2f, 1, 1);
    Box(b, 2.8f, 2.8f, 26.4f, 26.4f, .8f, ability == 0 || ability == 11 ? 15 : 9,
        ability == 0 || ability == 11 ? 15 : 9);
    if (ability == 11) {
        RunningKirby(b);
        return;
    }
    Oval(b, 8.5f, 23, 15, 2.3f, 13, 12);
    if (ability == 6) { /* Wheel with speed streaks and a raised hub. */
        for (int i = 0; i < 3; ++i)
            Stroke(b, 4 + i, 11 + i * 3, 14, 11 + i * 3, .48f, 14);
        Oval(b, 11, 7, 15, 18, 1, 1);
        Oval(b, 11.7f, 7.7f, 13.6f, 16.6f, 15, 12);
        Oval(b, 16, 12, 5, 6, 1, 11);
        Oval(b, 17, 12.8f, 2.8f, 3.4f, 15, 13);
        return;
    }
    if (ability == 4 || ability == 7 || ability == 3) {
        for (int i = 0; i < 12; ++i) {
            float a = (float)i * .523599f, dx = cosf(a), dy = sinf(a);
            float x = 16 + dx * 12.2f, y = 17 + dy * 12.2f;
            Stroke(b, 16 + dx * 6, 17 + dy * 6, x, y, ability == 4 ? .65f : 1.1f, ability == 4 ? 1 : 13);
            if (ability != 4) Stroke(b, x, y, x + dy * 1.7f, y - dx * 1.7f, .55f, 15);
        }
    }
    if (ability == 8)
        for (int i = 0; i < 5; ++i)
            Stroke(b, 10 + i * 3, 17, 10 + i * 3, 27 - i % 2, .6f, 14);
    float y = ability == 2 ? 14 : ability == 8 ? 6 : ability == 10 ? 5 : 9.3f;
    float x = ability == 2 ? 14 : 8.2f;
    float size = ability == 2 || ability == 10 ? 13.5f : 16;
    Oval(b, x - .7f, y + size * .73f, size * .43f, size * .27f, 11, 1);
    Oval(b, x + size * .61f, y + size * .73f, size * .43f, size * .27f, 12, 1);
    Face(b, x, y, size, 15, 13, 1);
    if (ability == 1) {
        Stroke(b, 8, 16, 14, 10, 2, 1);
        Stroke(b, 14, 10, 22, 12, 2, 1);
        Stroke(b, 8, 15.4f, 14, 9.4f, 1.55f, 13);
        Stroke(b, 14, 9.4f, 22, 11.4f, 1.5f, 14);
    } else if (ability == 2) {
        Stroke(b, 12, 10, 15, 22, .6f, 1);
        Oval(b, 4, 4, 17, 11, 1, 1);
        Oval(b, 4.7f, 4.7f, 15.6f, 9.6f, 15, 11);
        Oval(b, 9.4f, 4.9f, 6, 9.2f, 15, 12);
        for (int i = 0; i < 3; ++i)
            Oval(b, 4 + i * 5.7f, 12, 5.7f, 3, 9, 9);
    } else if (ability == 5) {
        for (int i = 0; i < 3; ++i) {
            float w = 16 - i * 3;
            Oval(b, 16 - w / 2, 18 + i * 2.5f, w, 3.8f, 15, 11);
        }
    } else if (ability == 9) {
        for (int i = 0; i < 3; ++i) {
            float cx = 6 + i * 9, cy = i % 2 ? 6 : 22;
            Spark(b, cx, cy, 1.5f, 15);
            Stroke(b, cx - 1, cy - 1, cx + 1, cy + 1, .3f, 14);
        }
    } else if (ability == 10) {
        Oval(b, 6.5f, 15, 19, 8, 1, 1);
        Oval(b, 7, 15.5f, 18, 6.7f, 15, 11);
        for (int i = 0; i < 3; ++i)
            Oval(b, 10 + i * 4.4f, 18.4f, 1.8f, 1.3f, 15, 13);
    }
}

static void Tomato(Brush *b, unsigned variant)
{
    Oval(b, .7f, 2, 14.6f, 13.5f, 1, 1);
    Oval(b, 1.8f, 3, 12.4f, 11.4f, variant ? 14 : 3, variant ? 11 : 2);
    Oval(b, 3.4f, 5.2f, 2, 3.5f, 15, variant ? 13 : 4);
    for (int i = 0; i < 5; ++i) {
        float angle = .35f + i * 1.25f;
        Stroke(b, 8, 4.6f, 8 + cosf(angle) * 3.4f, 4.6f + sinf(angle) * 2.5f, .65f, variant ? 12 : 6);
    }
    Stroke(b, 8, 4.3f, 8.6f, 1.5f, .7f, variant ? 11 : 7);
}

static void DisplayDigit(Brush *b, unsigned digit, bool wide)
{
    /* Shared top/bottom character halves also occur reflected. Symmetric
     * segment geometry preserves the digit when the PPU reuses a half. */
    static const uint8_t segments[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 7, 0x7f, 0x6f};
    float l = wide ? 4.6f : 1.5f, r = wide ? 11.4f : 6.5f;
    Box(b, -1, 15, 18, 1, 0, 2, 2);
    if (digit <= 1) {
        float x = wide ? 3.8f : .5f, w = wide ? 8.4f : 7;
        for (unsigned i = 0; i < 8; ++i) {
            float a = i * .7853982f;
            Letter(b, '0' + digit, x + .43f * cosf(a), .47f * sin(a), w, 16, .85f, 1);
        }
        Letter(b, '0' + digit, x, 0, w, 16, .64f, wide ? 3 : 0);
        return;
    }
    float coords[7][4] = {{l + 1, 2, r - 1, 2}, {r, 3, r, 7}, {r, 9, r, 13},       {l + 1, 14, r - 1, 14},
                          {l, 9, l, 13},        {l, 3, l, 7}, {l + 1, 8, r - 1, 8}};
    for (int pass = 0; pass < 2; ++pass)
        for (int k = 0; k < 7; ++k)
            if (segments[digit] & (1 << k))
                Stroke(b, coords[k][0], coords[k][1], coords[k][2], coords[k][3], pass ? .69f : 1.2f,
                       pass ? (wide ? 3 : 0) : 1);
}

static void Vitality(Brush *b, unsigned id)
{
    bool empty = id >= 8;
    b->top = (id >= 3 && id <= 5) || id == 7 || id >= 10 ? 8 : 0;
    b->left = id == 1 || id == 4 || id == 9 || id == 11 ? 8 : 0;
    /* Alternating cells cross two neighbouring fruits. */
    for (int copy = -1; copy <= 0; ++copy) {
        if ((id == 0 || id == 3 || id == 8 || id == 10) && copy == -1) continue;
        if ((id == 6 || id == 7) && copy == 0) continue;
        float x = 16 * copy;
        Oval(b, x + 6, 2.1f, 14.3f, 11.4f, 3, 3);
        if (empty) continue;
        b->pixels[4] = MixRgb(b->pixels[2], b->pixels[1], 112);
        b->pixels[5] = MixRgb(b->pixels[3], b->pixels[2], 112);
        b->pixels[6] = MixRgb(b->pixels[3], b->pixels[1], 108);
        PaintedOval(b, x + 11.35f, 6.7f, 6.35f, 5.3f, -.08f, 5, 2, 4);
        Oval(b, x + 6.9f, 4.1f, 1.2f, 1.9f, 5, 2);
        Stroke(b, x + 11.4f, 2.6f, x + 8.1f, 4.1f, .42f, 6);
        Stroke(b, x + 11.4f, 2.6f, x + 10.5f, 4.8f, .45f, 6);
        Stroke(b, x + 11.4f, 2.6f, x + 14.4f, 3.9f, .44f, 6);
        Stroke(b, x + 11.4f, 2.5f, x + 10.3f, 1.4f, .35f, 3);
        Stroke(b, x + 11.4f, 2.5f, x + 12.4f, .7f, .54f, 3);
    }
}

/* Hand-lettered title strokes in a 9 by 9 design space. Each letter has its
 * own slant, baseline and pen weight to follow the original wordmark. */
typedef struct TitleGlyph {
    char letter;
    float x, y, w, h, slant, radius;
} TitleGlyph;

static const TitleGlyph kGoldLogo[] = {
    {'K', 64, 28, 14, 40, -.15f, 3.65f}, {'I', 87, 31, 3, 30, -.19f, 3.5f},
    {'R', 94, 27, 17, 33, -.07f, 2.8f},  {'B', 118, 26, 17, 34, .09f, 2.8f},
    {'Y', 142, 28, 14, 33, .03f, 3.8f},  {'\'', 173, 19, 2, 11, -.12f, 2.4f},
    {'S', 165, 38, 22, 29, .13f, 3.4f}};
static const TitleGlyph kRedLogo[] = {
    {'D', 21, 80, 13, 23, -.13f, 3.6f},  {'R', 43, 77, 12, 23, .04f, 3.4f},
    {'E', 60, 82, 10, 22, .19f, 3.4f},   {'A', 75, 82, 14, 24, .08f, 3.6f},
    {'M', 93, 87, 16, 22, -.14f, 3.4f},  {'C', 127, 81, 14, 23, .05f, 3.7f},
    {'O', 145, 77, 16, 22, -.10f, 3.6f}, {'U', 166, 79, 12, 22, .02f, 3.7f},
    {'R', 183, 79, 11, 22, .01f, 3.4f},  {'S', 197, 77, 12, 24, -.08f, 3.6f},
    {'E', 214, 84, 18, 22, .22f, 3.4f}};

static const char *TitlePath(char c)
{
    switch (c) {
    case 'K':
        return "1019 801489";
    case 'I':
    case '\'':
        return "4049";
    case 'R':
        return "191040q9092q944414 4489";
    case 'B':
        return "191040q8082q844414 1444q9597q995919";
    case 'Y':
        return "004490 4449";
    case 'S':
        return "81q8050q0013q1455q9688q7949q1907";
    case 'D':
        return "090030q8094q993909";
    case 'E':
        return "80101989 1464";
    case 'A':
        return "094099 2676";
    case 'M':
        return "0900479099";
    case 'C':
        return "81q7040q0004q0949q6978";
    case 'O':
        return "40q9094q9949q0904q0040";
    case 'U':
        return "0005q0949q999590";
    default:
        return "";
    }
}

static void LogoGlyph(Brush *b, const TitleGlyph *g, bool gold)
{
    float r = gold ? g->radius : g->radius * .78f;
    if (g->x + g->w + r + 7 < b->left || g->x - r - 7 > b->left + 8 || g->y + g->h + r + 4 < b->top ||
        g->y - r > b->top + 8)
        return;
    float dist[1024], shadow[1024];
    for (int i = 0; i < 1024; ++i)
        dist[i] = shadow[i] = 100;
    const char *p = TitlePath(g->letter);
    while (*p) {
        float ax = p[0] - '0', ay = p[1] - '0';
        p += 2;
        while (*p && *p != ' ') {
            bool curve = *p == 'q';
            if (curve) ++p;
            float cx = p[0] - '0', cy = p[1] - '0';
            p += 2;
            float ex = cx, ey = cy;
            if (curve) {
                ex = p[0] - '0';
                ey = p[1] - '0';
                p += 2;
            }
            float xx = ax, yy = ay;
            for (int j = 1; j <= (curve ? 8 : 1); ++j) {
                float t = curve ? j / 8.f : 1, u = 1 - t;
                float nx = u * u * ax + 2 * u * t * cx + t * t * ex,
                      ny = u * u * ay + 2 * u * t * cy + t * t * ey;
                float x0 = g->x + xx * g->w / 9 + g->slant * (4.5f - yy) * g->h / 9;
                float x1 = g->x + nx * g->w / 9 + g->slant * (4.5f - ny) * g->h / 9;
                float y0 = g->y + yy * g->h / 9, y1 = g->y + ny * g->h / 9;
                for (int y = 0; y < 32; ++y)
                    for (int x = 0; x < 32; ++x) {
                        float px = b->left + (x + .5f) / 4, py = b->top + (y + .5f) / 4;
                        int n = y * 32 + x;
                        dist[n] = fminf(dist[n], SdSegment(px, py, x0, y0, x1, y1));
                        shadow[n] = fminf(shadow[n], SdSegment(px - 1.05f, py - 1.35f, x0, y0, x1, y1));
                    }
                xx = nx;
                yy = ny;
            }
            ax = ex;
            ay = ey;
        }
        if (*p) ++p;
    }
    for (int i = 0; i < 1024; ++i) {
        float v = (b->top + (i / 32 + .5f) / 4 - g->y) / g->h;
        uint32_t color = MixRgb(b->pixels[gold ? 4 : 6], b->pixels[gold ? 2 : 8], UnitByte(v));
        Ink(&b->out[i], b->pixels[15], UnitByte(.5f - 4 * (shadow[i] - r - .5f)));
        Ink(&b->out[i], b->pixels[gold ? 3 : 9], UnitByte(.5f - 4 * (dist[i] - r - .35f)));
        Ink(&b->out[i], color, UnitByte(.5f - 4 * (dist[i] - r)));
    }
}

static void TitleLettering(Brush *b, unsigned pal)
{
    if (pal == 5) {
        /* Kirby floating with raised arms and one foot swinging forward. */
        Oval(b, 86, 174, 27, 23, 5, 5);
        Oval(b, 87, 175, 25, 21, 14, 15);
        Oval(b, 82.7f, 127, 25, 27, 5, 5);
        Oval(b, 84, 128, 22.7f, 24.6f, 7, 9);
        Oval(b, 135, 129, 20, 24, 5, 5);
        Oval(b, 136, 130, 17.5f, 21, 7, 9);
        Oval(b, 90, 121.4f, 57.4f, 62.7f, 5, 5);
        Oval(b, 91.6f, 123, 54.2f, 59.4f, 7, 9);
        Oval(b, 126.5f, 158, 31, 27, 5, 5);
        Oval(b, 128, 159.4f, 28, 24, 14, 15);
        for (int i = 0; i < 2; ++i) {
            float x = 119 + i * 9;
            Oval(b, x, 133, 3.5f, 9, 5, 5);
            Oval(b, x + .7f, 134, 1.5f, 3, 1, 1);
        }
        Oval(b, 112, 143, 5.7f, 2.2f, 10, 9);
        Oval(b, 135, 142.5f, 5.7f, 2.2f, 10, 9);
        Stroke(b, 120.8f, 146, 122.2f, 147.4f, .58f, 5);
        Stroke(b, 122.2f, 147.4f, 124.3f, 147.6f, .58f, 5);
        Stroke(b, 124.3f, 147.6f, 126, 146, .58f, 5);
        return;
    }
    const TitleGlyph *letters = pal == 3 ? kGoldLogo : kRedLogo;
    size_t count = pal == 3 ? sizeof(kGoldLogo) / sizeof(*letters) : sizeof(kRedLogo) / sizeof(*letters);
    for (size_t i = 0; i < count; ++i)
        LogoGlyph(b, &letters[i], pal == 3);
}

static void SpinDial(Brush *b, const PpuHdTileQuery *q)
{
    Box(b, 0, 0, 48, 48, 1.5f, 11, 11);
    Box(b, .8f, .8f, 46.4f, 46.4f, 1, 14, 12);
    Box(b, 3.6f, 3.6f, 40.8f, 36.6f, 3, 11, 11);
    Box(b, 4.3f, 4.4f, 39.4f, 35, 2.7f, 14, 12);
    Oval(b, 5.2f, 4.6f, 37.6f, 36.6f, 11, 11);
    Oval(b, 6, 5.4f, 36, 35, 8, 6);
    /* The bar may be absent or animated. Its center-line ink is present
     * in the queried character only while that portion of the bar is drawn. */
    uint8_t px[64];
    DecodeWords(q->words, 4, px);
    if (b->top == 16 || b->top == 24) {
        bool bar = false;
        for (int k = 0; k < 64; ++k)
            if (px[k] == 9 || px[k] == 10 || px[k] == 15) bar = true;
        if (bar) {
            Box(b, 10, 21.5f, 28, 4, 1.8f, 11, 11);
            Box(b, 10.7f, 22.1f, 26.6f, 2.7f, 1.3f, 15, 9);
            for (int i = 0; i < 7; ++i)
                Stroke(b, 12 + i * 4, 23.1f, 12 + i * 4, 24, .22f, 10);
        }
    }
}

static bool MeterPiece(Brush *b, unsigned id)
{
    if (id == 1 || id == 2) {
        Spark(b, 3.7f, 3.7f, id == 1 ? 3 : 2.2f, 5);
        return true;
    }
    if (id >= 3 && id <= 7) {
        unsigned c = id == 7 ? 4 : id == 3 ? 14 : id == 4 ? 13 : 12;
        if (id == 6) {
            Oval(b, 3.1f, 3.1f, 1.4f, 1.4f, c, c);
            return true;
        }
        /* A five-point star, with a raised center. */
        for (int i = 0; i < 5; ++i) {
            float a = -1.5708f + i * 1.256637f;
            Stroke(b, 4, 4, 4 + 3 * cosf(a), 4 + 3 * sinf(a), .7f, c);
        }
        Oval(b, 2.7f, 2.7f, 2.6f, 2.6f, c + 1, c);
        return true;
    }
    if (id == 0x32 || id == 0x33 || id == 0x44 || id == 0x45) {
        b->left = (id & 1) * 8;
        b->top = id >= 0x44 ? 8 : 0;
        Box(b, 0, 0, 16, 16, 1.3f, 11, 11);
        Box(b, .8f, .75f, 14.4f, 14.5f, 1, 14, 12);
        Box(b, 1.7f, 3, 12.6f, 10, .9f, 11, 11);
        Box(b, 2.3f, 4, 11.4f, 8, 1, 15, 9);
        return true;
    }
    if (id == 0x34 || id == 0x42 || id == 0x35 || id == 0x36 || id == 0x3b || id == 0x3c || id == 0x3d ||
        id == 0x43) {
        bool left = id == 0x34 || id == 0x42;
        Box(b, -1, -1, 10, 10, 0, 1, 1);
        Box(b, left ? 0 : 2, -1, left ? 2 : 6, 10, 0, 11, 14);
        Stroke(b, left ? 1.3f : 4.8f, -1, left ? 1.3f : 4.8f, 9, .25f, 10);
        if (!left) {
            float pos = id == 0x35 || id == 0x43 ? 7.3f
                        : id == 0x3b             ? 3.4f
                        : id == 0x3c             ? 2.4f
                        : id == 0x3d             ? .5f
                                                 : -2;
            Box(b, 0, pos, 2.2f, .7f, .3f, 15, 10);
        }
        return true;
    }
    if (id == 0x37 || id == 0x38) {
        b->top = id == 0x38 ? 8 : 0;
        Box(b, 2, 2, 4, 14, 1.2f, 10, 10);
        Box(b, 2.6f, 2.7f, 2.8f, 12.6f, .8f, 15, 13);
        return true;
    }
    if (id >= 0x46 && id <= 0x4d) {
        float height = id - 0x45;
        Box(b, 1, 8 - height, 6, height + 1, .55f, 14, 11);
        Stroke(b, 2, 8.4f - height, 5.6f, 8.4f - height, .16f, 15);
        return true;
    }
    if (id == 0x39 || id == 0x3a) {
        Box(b, .4f, 2, 7.2f, 4, 2, 10, 10);
        Box(b, 1, 2.55f, 6, 2.9f, 1.4f, id == 0x39 ? 5 : 6, 4);
        return true;
    }
    return false;
}

static void PanelRail(Brush *b, unsigned id)
{
    Box(b, -1, -1, 10, 10, 0, 1, 1);
    bool right = id == 2 || id == 5 || id == 7, bottom = id >= 3 && id <= 5;
    float cx = right ? 4.6f : 3.4f, cy = bottom ? 4.6f : 3.4f;
    for (int j = 0; j < 32; ++j)
        for (int i = 0; i < 32; ++i) {
            float x = (i + .5f) / 4, y = (j + .5f) / 4;
            float u = (right ? 1 : -1) * (x - cx), v = (bottom ? 1 : -1) * (y - cy);
            float d = id == 1 || id == 4 ? v
                      : id >= 6          ? u
                                         : hypotf(fmaxf(0, u), fmaxf(0, v)) + fminf(0, fmaxf(u, v));
            Ink(&b->out[j * 32 + i], b->pixels[2], UnitByte(.5f - 4 * (fabsf(d - 2) - .52f)));
        }
}

static void TitleStar(Brush *b, unsigned size)
{
    const float cx = size == 0 ? 14.3f : size == 1 ? 8.8f : 6.5f;
    const float cy = size == 0 ? 13.5f : size == 1 ? 11.2f : 6.5f;
    const float r = size == 0 ? 9.5f : size == 1 ? 7.1f : 5.5f;
    float px[10], py[10];
    for (int i = 0; i < 10; ++i) {
        float angle = -1.18f + i * .62831853f;
        float ray = (i & 1) ? r * .52f : r;
        px[i] = cx + ray * cosf(angle);
        py[i] = cy + ray * sinf(angle);
    }
    unsigned color = 3 + size;
    for (int j = 0; j < 32; ++j)
        for (int i = 0; i < 32; ++i) {
            float x = b->left + (i + .5f) / 4, y = b->top + (j + .5f) / 4;
            float d = 100;
            bool inside = false;
            for (int k = 0, m = 9; k < 10; m = k++) {
                d = fminf(d, SdSegment(x, y, px[k], py[k], px[m], py[m]));
                if ((py[k] > y) != (py[m] > y) && x < (px[m] - px[k]) * (y - py[k]) / (py[m] - py[k]) + px[k])
                    inside = !inside;
            }
            unsigned shine = UnitByte(.35f * fmaxf(0, 1 - hypotf((x - cx) / r, (y - cy + 2) / r)));
            Ink(&b->out[j * 32 + i], MixRgb(b->pixels[color], b->pixels[1], shine),
                UnitByte(.5f + 4 * ((inside ? d : -d) + .35f)));
        }
}

static void TitleRail(Brush *b, unsigned id)
{
    if (id == 0) {
        Box(b, 0, 0, 18, 18, 2.4f, 7, 7);
        Box(b, .8f, .8f, 17, 17, 2, 8, 8);
        Stroke(b, 2, 13, 13, 2, .55f, 15);
        Oval(b, 2.5f, 2.5f, 2.5f, 2.5f, 1, 15);
        return;
    }
    bool vertical = id == 3 || id == 4 || id == 7 || id == 8;
    unsigned panel = (id == 1 || id == 3 || id == 6 || id == 8) ? 1 : 3;
    Box(b, -1, -1, 10, 10, 0, panel, panel);
    if (vertical) {
        Box(b, -1, -1, 4, 10, 0, 8, 15);
        Stroke(b, .4f, -1, .4f, 9, .4f, 7);
        if (id == 3 || id == 7) Stroke(b, 7.7f, -.3f, 2, 5.4f, .7f, 15);
    } else {
        Box(b, -1, -1, 10, 4, 0, 8, 15);
        Stroke(b, -1, .4f, 9, .4f, .4f, 7);
        if (id == 1 || id == 5) Stroke(b, -.3f, 7.7f, 5.4f, 2, .7f, 15);
    }
}

static bool PaintUi(const UiTileEntry *e, Brush *b, const PpuHdTileQuery *q)
{
    unsigned id = e->param;
    switch (e->kind) {
    case kUiBoxBorder:
        PanelRail(b, id);
        return true;
    case kUiTitleRail:
        TitleRail(b, id);
        return true;
    case kUiTitleStar:
        TitleStar(b, id);
        return true;
    case kUiMemberAction: {
        static const char *const labels[] = {"MEMBERS", "NEW MEMBER", "ERASE", "RANKING"};
        static const float widths[] = {104, 128, 80, 96};
        unsigned c = 1 + 3 * (id % 2);
        Word(b, labels[id], 1.4f, .95f, widths[id] - 3, 16, 1.9f, c);
        Word(b, labels[id], 1, .45f, widths[id] - 3, 16, .85f, c + 2);
        return true;
    }
    case kUiPointsDigit:
        Box(b, -1, -1, 10, 15, 0, 2, 2);
        Box(b, -1, 14, 10, 1, 0, 1, 1);
        Letter(b, '0' + id, .5f, .5f, 7, 13.7f, 1.7f, 1);
        Letter(b, '0' + id, .5f, .2f, 7, 13.7f, .6f, 3);
        return true;
    case kUiMenuBanner: {
        float width = id ? 136 : 160;
        if (id) {
            Box(b, -2, 0, width + 4, 16, 1, 8, 8);
            Box(b, -2, .8f, width + 4, 14.4f, 1, 9, 4);
        } else {
            Box(b, -1, -1, width + 2, 18, 0, 10, 10);
            Box(b, -1, 2, width + 2, 12, 0, 9, 9);
            Box(b, -1, 3, width + 2, 10, 0, 15, 15);
        }
        const char *name = id ? "NAME ENTRY" : "MEMBERS LIST";
        Word(b, name, 3.3f, .7f, width - 6, 16, 1.4f, id ? 13 : 12);
        Word(b, name, 3, .1f, width - 6, 16, .55f, id ? 15 : 14);
        return true;
    }
    case kUiTitle:
        if (q->layer != 0) return false;
        TitleLettering(b, id);
        return true;
    case kUiVitality:
        Vitality(b, id);
        return true;
    case kUiShotDigit:
    case kUiHoleDigit:
        DisplayDigit(b, id, e->kind == kUiHoleDigit);
        return true;
    case kUiStock:
        Box(b, -1, 1, 10, 6, 0, 3, 3);
        Letter(b, '0' + id, .25f, -.4f, 7.5f, 8.5f, 1.4f, 1);
        Letter(b, '0' + id, .25f, -.45f, 7.5f, 8.5f, .40f, 2);
        return true;
    case kUiSpinGauge:
        SpinDial(b, q);
        return true;
    case kUiHudObj:
        return MeterPiece(b, id);
    case kUiFontV1:
    case kUiFontV2: {
        char ch = id >= 0x21 && id <= 0x3a   ? (char)('A' + id - 0x21)
                  : id >= 0x51 && id <= 0x6a ? (char)('a' + id - 0x51)
                  : id >= 16 && id <= 25     ? (char)('0' + id - 16)
                  : id == 0x20               ? '@'
                  : id == 0x1c               ? '+'
                  : id == 0x1d               ? '-'
                  : id == 0x6b || id == 0x6c ? ','
                  : id == 9                  ? '!'
                  : id < 9                   ? " #%()*/:."[id]
                  : id >= 10 && id < 16      ? "<=>?[]"[id - 10]
                                             : 0;
        if (!Glyph(ch) && id != 0x40) return false;
        if (e->kind == kUiFontV2) Box(b, -1, -1, 10, 10, 0, 1, 1);
        if (id == 0x40) {
            for (int j = 0; j < 32; ++j)
                for (int i = 0; i < 32; ++i) {
                    float x = (i + .5f) / 4, y = (j + .5f) / 4;
                    Ink(&b->out[j * 32 + i], b->pixels[3],
                        UnitByte(4 * fminf(x - .6f, 6.7f - x - 1.65f * fabsf(y - 3.8f))));
                }
        } else {
            MenuGlyph(b, ch);
        }
        return true;
    }
    case kUiCreditsV1:
    case kUiCreditsV2: {
        const char *s;
        unsigned tile;
        float w;
        if (id < 15) {
            s = "HAL Laboratory,Inc.";
            tile = id;
            w = 119;
        } else if (id == 15) {
            s = "C";
            tile = 0;
            w = 7;
        } else if (id <= 22) {
            s = "Nintendo";
            tile = id - 16;
            w = 55;
        } else if (id >= 25 && id <= 30) {
            s = id < 27 ? "19" : id < 29 ? "94" : "95";
            tile = (id - 25) % 2;
            w = id < 27 ? 16 : 14.5f;
        } else
            return false;
        b->left = tile * 8;
        if (e->kind == kUiCreditsV2) Box(b, -1, -1, w + 2, 10, 0, 1, 1);
        unsigned fg = e->kind == kUiCreditsV2 ? 3 : 1;
        unsigned shadow = e->kind == kUiCreditsV2 ? 1 : 2;
        if (id == 15) {
            Letter(b, 169, .1f, .15f, 7.6f, 7.7f, .15f, fg);
        } else {
            Word(b, s, .85f, .7f, w - 1.45f, 7.4f, .49f, shadow);
            Word(b, s, .50f, .05f, w - 1.45f, 7.4f, .49f, fg);
        }
        return true;
    }
    case kUiAbilityPortrait:
        Portrait(b, id);
        return true;
    case kUiObjTomato:
        Tomato(b, id);
        return true;
    case kUiHud1pCorner:
        if (id == 2)
            Box(b, 3, 1, 12, 6, 3, 3, 3);
        else if (id == 3)
            Box(b, 0, 1, 8, 6, .3f, 3, 3);
        else {
            if (id) b->left = 8;
            Box(b, 0, 0, 16, 16, 2.3f, 1, 1);
            Box(b, 1.8f, 1.8f, 12.4f, 12.4f, 2, 2, 2);
            Box(b, 3.8f, 3.8f, 8.4f, 8.4f, 2.4f, 0, 0);
        }
        return true;
    case kUiHud2pCorner:
        Box(b, -1, 0, 10, 2, id == 2 ? 1.3f : 0, 1, 1);
        Box(b, -1, 2, 10, 2, 0, 2, 2);
        if (id != 2) Box(b, id == 3 ? 5 : 3, 5, 9, 6, 1.8f, 3, 3);
        return true;
    case kUiHud1pIcon:
        if (id) {
            b->left = (id - 1) * 8;
            Box(b, -1, 1, 18, 6, 0, 3, 3);
            Stroke(b, 4.6f, 2.65f, 11.5f, 2.65f, .52f, 1);
            Stroke(b, 4.6f, 5.35f, 11.5f, 5.35f, .52f, 1);
            return true;
        }
        Face(b, .2f, .15f, 7.55f, 3, 3, 1);
        return true;
    case kUiHud1pLabel:
        b->left = (id % 2) * 8;
        Box(b, -1, -1, 18, 10, 0, 2, 1);
        Box(b, 0, .8f, 16, 7.2f, .7f, 1, 1);
        Word(b, id < 2 ? "HOLE" : "SHOT", .55f, 1.35f, 15, 6.2f, .31f, id < 2 ? 3 : 0);
        return true;
    case kUiHud2pPlayer:
    case kUiHud2pPoint: {
        static const char *const difficulty[] = {"1", "2", "EASY", "MEDIUM", "HARD"};
        if (e->kind == kUiHud2pPoint) {
            Box(b, -1, -1, 34, 15, 0, 2, 2);
            Box(b, -1, 14, 34, 1, 0, 1, 1);
            Box(b, 5, 2.3f, 22, 9.2f, 1.8f, 1, 1);
            Word(b, "POINT", 6.2f, 2.35f, 19.8f, 9, .4f, 0);
        } else {
            Box(b, .35f, .65f, 31.3f, 14.6f, 1.5f, 1, 1);
            Word(b, id < 2 ? "PLAYER" : "CPU", 3.1f, 1.2f, 25.8f, 6.2f, .32f, 0);
            float w = id < 2 ? 6 : 23;
            Word(b, difficulty[id], (32 - w) / 2, 7.6f, w, 7, .38f, 0);
        }
        return true;
    }
    case kUiHud2pCapsule:
        Box(b, .15f, .35f, 7.7f, 7.3f, 2.1f, 1, 1);
        Box(b, .65f, .85f, 6.7f, 6.3f, 1.7f, id ? 3 : 2, 1);
        Box(b, 1.5f, 1.4f, 4.6f, .85f, .42f, 3, 2);
        return true;
    case kUiGuideNum: {
        char digit = (char)('1' + id);
        Letter(b, digit, .8f, 1.6f, 6.8f, 13, .94f, 1);
        Letter(b, digit, .45f, 1, 6.8f, 13, .59f, 15);
        return true;
    }
    case kUiObPopup:
        Word(b, "OB!", 1.3f, 1.8f, 30, 13, 1.45f, 1);
        Word(b, "OB!", .9f, 1, 30, 13, .92f, 15);
        return true;
    default:
        return false;
    }
}

#include "hd_ui_title.inc"

bool KdcHdUiRgba(const PpuHdTileQuery *q, uint32_t *pixels)
{
    if (!q || !pixels || q->scale != 4 || (q->bpp != 2 && q->bpp != 4)) return false;
    /* An installed texture pack replaces the drawn artwork; characters it
     * does not cover keep the game's own pixels. */
    if (KdcHdPackAvailable()) return KdcHdPackTile(q, pixels);
    uint64_t hash = HashTileWords(q->words, 4 * q->bpp);
    const UiTileEntry *e = NULL;
    unsigned tile = q->address / 16;
    if (q->layer == 0 && q->bpp == 4 && tile < sizeof(kTitleTiles) / sizeof(kTitleTiles[0]) &&
        kTitleTiles[tile].hash == hash)
        e = &kTitleTiles[tile];
    if (q->bpp == 4 && q->layer == 1 && tile >= 0x1c2 &&
        tile - 0x1c2 < sizeof(kTitleDecor) / sizeof(*kTitleDecor) && kTitleDecor[tile - 0x1c2].hash == hash)
        e = &kTitleDecor[tile - 0x1c2];
    if (q->bpp == 4 && q->layer == 0 && tile < sizeof(kMembersTitle) / sizeof(*kMembersTitle) &&
        kMembersTitle[tile].hash == hash)
        e = &kMembersTitle[tile];
    if (q->bpp == 4 && q->layer == 0 && tile >= 0x73 &&
        tile - 0x73 < sizeof(kMemberActions) / sizeof(*kMemberActions) &&
        kMemberActions[tile - 0x73].hash == hash)
        e = &kMemberActions[tile - 0x73];
    if (q->bpp == 4 && q->address >= 0x3000) {
        tile = (q->address - 0x3000) / 16;
        if (q->layer == 1 && tile < sizeof(kEntryTitle) / sizeof(kEntryTitle[0]) &&
            kEntryTitle[tile].hash == hash)
            e = &kEntryTitle[tile];
    }
    if (!e && ((q->bpp == 2 && q->layer == 2) || (q->bpp == 4 && q->layer == 4)))
        e = LookupUiEntry(hash, q->bpp);
    if (!e) return false;
    Brush b = {.out = pixels, .left = e->tx * 8, .top = e->ty * 8};
    b.pixels[0] = 0x01000000u; /* Punch through to the window backing. */
    for (unsigned i = 1; i < (1u << q->bpp); ++i) {
        unsigned c = q->colors[i];
        unsigned rgb = ((c & 31) << 19) | ((c & 992) << 6) | ((c >> 10 & 31) << 3);
        b.pixels[i] = rgb | ((rgb >> 5) & 0x070707);
    }
    memset(pixels, 0, 1024 * sizeof(*pixels));
    return PaintUi(e, &b, q);
}

uint32_t KdcHdUiBlend(uint32_t background, uint32_t ink)
{
    const unsigned a = ink >> 24;
    if (a == 255u) return ink;
    if (!a) return background;
    const unsigned inv = 255u - a;
    const unsigned r = Div255(((ink >> 16) & 255u) * a + ((background >> 16) & 255u) * inv);
    const unsigned g = Div255(((ink >> 8) & 255u) * a + ((background >> 8) & 255u) * inv);
    const unsigned b = Div255((ink & 255u) * a + (background & 255u) * inv);
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

void KdcHdUiGlyph(unsigned char ch, uint32_t color, uint32_t *pixels)
{
    Brush b = {.out = pixels};
    b.pixels[1] = 0x161038;
    b.pixels[3] = color & 0xffffff;
    memset(pixels, 0, 1024 * sizeof(*pixels));
    MenuGlyph(&b, ch);
}
