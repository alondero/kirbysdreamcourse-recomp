#include "course_orbit.h"
#include "course_camera.h"
#include "course_geometry.h"
#include "course_view.h"
#include "course_sprites.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern Snes *g_snes;
extern uint8_t g_ram[0x20000];

static KdcCourseCamera camera = {.zoom = 1};
static unsigned map_id = ~0u;
static double focus_height;
static bool focus_ready;
static unsigned focus_player = ~0u;

typedef struct Vertex {
    double x, y, z;
    double texture_x, texture_y;
} Vertex;

typedef struct Render {
    uint8_t *pixels;
    size_t pitch;
    int width, height;
    float *depth;
    Ppu *ppu;
    KdcCourseGeometry geometry;
} Render;

static float depth_buffer[kPpuBufWidth * 240];
static KdcOrbitSprite sprite_buffer[4096];
static unsigned sprite_count;

void KdcOrbitReset(void)
{
    KdcCameraReset(&camera);
    map_id = ~0u;
    focus_height = 0;
    focus_ready = false;
    focus_player = ~0u;
}

/* Player ball U/V: signed integer cells with 16-bit fractions. */
static double BallCoordinate(unsigned player, unsigned low)
{
    return (int16_t)KdcReadWord(g_ram, low + 4 + player) +
           KdcReadWord(g_ram, low + player) / 65536.0;
}

/* Player ball height in elevation steps: a 16.16 value at $D8BE/$D8BA in
 * which one terrain step is $5A82 (23170) units, as in course_geometry.c. */
static double BallHeight(unsigned player)
{
    return ((int16_t)KdcReadWord(g_ram, 0xd8be + player) * 65536.0 +
            KdcReadWord(g_ram, 0xd8ba + player)) / 23170.0;
}

void KdcOrbitInput(int horizontal, int vertical, bool reset)
{
    if (!g_snes || !g_snes->ppu || !KdcIsCourse(g_snes->ppu)) {
        KdcOrbitReset();
        return;
    }
    unsigned current = KdcReadWord(g_ram, 0xabb2);
    if (current != map_id) {
        KdcOrbitReset();
        map_id = current;
    }
    const double seconds = 1.0 / 60.0988;
    bool was_active = camera.active;
    KdcCameraStep(&camera, horizontal, vertical, reset, seconds);
    if (reset || (!was_active && camera.active)) focus_ready = false;
    unsigned player = KdcReadWord(g_ram, 0x1803);
    if (!camera.active || (player != 0 && player != 2)) return;
    unsigned proxy = KdcReadWord(g_ram, 0x6b4e);
    bool ball_ready = (int16_t)KdcReadWord(g_ram, 0x87e + player) >= 0;
    bool proxy_ready = proxy < 128 && !(proxy & 1) &&
                       KdcReadWord(g_ram, 0x4e1c + proxy) == 0x1a;
    /* Course PPU registers appear before the hole loader has initialized
     * the ball. Do not adopt its temporary zero-height pivot. */
    if (!ball_ready && !proxy_ready) return;
    /* Orbit at the ground beneath the player, not the zero-height plane
     * far below an elevated course or the tee's height after a shot to
     * another level. Ground (not ball) height keeps bounces from shaking
     * the course; easing hides cliff steps. Over void, keep the last. */
    double z = BallHeight(player), ground;
    KdcCourseGeometry geometry;
    if (g_snes->cart && KdcCourseGeometryInit(&geometry, g_ram, sizeof(g_ram),
            g_snes->cart->rom, g_snes->cart->romImageSize) &&
        KdcCourseGeometrySample(&geometry, BallCoordinate(player, 0xd8aa),
                                BallCoordinate(player, 0xd8b2), &ground))
        z = ground;
    else if (focus_ready && player == focus_player)
        z = focus_height;
    if (!(z >= -2 && z <= 200)) return;
    if (!focus_ready || player != focus_player)
        focus_height = z;
    else
        focus_height += (z - focus_height) * (1 - exp(-seconds / 0.1));
    focus_ready = true;
    focus_player = player;
}

