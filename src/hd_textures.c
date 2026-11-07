#include "hd_textures.h"
#include "hd_scaler.h"
#include "course_sprites.h"
#include "course_view.h"
#include "launcher_settings.h"
#include "snes/ppu.h"
#include "snes/snes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern Snes *g_snes;
extern uint8_t g_ram[0x20000];
extern int snes_frame_counter;

bool KdcHdEnabled(void)
{
    const char *forced = getenv("KDC_HD");
    if (forced && *forced) return *forced != '0';
    return KdcHdTexturesEnabled();
}

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
    float luma[256] = {0};
    luma[0] = -1.0f;
    for (unsigned i = 1; i < (1u << q->bpp); ++i) luma[i] = KdcXbrLuma555(q->colors[i]);
    KdcXbrScale(pixels, 8, 8, luma, q->scale, indices);
    return true;
}

static bool course_active;

/* Screen-anchored sprite pieces (HUD portrait, shot meter) for the frame
 * being drawn, padded by a pixel; OAM Y displays one line lower. */
typedef struct { int x0, y0, x1, y1; } HudRect;
enum { kMaxHudRects = 512 };
static HudRect hud_rects[kMaxHudRects];
static unsigned hud_count;
static bool hud_overflow;
static int hud_frame = -1;

static void CollectHudSprite(const KdcOrbitSprite *sprite, void *context)
{
    static const uint8_t sizes[8][2] = {
        {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32}
    };
    const Ppu *ppu = context;
    if (sprite->world || sprite->unresolved) return;
    if (hud_count == kMaxHudRects) { hud_overflow = true; return; }
    const int size = sizes[PPU_objSize(ppu)][sprite->large != 0];
    hud_rects[hud_count++] = (HudRect){sprite->offset_x - 1, sprite->offset_y,
                                       sprite->offset_x + size, sprite->offset_y + size + 1};
}

static void Keep(int x0, int x1, int first_x, unsigned count, uint8_t *keep_native)
{
    for (int x = x0; x <= x1; ++x) {
        const int k = x - first_x;
        if (k >= 0 && k < (int)count) keep_native[k] = 1;
    }
}

/* Scope: live course terrain, scenery and sprites. Menus, course select and
 * scorecards keep their native art, as does the HUD: its BG3 panel spans
 * (the same rule the orbit camera uses) and its screen-anchored sprites. */
static bool HdLinePolicy(const Ppu *ppu, unsigned line, int first_x, unsigned count,
                         uint8_t *keep_native, void *context)
{
    (void)context;
    if (!course_active) return false;
    if (hud_frame != snes_frame_counter) {
        hud_frame = snes_frame_counter;
        hud_count = 0;
        hud_overflow = false;
        KdcVisitOrbitSprites(ppu, g_ram, CollectHudSprite, (void *)ppu);
    }
    if (hud_overflow) return false; /* cannot bound the HUD: keep it all */
    for (unsigned i = 0; i < hud_count; ++i)
        if ((int)line >= hud_rects[i].y0 && (int)line <= hud_rects[i].y1)
            Keep(hud_rects[i].x0, hud_rects[i].x1, first_x, count, keep_native);
    int left, right;
    if (ppu->bgXsc[2] == 0x3c && PPU_bgTileAdr(ppu, 2) == 0x4000 &&
        KdcHudSpan(PpuRenderVram(ppu) + 0x3c00, g_ram, (line + ppu->vScroll[2]) & 255,
                   (uint16_t)ppu->hScroll[2], &left, &right))
        Keep(left, right, first_x, count, keep_native);
    return true;
}

unsigned KdcHdScale(void)
{
    static Ppu *bound;
    static PpuHdTileFill *bound_fill;
    const bool want = KdcHdEnabled();
    Ppu *ppu = g_snes ? g_snes->ppu : NULL;
    const char *source = getenv("KDC_HD_SOURCE");
    PpuHdTileFill *fill = source && !strcmp(source, "nearest") ? KdcHdNearestTile : KdcHdSmoothTile;
    if (!want) fill = NULL;
    /* The PPU lives for the whole process; rebinding only follows the setting. */
    if (ppu != bound || fill != bound_fill) {
        if (ppu && (!PpuSetHdTiles(ppu, fill, NULL) ||
                    !PpuSetHdTilePolicy(ppu, fill ? HdLinePolicy : NULL, NULL)))
            fill = NULL;
        bound = ppu;
        bound_fill = fill;
    }
    /* Sampled between frames, like the other course presentation hooks. */
    course_active = ppu && KdcIsCourse(ppu);
    /* Developer diagnostic: KDC_HD_STATS=1 prints cumulative counters. */
    static int reported = -1;
    if (fill && ppu && snes_frame_counter % 300 == 0 && snes_frame_counter != reported &&
        getenv("KDC_HD_STATS")) {
        reported = snes_frame_counter;
        PpuHdTileStats s;
        PpuGetHdTileStats(ppu, &s);
        fprintf(stderr, "[hd] frame=%d hd_lines=%llu native_lines=%llu replaced=%llu "
                "fallback=%llu cached=%u\n", snes_frame_counter,
                (unsigned long long)s.hd_lines, (unsigned long long)s.native_lines,
                (unsigned long long)s.replaced_pixels, (unsigned long long)s.fallback_pixels,
                (unsigned)s.cached_tiles);
    }
    return fill ? kKdcHdScale : 1;
}
