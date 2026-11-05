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
    bool was_active = camera.active;
    KdcCameraStep(&camera, horizontal, vertical, reset, 1.0 / 60.0988);
    if (reset || (!was_active && camera.active)) focus_ready = false;
    unsigned player = KdcReadWord(g_ram, 0x1803);
    if (camera.active && (!focus_ready || player != focus_player)) {
        /* Orbit at the player's elevation, not the zero-height plane far
         * below an elevated course. Keep it fixed during airborne shots;
         * the original camera still supplies the horizontal tracking. */
        if (player == 0 || player == 2) {
            unsigned proxy = KdcReadWord(g_ram, 0x6b4e);
            bool ball_ready = (int16_t)KdcReadWord(g_ram, 0x87e + player) >= 0;
            bool proxy_ready = proxy < 128 && !(proxy & 1) &&
                               KdcReadWord(g_ram, 0x4e1c + proxy) == 0x1a;
            /* Course PPU registers appear before the hole loader has
             * initialized the ball. Do not freeze a zero-height pivot. */
            if (!ball_ready && !proxy_ready) return;
            double z = ((int16_t)KdcReadWord(g_ram, 0xd8be + player) * 65536.0 +
                        KdcReadWord(g_ram, 0xd8ba + player)) / 23170.0;
            if (z >= -2 && z <= 200) {
                focus_height = z;
                focus_ready = true;
                focus_player = player;
            }
        }
    }
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
            row[x] = textured ? TerrainColor(render,
                w0 * a.texture_x + w1 * b.texture_x + w2 * c.texture_x,
                w0 * a.texture_y + w1 * b.texture_y + w2 * c.texture_y, color) : color;
        }
    }
}

static void Terrain(Render *render)
{
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
                double sx, sy;
                KdcCourseGeometryProject(&render->geometry, x + u, y + v, z - 2, &sx, &sy);
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
                double za = -2, zb = -2;
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
    if (render->ppu->bgXsc[2] != 0x3c || PPU_bgTileAdr(render->ppu, 2) != 0x4000)
        return;
    const uint16_t *vram = PpuRenderVram(render->ppu);
    int extra = (render->width - 256) / 2;
    for (int y = 0; y < render->height; ++y) {
        uint32_t *row = (uint32_t *)(render->pixels + y * render->pitch);
        for (int x = 0; x < render->width; ++x) {
            int screen_x = x - extra;
            if (hud && (screen_x < 0 || screen_x >= 256)) continue;
            unsigned u = (screen_x + render->ppu->hScroll[2]) & 255;
            unsigned v = (y + render->ppu->vScroll[2] + 1) & 255;
            unsigned index = (v / 8) * 32 + u / 8;
            unsigned clean = KdcReadWord(g_ram, 0xdadc + 2 * index);
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

static void Sprites(Render *render, bool world, bool ground)
{
    static const uint8_t sizes[8][2] = {
        {8,16}, {8,32}, {8,64}, {16,32}, {16,64}, {32,64}, {16,32}, {16,32}
    };
    const uint16_t *vram = PpuRenderVram(render->ppu);
    for (unsigned i = sprite_count; i > 0; --i) {
        const KdcOrbitSprite *sprite = &sprite_buffer[i - 1];
        if (sprite->world != world || (world && sprite->ground != ground)) continue;
        int size = sizes[PPU_objSize(render->ppu)][sprite->large != 0];
        double scale = world ? camera.zoom : 1;
        KdcCameraPoint point = world ? KdcCameraProject(&camera,
            sprite->x, sprite->y, sprite->z, render->width, render->height) :
            (KdcCameraPoint){(render->width - 256) / 2.0, -1, 0};
        double left = point.x + sprite->offset_x * scale;
        double top = point.y + sprite->offset_y * scale;
        unsigned attr = sprite->attributes;
        unsigned base = attr & 0x100 ? PPU_objTileAdr2(render->ppu) : PPU_objTileAdr1(render->ppu);
        unsigned palette = 0x80 + 16 * ((attr >> 9) & 7);
        int x0 = (int)fmax(0, floor(left)), x1 = (int)fmin(render->width, ceil(left + size * scale));
        int y0 = (int)fmax(0, floor(top)), y1 = (int)fmin(render->height, ceil(top + size * scale));
        for (int y = y0; y < y1; ++y) {
            uint32_t *row = (uint32_t *)(render->pixels + y * render->pitch);
            int source_y = (int)floor((y + 0.5 - top) / scale);
            if (source_y < 0 || source_y >= size) continue;
            double depth = sprite->ground ?
                point.depth + 2 * (y + 0.5 - point.y) / scale + 0.125 :
                point.depth - (sprite->offset_y + source_y) * 0.5 + 0.25;
            if (attr & 0x8000) source_y = size - 1 - source_y;
            for (int x = x0; x < x1; ++x) {
                int source_x = (int)floor((x + 0.5 - left) / scale);
                if (source_x < 0 || source_x >= size) continue;
                unsigned index = y * render->width + x;
                if (world && depth < render->depth[index]) continue;
                if (attr & 0x4000) source_x = size - 1 - source_x;
                unsigned tile = ((((attr & 255) >> 4) + (source_y >> 3)) << 4) |
                                (((attr & 15) + (source_x >> 3)) & 15);
                unsigned pixel = TilePixel(vram, tile, base, source_x & 7, source_y & 7, 4);
                if (!pixel) continue;
                row[x] = Color(render->ppu->cgram[palette + pixel]);
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
    camera.target_x = (horizontal + vertical + focus_height) / 2;
    camera.target_y = (horizontal - vertical - focus_height) / 2;
    camera.target_z = focus_height;
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