static uint32_t Color(unsigned rgb15)
{
    unsigned r = rgb15 & 31, g = (rgb15 >> 5) & 31, b = (rgb15 >> 10) & 31;
    return 0xff000000u | ((r << 3 | r >> 2) << 16) |
           ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
}

static unsigned TilePixel(const uint16_t *vram, unsigned tile,
                           unsigned base, int x, int y, unsigned bpp)
{
    if (tile & 0x4000) x = 7 - x;
    if (tile & 0x8000) y = 7 - y;
    unsigned address = (base + (tile & 1023) * (bpp * 4) + y) & 0x7fff;
    uint32_t plane = vram[address];
    if (bpp == 4) plane |= (uint32_t)vram[(address + 8) & 0x7fff] << 16;
    plane >>= 7 - x;
    return (plane & 1) | ((plane >> 7) & 2) |
           ((plane >> 14) & 4) | ((plane >> 21) & 8);
}

static uint32_t TerrainColor(const Render *render, double u, double v,
                              uint32_t fallback)
{
    int x = (int)floor(u), y = (int)floor(v);
    if (x < 0 || y < 0) return fallback;
    const uint16_t *vram = PpuRenderVram(render->ppu);
    unsigned words[2], pixels[2];
    for (unsigned layer = 0; layer < 2; ++layer) {
        words[layer] = KdcCourseTile(g_ram, layer, x / 8, y / 8);
        pixels[layer] = TilePixel(vram, words[layer], 0, x & 7, y & 7, 4);
    }
    int layer = pixels[0] ? 0 : 1;
    if (pixels[1] && (words[1] & 0x2000) && !(words[0] & 0x2000)) layer = 1;
    if (!pixels[layer]) return fallback;
    return Color(render->ppu->cgram[((words[layer] >> 10) & 7) * 16 + pixels[layer]]);
}

/* Texel footprint per output pixel, rounded: near 1x (above about 0.67x)
 * pixel art stays point-sampled and sharp; at most a 4x4 grid. */
static int Samples(double scale)
{
    return scale > 0 ? (int)fmax(1, fmin(4, floor(1 / scale + 0.5))) : 1;
}

static double Edge(KdcCameraPoint a, KdcCameraPoint b, double x, double y)
{
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

static void Triangle(Render *render, Vertex a, Vertex b, Vertex c,
                       bool textured, uint32_t color)
{
    KdcCameraPoint p[3] = {
        KdcCameraProject(&camera, a.x, a.y, a.z, render->width, render->height),
        KdcCameraProject(&camera, b.x, b.y, b.z, render->width, render->height),
        KdcCameraProject(&camera, c.x, c.y, c.z, render->width, render->height),
    };
    double area = Edge(p[0], p[1], p[2].x, p[2].y);
    if (fabs(area) < 0.000001) return;
    int left = (int)fmax(0, floor(fmin(p[0].x, fmin(p[1].x, p[2].x))));
    int right = (int)fmin(render->width - 1, ceil(fmax(p[0].x, fmax(p[1].x, p[2].x))));
    int top = (int)fmax(0, floor(fmin(p[0].y, fmin(p[1].y, p[2].y))));
    int bottom = (int)fmin(render->height - 1, ceil(fmax(p[0].y, fmax(p[1].y, p[2].y))));
    /* Zoomed out, one output pixel spans several source texels. Average a
     * grid across its footprint instead of point-sampling one, which makes
     * fine checkerboards shimmer. The texture is affine in screen space. */
    int samples = textured ? Samples(camera.zoom) : 1;
    for (int y = top; y <= bottom; ++y) {
        uint32_t *row = (uint32_t *)(render->pixels + y * render->pitch);
        for (int x = left; x <= right; ++x) {
            double w0 = Edge(p[1], p[2], x + 0.5, y + 0.5) / area;
            double w1 = Edge(p[2], p[0], x + 0.5, y + 0.5) / area;
            double w2 = 1 - w0 - w1;
            if (w0 < -0.000001 || w1 < -0.000001 || w2 < -0.000001) continue;
            double depth = w0 * p[0].depth + w1 * p[1].depth + w2 * p[2].depth;
            unsigned index = y * render->width + x;
            if (depth < render->depth[index]) continue;
            render->depth[index] = (float)depth;
            if (!textured) { row[x] = color; continue; }
            if (samples == 1) {
                row[x] = TerrainColor(render,
                    w0 * a.texture_x + w1 * b.texture_x + w2 * c.texture_x,
                    w0 * a.texture_y + w1 * b.texture_y + w2 * c.texture_y, color);
                continue;
            }
            unsigned r = 0, g = 0, blue = 0;
            for (int j = 0; j < samples; ++j) {
                for (int i = 0; i < samples; ++i) {
                    double sx = x + (i + 0.5) / samples, sy = y + (j + 0.5) / samples;
                    double v0 = Edge(p[1], p[2], sx, sy) / area;
                    double v1 = Edge(p[2], p[0], sx, sy) / area, v2 = 1 - v0 - v1;
                    uint32_t sample = TerrainColor(render,
                        v0 * a.texture_x + v1 * b.texture_x + v2 * c.texture_x,
                        v0 * a.texture_y + v1 * b.texture_y + v2 * c.texture_y, color);
                    r += (sample >> 16) & 255; g += (sample >> 8) & 255; blue += sample & 255;
                }
            }
            unsigned n = (unsigned)(samples * samples);
            row[x] = 0xff000000u | (r / n) << 16 | (g / n) << 8 | blue / n;
        }
    }
}

static void Terrain(Render *render)
{
    /* Outer and void-facing cliffs end at elevation zero, where the
     * original projected cliff artwork ends (measured against native 1-1). */
    const double base_height = 0;
    static const double corners[5][2] = {{0,0},{1,0},{1,1},{0,1},{0.5,0.5}};
    static const int neighbor_x[4] = {0,1,0,-1};
    static const int neighbor_y[4] = {-1,0,1,0};
    /* The original atlas only depicts the two camera-facing cliff sides.
     * Reuse their live palette for newly exposed backs; never bake artwork
     * or invent a per-course material index from the collision flags. */
    unsigned wall_r = 0, wall_g = 0, wall_b = 0, wall_samples = 0;
    for (unsigned y = 0; y < render->geometry.height; ++y) {
        for (unsigned x = 0; x < render->geometry.width; ++x) {
            for (unsigned edge = 0; edge < 2; ++edge) {
                double u = edge ? 1 : 0.5, v = edge ? 0.5 : 0, z, neighbor;
                if (!KdcCourseCellHeight(&render->geometry, x, y, u, v, &z)) continue;
                int nx = (int)x + neighbor_x[edge], ny = (int)y + neighbor_y[edge];
                if (nx >= 0 && ny >= 0 && KdcCourseCellHeight(&render->geometry,
                    nx, ny, x + u - nx, y + v - ny, &neighbor)) continue;
                if (z <= base_height) continue;
                double sx, sy;
                KdcCourseGeometryProject(&render->geometry, x + u, y + v,
                                         (z + base_height) * 0.5, &sx, &sy);
                uint32_t color = TerrainColor(render, sx, sy, 0);
                if (!color) continue;
                wall_r += (color >> 16) & 255; wall_g += (color >> 8) & 255;
                wall_b += color & 255; ++wall_samples;
            }
        }
    }
    uint32_t wall_color = wall_samples ? 0xff000000u |
        (wall_r / wall_samples << 16) | (wall_g / wall_samples << 8) |
        (wall_b / wall_samples) : 0xff5a5348;
    for (unsigned y = 0; y < render->geometry.height; ++y) {
        for (unsigned x = 0; x < render->geometry.width; ++x) {
            Vertex vertices[5];
            bool valid = true;
            for (unsigned i = 0; i < 5; ++i) {
                Vertex *v = &vertices[i];
                v->x = x + corners[i][0]; v->y = y + corners[i][1];
                if (!KdcCourseCellHeight(&render->geometry, x, y,
                    corners[i][0], corners[i][1], &v->z)) { valid = false; break; }
                KdcCourseGeometryProject(&render->geometry, v->x, v->y, v->z,
                                         &v->texture_x, &v->texture_y);
            }
            if (!valid) continue;
            uint32_t top_color = TerrainColor(render, vertices[4].texture_x,
                vertices[4].texture_y, 0xff739a52);
            for (unsigned i = 0; i < 4; ++i) {
                unsigned next = (i + 1) & 3;
                Triangle(render, vertices[i], vertices[next], vertices[4], true, top_color);
                /* Adjacent cells may meet at different heights. Build the
                 * vertical exposed face rather than stretching the top. */
                Vertex lower_a = vertices[i], lower_b = vertices[next];
                int nx = (int)x + neighbor_x[i], ny = (int)y + neighbor_y[i];
                double za = base_height, zb = base_height;
                if (nx >= 0 && ny >= 0) {
                    (void)KdcCourseCellHeight(&render->geometry, nx, ny,
                        lower_a.x - nx, lower_a.y - ny, &za);
                    (void)KdcCourseCellHeight(&render->geometry, nx, ny,
                        lower_b.x - nx, lower_b.y - ny, &zb);
                }
                lower_a.z = fmin(lower_a.z, za); lower_b.z = fmin(lower_b.z, zb);
                KdcCourseGeometryProject(&render->geometry, lower_a.x, lower_a.y, lower_a.z,
                                         &lower_a.texture_x, &lower_a.texture_y);
                KdcCourseGeometryProject(&render->geometry, lower_b.x, lower_b.y, lower_b.z,
                                         &lower_b.texture_x, &lower_b.texture_y);
                Triangle(render, vertices[i], lower_a, lower_b, i < 2, wall_color);
                Triangle(render, vertices[i], lower_b, vertices[next], i < 2, wall_color);
            }
        }
    }
}

static void Scenery(Render *render, bool hud)
{
    /* The same course scenery signature as the native wide margins. */
    if (render->ppu->bgXsc[2] != 0x3c || PPU_bgTileAdr(render->ppu, 2) != 0x4000 ||
        PPU_bigTiles(render->ppu, 2))
        return;
    const uint16_t *vram = PpuRenderVram(render->ppu);
    int extra = (render->width - 256) / 2;
    for (int y = 0; y < render->height; ++y) {
        uint32_t *row = (uint32_t *)(render->pixels + y * render->pitch);
        for (int x = 0; x < render->width; ++x) {
            int screen_x = x - extra;
            if (hud && (screen_x < 0 || screen_x >= 256)) continue;
            uint16_t scroll = render->ppu->hScroll[2];
            unsigned u = (unsigned)(screen_x + scroll) & 255;
            unsigned v = (y + render->ppu->vScroll[2] + 1) & 255;
            /* The clean map's opposite edges do not join. As in the native
             * widescreen margins, reflect whole tiles outside the field. */
            bool flip;
            unsigned column = KdcReflectedColumn(screen_x, scroll, &flip) & 31;
            unsigned index = (v / 8) * 32 + column;
            unsigned clean = KdcReadWord(g_ram, 0xdadc + 2 * index) ^ (flip ? 0x4000 : 0);
            unsigned live = vram[0x3c00 + index];
            if (hud && live == clean) continue;
            unsigned tile = hud ? live : clean;
            unsigned pixel = TilePixel(vram, tile, 0x4000, u & 7, v & 7, 2);
            if (pixel) row[x] = Color(render->ppu->cgram[((tile >> 10) & 7) * 4 + pixel]);
        }
    }
}

static void RestoreHud(Render *render, const uint8_t *field)
{
    if (!field || render->ppu->bgXsc[2] != 0x3c ||
        PPU_bgTileAdr(render->ppu, 2) != 0x4000) return;
    /* The game composites its HUD into BG3 over the clean scenery map.
     * Preserve the complete occupied horizontal span on each HUD row,
     * including transparent interiors that use the native PPU's color
     * math and object portraits. No fixed screen rectangle is assumed. */
    const uint16_t *vram = PpuRenderVram(render->ppu);
    int extra = (render->width - 256) / 2;
    for (int y = 0; y < render->height; ++y) {
        unsigned v = (y + render->ppu->vScroll[2] + 1) & 255;
        int left = 256, right = -1;
        for (int x = 0; x < 256; ++x) {
            unsigned u = (x + render->ppu->hScroll[2]) & 255;
            unsigned index = (v / 8) * 32 + u / 8;
            if (vram[0x3c00 + index] == KdcReadWord(g_ram, 0xdadc + 2 * index)) continue;
            if (x < left) left = x;
            right = x;
        }
        if (left > right) continue;
        left += extra; right += extra;
        if (left < 0) left = 0;
        if (right >= render->width) right = render->width - 1;
        if (left <= right)
            memcpy(render->pixels + y * render->pitch + left * 4,
                   field + ((size_t)y * render->width + left) * 4,
                   (size_t)(right - left + 1) * 4);
    }
}

static void CollectSprite(const KdcOrbitSprite *sprite, void *context)
{
    (void)context;
    if (sprite_count < sizeof(sprite_buffer) / sizeof(sprite_buffer[0]))
        sprite_buffer[sprite_count++] = *sprite;
}

static int SpriteSize(const Render *render, const KdcOrbitSprite *sprite)
{
    static const uint8_t sizes[8][2] = {
        {8,16}, {8,32}, {8,64}, {16,32}, {16,64}, {32,64}, {16,32}, {16,32}
    };
    return sizes[PPU_objSize(render->ppu)][sprite->large != 0];
}

/* Mode 1 OBJ priority against the course planes: priority 0/1 objects lie
 * behind every opaque BG1/BG2 pixel, priority 2 behind high-priority tiles
 * and priority 3 in front. The game uses this to mask artwork, such as the
 * cup's interior showing only through the hole in the floor tiles. */
static bool NativeMasked(const Render *render, unsigned priority, double x, double y)
{
    if (priority >= 3 || x < 0 || y < 0) return false;
    int px = (int)x, py = (int)y;
    const uint16_t *vram = PpuRenderVram(render->ppu);
    for (unsigned layer = 0; layer < 2; ++layer) {
        unsigned word = KdcCourseTile(g_ram, layer, px / 8, py / 8);
        if ((priority < 2 || (word & 0x2000)) &&
            TilePixel(vram, word, 0, px & 7, py & 7, 4)) return true;
    }
    return false;
}

/* Original OBJ color at an unflipped piece position; 0 when transparent or
 * masked. native is the anchor's original projected position, or NULL. */
static uint32_t SpriteColor(const Render *render, const KdcOrbitSprite *sprite,
                            int size, double source_x, double source_y,
                            const double *native)
{
    if (!(source_x >= 0 && source_x < size && source_y >= 0 && source_y < size)) return 0;
    int sx = (int)source_x, sy = (int)source_y;
    unsigned attr = sprite->attributes;
    if (native && NativeMasked(render, (attr >> 12) & 3,
            native[0] + sprite->offset_x + sx, native[1] + sprite->offset_y + sy))
        return 0;
    if (attr & 0x4000) sx = size - 1 - sx;
    if (attr & 0x8000) sy = size - 1 - sy;
    unsigned base = attr & 0x100 ? PPU_objTileAdr2(render->ppu) : PPU_objTileAdr1(render->ppu);
    unsigned tile = ((((attr & 255) >> 4) + (sy >> 3)) << 4) |
                    (((attr & 15) + (sx >> 3)) & 15);
    unsigned pixel = TilePixel(PpuRenderVram(render->ppu), tile, base, sx & 7, sy & 7, 4);
    return pixel ? Color(render->ppu->cgram[0x80 + 16 * ((attr >> 9) & 7) + pixel]) : 0;
}

/* Zoomed out, average the source texels under a pixel; it is drawn when at
 * least half of them are opaque. Otherwise this is a point sample. */
typedef struct Footprint { double x, y, step; int samples; } Footprint;
static uint32_t Filtered(const Render *render, const KdcOrbitSprite *sprite,
                         int size, Footprint f, const double *native)
{
    if (f.samples == 1) return SpriteColor(render, sprite, size, f.x, f.y, native);
    unsigned r = 0, g = 0, b = 0, opaque = 0;
    for (int j = 0; j < f.samples; ++j) {
        for (int i = 0; i < f.samples; ++i) {
            uint32_t c = SpriteColor(render, sprite, size,
                f.x + (i + 0.5 - f.samples * 0.5) * f.step,
                f.y + (j + 0.5 - f.samples * 0.5) * f.step, native);
            if (!c) continue;
            r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; ++opaque;
        }
    }
    if (opaque * 2 < (unsigned)(f.samples * f.samples)) return 0;
    return 0xff000000u | (r / opaque) << 16 | (g / opaque) << 8 | b / opaque;
}

/* Ground artwork (tee, shadows, guide ground marks) was drawn in the
 * original projection. Paint it onto the visible terrain exactly as the
 * native camera would have projected it there, like the terrain artwork
 * itself: it turns with the course and follows slopes instead of standing
 * as a billboard that terrain can cut through. Terrain depth is retained,
 * so later bodies stand in front, and nearer occluders hide the decal. */
static void Decal(Render *render, const KdcOrbitSprite *sprite)
{
    int size = SpriteSize(render, sprite);
    double native_x, native_y;
    KdcCourseGeometryProject(&render->geometry, sprite->x, sprite->y, sprite->z,
                             &native_x, &native_y);
    /* Screen bounds of the piece on the anchor plane, widened for slopes. */
    double left = DBL_MAX, right = -DBL_MAX, top = DBL_MAX, bottom = -DBL_MAX;
    for (unsigned corner = 0; corner < 4; ++corner) {
        double dx = sprite->offset_x + (corner & 1 ? size : 0);
        double dy = sprite->offset_y + (corner & 2 ? size : 0);
        KdcCameraPoint p = KdcCameraProject(&camera,
            sprite->x + (dx / 32 + dy / 16) / 2, sprite->y + (dx / 32 - dy / 16) / 2,
            sprite->z, render->width, render->height);
        left = fmin(left, p.x); right = fmax(right, p.x);
        top = fmin(top, p.y); bottom = fmax(bottom, p.y);
    }
    const double tolerance = 1.5; /* elevation steps from the anchor plane */
    double margin = 16 * tolerance * camera.zoom + 1;
    int x0 = (int)fmax(0, floor(left - 1)), x1 = (int)fmin(render->width, ceil(right + 1));
    int y0 = (int)fmax(0, floor(top - margin)), y1 = (int)fmin(render->height, ceil(bottom + margin));
    /* Source pixels spanned by one output pixel on a horizontal plane. Both
     * projections are affine, so this is the same at every pixel and height. */
    int samples = Samples(camera.zoom);
    double step = 1;
    if (samples > 1) {
        double p[3][2], unused;
        for (unsigned i = 0; i < 3; ++i) {
            double wx, wy;
            if (!KdcCameraUnprojectPlane(&camera, 0.5 + (i == 1), 0.5 + (i == 2),
                    sprite->z, render->width, render->height, &wx, &wy, &unused))
                return;
            KdcCourseGeometryProject(&render->geometry, wx, wy, sprite->z,
                                     &p[i][0], &p[i][1]);
        }
        step = fmax(hypot(p[1][0] - p[0][0], p[1][1] - p[0][1]),
                    hypot(p[2][0] - p[0][0], p[2][1] - p[0][1])) / samples;
    }
    const double native[2] = {native_x, native_y};
    for (int y = y0; y < y1; ++y) {
        uint32_t *row = (uint32_t *)(render->pixels + y * render->pitch);
        for (int x = x0; x < x1; ++x) {
            float depth = render->depth[y * render->width + x];
            double wx, wy, wz, unused;
            if (depth == -FLT_MAX) {
                /* Over the void (a ball leaving the course) keep the plane. */
                wz = sprite->z;
                if (!KdcCameraUnprojectPlane(&camera, x + 0.5, y + 0.5, wz,
                        render->width, render->height, &wx, &wy, &unused)) continue;
            } else if (!KdcCameraUnproject(&camera, x + 0.5, y + 0.5, depth,
                           render->width, render->height, &wx, &wy, &wz) ||
                       fabs(wz - sprite->z) > tolerance) continue;
            double sx, sy;
            KdcCourseGeometryProject(&render->geometry, wx, wy, wz, &sx, &sy);
            Footprint f = {sx - native_x - sprite->offset_x,
                           sy - native_y - sprite->offset_y, step, samples};
            uint32_t color = Filtered(render, sprite, size, f, native);
            if (color) row[x] = color;
        }
    }
}

static void Sprites(Render *render, bool world, bool ground)
{
    for (unsigned i = sprite_count; i > 0; --i) {
        const KdcOrbitSprite *sprite = &sprite_buffer[i - 1];
        if (sprite->world != world || (world && sprite->ground != ground)) continue;
        if (world && ground) { Decal(render, sprite); continue; }
        int size = SpriteSize(render, sprite);
        double scale = world ? camera.zoom : 1;
        double z = sprite->z, surface;
        /* Guest heights are fixed point; the mesh is continuous. Treat an
         * anchor within a quarter step below the surface as resting on it. */
        if (world && KdcCourseGeometrySample(&render->geometry, sprite->x, sprite->y, &surface) &&
            surface > z && surface - z <= 0.25) z = surface;
        KdcCameraPoint point = world ? KdcCameraProject(&camera,
            sprite->x, sprite->y, z, render->width, render->height) :
            (KdcCameraPoint){(render->width - 256) / 2.0, -1, 0};
        double left = point.x + sprite->offset_x * scale;
        double top = point.y + sprite->offset_y * scale;
        int samples = world ? Samples(scale) : 1;
        double native[2];
        if (world) KdcCourseGeometryProject(&render->geometry, sprite->x, sprite->y,
                                            sprite->z, &native[0], &native[1]);
        int x0 = (int)fmax(0, floor(left)), x1 = (int)fmin(render->width, ceil(left + size * scale));
        int y0 = (int)fmax(0, floor(top)), y1 = (int)fmin(render->height, ceil(top + size * scale));
        for (int y = y0; y < y1; ++y) {
            uint32_t *row = (uint32_t *)(render->pixels + y * render->pitch);
            /* Upright billboard above its anchor. Rows the original image
             * draws below the anchor (hole, shadows, feet) lie in front of
             * it on the ground plane instead of being buried beneath it. */
            double below = (y + 0.5 - point.y) / scale;
            double depth = point.depth + (below >= 0 ? 2 * below : -0.5 * below) + 0.25;
            for (int x = x0; x < x1; ++x) {
                uint32_t color = Filtered(render, sprite, size, (Footprint){
                    (x + 0.5 - left) / scale, (y + 0.5 - top) / scale, 1 / scale / samples, samples},
                    world ? native : NULL);
                if (!color) continue;
                unsigned index = y * render->width + x;
                /* Compare at buffer precision: equal depths keep OBJ order. */
                if (world && (float)depth < render->depth[index]) continue;
                row[x] = color;
                if (world) render->depth[index] = (float)depth;
            }
        }
    }
}

int KdcOrbitDraw(uint8_t *dst, size_t pitch, const uint8_t *field,
                  int width, int height, double alpha)
{
    (void)alpha;
    if (!camera.active || !focus_ready || !dst || width < 1 || width > kPpuBufWidth ||
        height < 1 || height > 240 || pitch < (size_t)width * 4 ||
        !g_snes || !g_snes->ppu || !g_snes->cart || !KdcIsCourse(g_snes->ppu)) return 0;
    /* Preserve original fades, forced blank and mosaic transitions until
     * the fully visible course returns. Camera state is retained. */
    if (PPU_forcedBlank(g_snes->ppu) || PPU_brightness(g_snes->ppu) != 15 ||
        PPU_mosaicSize(g_snes->ppu) > 1) return 0;
    Render render = {dst, pitch, width, height, depth_buffer, g_snes->ppu, {0}};
    if (!KdcCourseGeometryInit(&render.geometry, g_ram, sizeof(g_ram),
        g_snes->cart->rom, g_snes->cart->romImageSize)) return 0;
    double cx = KdcCourseScroll(render.ppu->hScroll[0], KdcReadWord(g_ram, 0x86)) + 128;
    /* Visible SNES scanlines begin at one, while the output rows begin at
     * zero. Preserve that phase for terrain and billboard origins. */
    double cy = KdcCourseScroll(render.ppu->vScroll[0], KdcReadWord(g_ram, 0x88)) + 113;
    double horizontal = (cx - render.geometry.origin_x) / 32;
    double vertical = (cy - render.geometry.origin_y) / 16;
    /* The native view center, on the plane beneath the player. */
    double center_x = (horizontal + vertical + focus_height) / 2;
    double center_y = (horizontal - vertical - focus_height) / 2;
    /* Pivot on the player's ground point. The original look-ahead from it
     * to the view center is not magnified when zooming in, so the player
     * stays in view; zoomed out it shrinks with the course, which stays
     * centered. It fades out as the camera turns from the original angle,
     * where its screen-space intent no longer applies: from a quarter turn
     * onwards the player is centered. At 1x and the original angle this is
     * exactly the native framing. */
    double ball_x = BallCoordinate(focus_player, 0xd8aa);
    double ball_y = BallCoordinate(focus_player, 0xd8b2);
    double lead = fmax(0, cos(camera.yaw)) * fmin(1, camera.zoom);
    camera.target_x = ball_x;
    camera.target_y = ball_y;
    camera.target_z = focus_height;
    camera.lead_x = (center_x - ball_x) * lead;
    camera.lead_y = (center_y - ball_y) * lead;
    /* The ground pivot keeps bounces from shaking the course, but zoomed
     * in a high ball would leave the frame. Shift the pivot height only as
     * far as needed to keep the ball and a 16-pixel sprite above it on
     * screen; raising it one step moves the view 16 pixels per zoom down. */
    KdcCameraPoint ball = KdcCameraProject(&camera, ball_x, ball_y,
        BallHeight(focus_player), width, height);
    double top = 8 + 16 * camera.zoom, bottom = height - 8.0;
    if (ball.y < top)
        camera.target_z += (top - ball.y) / (16 * camera.zoom);
    else if (ball.y > bottom)
        camera.target_z -= (ball.y - bottom) / (16 * camera.zoom);
    for (int y = 0; y < height; ++y) {
        uint32_t *row = (uint32_t *)(dst + y * pitch);
        for (int x = 0; x < width; ++x) {
            row[x] = Color(render.ppu->cgram[0]);
            depth_buffer[y * width + x] = -FLT_MAX;
        }
    }
    Scenery(&render, false);
    Terrain(&render);
    sprite_count = 0;
    KdcVisitOrbitSprites(render.ppu, g_ram, CollectSprite, NULL);
    Sprites(&render, true, true);
    Sprites(&render, true, false);
    Scenery(&render, true);
    Sprites(&render, false, false);
    RestoreHud(&render, field);
    return 1;
}
