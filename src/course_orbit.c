#include "course_orbit.h"
#include "course_camera.h"
#include "course_geometry.h"
#include "course_view.h"
#include "course_sprites.h"
#include "course_hud.h"
#include "course_terrain.h"
#include "hd_textures.h"
#include "host_parallel.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

extern Snes *g_snes;
extern uint8_t g_ram[0x20000];

static KdcCourseCamera camera = {.zoom = 1};
static unsigned map_id = ~0u;
static double focus_height;
static bool focus_ready;
static unsigned focus_player = ~0u;
static bool high_refresh;
static bool orbit_controls = true;

typedef struct Presentation {
    KdcCourseCamera camera;
    KdcOrbitSprite sprites[4096];
    unsigned count, frame, map, player;
    int width, height;
    bool valid;
} Presentation;

static Presentation previous, current;
static int previous_piece[4096], previous_anchor[4096];
static bool continuous;

void KdcOrbitConfigure(bool enabled, bool controls)
{
    if (enabled != high_refresh || controls != orbit_controls) {
        KdcOrbitReset();
        high_refresh = enabled;
        orbit_controls = controls;
    }
}

/* One frame is prepared on the host thread, then drawn as independent
 * horizontal bands on the worker pool. Everything a band reads is fixed
 * before the first one starts; a band writes only its own rows. */

/* The terrain texture level chosen for a draw. */
typedef struct Texture {
    const uint8_t *texels; /* palette indices, 0 transparent */
    int stride, width, height;
    int scale;             /* texels per source pixel */
    int left, top;         /* source pixel of texel (0,0) */
    const uint32_t *colors;
} Texture;

/* A projected terrain triangle. Barycentric weights, depth and texture
 * coordinates are all a*x + b*y + c at the centre of output pixel (x,y). */
typedef struct Face {
    int left, right, top, bottom; /* inclusive pixel bounds */
    double weight[3][3];
    double depth[3], u[3], v[3];
    uint32_t color;               /* flat, or shown where the texture is clear */
    bool textured;
} Face;

/* A sprite piece with everything derived from the camera resolved. */
typedef struct Piece {
    const KdcOrbitSprite *sprite;
    int size, samples;
    int x0, x1, y0, y1;       /* output bounds, exclusive ends */
    double left, top, scale;  /* billboard placement */
    KdcCameraPoint point;
    double native[2];         /* original projected anchor, for OBJ masks */
    bool masked;
    /* Ground decals: output pixel and depth to original projected position. */
    double inverse[4][3], plane[3][2], step;
    const uint8_t *const *art; /* enlarged image per 8x8 cell, or NULL */
    unsigned base;             /* OBJ character base in VRAM words */
    const uint32_t *colors;    /* the piece's 16 palette colours */
} Piece;

typedef struct Render {
    uint8_t *pixels;
    size_t pitch;
    int width, height;
    int scale;
    float *depth;
    Ppu *ppu;
    const uint16_t *vram;
    KdcCourseGeometry geometry;
    KdcCourseCamera camera;
    bool hd;
    const KdcTerrain *terrain;
    Texture texture;
    bool smooth;   /* antialiased texture sampling */
    int samples;   /* per axis, when one pixel spans several texels */
    const Face *faces;
    unsigned face_count;
    const KdcScenery *scenery; /* NULL without the course scenery signature */
    uint32_t backdrop;
    const uint8_t *field;
    const Piece *decals, *bodies, *screen;
    unsigned decal_count, body_count, screen_count;
    uint32_t obj_colors[128];
} Render;

static float depth_buffer[kPpuBufWidth * 240 * kKdcHdScale * kKdcHdScale];
static KdcOrbitSprite sprite_buffer[4096];
static unsigned sprite_count;
static KdcOrbitSprite ordered_sprites[4096];

static bool PlayerPiece(const KdcOrbitSprite *sprite, unsigned player, unsigned bucket)
{
    return sprite->world && !sprite->ground && sprite->bucket == bucket &&
        sprite->view.player == player && (sprite->view_tiles[0] || sprite->view_order) &&
        (sprite->motion_id == 0x100u + player || sprite->motion_id == 0x300u + player);
}

static void OrderAttachments(void)
{
    /* Lower OBJ order wins a depth tie. Reorder only the verified player's
     * pieces within their existing slots and bucket; unrelated sprites keep
     * their positions and the depth test still handles nearer objects. */
    for (unsigned player = 0; player <= 2; player += 2) {
        for (unsigned bucket = 0; bucket < 10; ++bucket) {
            int order = 0;
            for (unsigned i = 0; i < sprite_count; ++i)
                if (PlayerPiece(&sprite_buffer[i], player, bucket) && sprite_buffer[i].view_order)
                    order = sprite_buffer[i].view_order;
            if (!order) continue;
            unsigned count = 0;
            for (unsigned pass = 0; pass < 2; ++pass) {
                bool attachment = (pass == 0) == (order > 0);
                for (unsigned i = 0; i < sprite_count; ++i)
                    if (PlayerPiece(&sprite_buffer[i], player, bucket) &&
                        sprite_buffer[i].view.attachment == attachment)
                        ordered_sprites[count++] = sprite_buffer[i];
            }
            unsigned next = 0;
            for (unsigned i = 0; i < sprite_count; ++i)
                if (PlayerPiece(&sprite_buffer[i], player, bucket))
                    sprite_buffer[i] = ordered_sprites[next++];
        }
    }
}

void KdcOrbitReset(void)
{
    KdcCameraReset(&camera);
    map_id = ~0u;
    focus_height = 0;
    focus_ready = false;
    focus_player = ~0u;
    current.valid = previous.valid = continuous = false;
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
    unsigned next_map = KdcReadWord(g_ram, 0xabb2);
    if (next_map != map_id) {
        KdcOrbitReset();
        map_id = next_map;
    }
    const double seconds = 1.0 / 60.0988;
    bool was_active = camera.active;
    KdcCameraStep(&camera, horizontal, vertical, reset, seconds);
    if (reset) current.valid = previous.valid = continuous = false;
    if (high_refresh) camera.active = true;
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

/* Share the PPU's palette-locked tile source with the composed view. The
 * source is stable during a draw; revalidate content once per draw so VRAM
 * uploads, animation and palette cycling cannot leave stale replacements. */
static uint64_t art_generation;
static const uint8_t *HdArtTileLookup(const Render *render, unsigned address, bool rom,
                                unsigned palette, unsigned bpp)
{
    enum { count = 4096, side = 8 * kKdcHdScale };
    static struct Art {
        unsigned address, palette, bpp;
        uint64_t generation;
        bool rom, valid;
        PpuHdTileQuery query;
        uint8_t pixels[side * side];
    } cache[count];
    unsigned hash = address ^ (palette << 17) ^ (rom ? 0x55555555u : 0) ^ (bpp << 25);
    hash ^= hash >> 16; hash *= 0x7feb352du; hash ^= hash >> 15;
    unsigned bucket = (hash & (count / 4 - 1)) * 4;
    struct Art *entry = NULL, *available = NULL;
    bool same = false;
    for (unsigned i = 0; i < 4; ++i) {
        struct Art *candidate = &cache[bucket + i];
        if (candidate->valid && candidate->address == address && candidate->rom == rom &&
            candidate->palette == palette && candidate->bpp == bpp) {
            entry = candidate; same = true; break;
        }
        if (!available && (!candidate->valid || candidate->generation != art_generation))
            available = candidate;
    }
    if (!entry) entry = available;
    /* Never evict artwork used by this draw: a full bucket must not trigger
     * an expensive rescale on every alternating source pixel. */
    if (!entry) return NULL;
    if (!same || entry->generation != art_generation) {
        PpuHdTileQuery q = {.bpp = bpp, .layer = 4, .scale = kKdcHdScale};
        for (unsigned i = 0; i < bpp * 4; ++i)
            q.words[i] = rom ? g_snes->cart->rom[address + 2 * i] |
                (uint16_t)g_snes->cart->rom[address + 2 * i + 1] << 8 :
                PpuRenderVram(render->ppu)[(address + i) & 0x7fff];
        for (unsigned i = 1; i < (1u << bpp); ++i) q.colors[i] = render->ppu->cgram[palette + i];
        if (!same || memcmp(&q, &entry->query, sizeof(q))) {
            if (!KdcHdSmoothTile(&q, entry->pixels, NULL))
                KdcHdNearestTile(&q, entry->pixels, NULL);
            entry->query = q;
        }
        entry->address = address; entry->palette = palette; entry->bpp = bpp;
        entry->rom = rom; entry->valid = true; entry->generation = art_generation;
    }
    return entry->pixels;
}


/* floor() for coordinates far inside this bias, without a library call. */
static inline int Floor(double value)
{
    return (int)(value + 1048576.0) - 1048576;
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

/* The composed terrain colour at an original projected position, or 0. */
static uint32_t SourceColor(const KdcTerrain *terrain, double u, double v)
{
    if (!terrain) return 0;
    const int x = Floor(u) - terrain->left, y = Floor(v) - terrain->top;
    if (x < 0 || y < 0 || x >= terrain->width || y >= terrain->height) return 0;
    return terrain->colors[terrain->native[(size_t)y * terrain->width + x]];
}

/* The course as world-space triangles with original projected texture
 * coordinates. It depends only on the collision planes, so it is rebuilt
 * when they change, not for every camera position. */
typedef struct MeshVertex { double x, y, z, u, v; } MeshVertex;
enum { kMeshTop, kMeshWallArt, kMeshWallFlat };
typedef struct MeshTriangle {
    MeshVertex vertex[3];
    unsigned cell;
    int kind;
} MeshTriangle;

static struct Mesh {
    bool valid;
    uint8_t shape[0x800], elevation[0x800];
    unsigned width, height;
    int origin_x, origin_y;
    MeshTriangle *triangles;
    unsigned count, capacity;
    /* Texture positions sampled for colours: each cell's centre, and the
     * original cliff artwork whose palette colours newly exposed backs. */
    double (*centers)[2], (*walls)[2];
    unsigned wall_count;
    Face *faces;
} mesh;

static bool MeshPush(MeshVertex a, MeshVertex b, MeshVertex c, unsigned cell, int kind)
{
    /* Equal-height neighbours give walls of no height. */
    const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    if (nx * nx + ny * ny + nz * nz < 1e-18) return true;
    if (mesh.count == mesh.capacity) {
        const unsigned capacity = mesh.capacity ? mesh.capacity * 2 : 1024;
        MeshTriangle *triangles = realloc(mesh.triangles, capacity * sizeof(*triangles));
        if (!triangles) return false;
        mesh.triangles = triangles;
        Face *faces = realloc(mesh.faces, capacity * sizeof(*faces));
        if (!faces) return false;
        mesh.faces = faces;
        mesh.capacity = capacity;
    }
    mesh.triangles[mesh.count++] = (MeshTriangle){{a, b, c}, cell, kind};
    return true;
}

static bool MeshUpdate(const KdcCourseGeometry *geometry)
{
    const size_t cells = (size_t)geometry->width * geometry->height;
    if (mesh.valid && mesh.width == geometry->width && mesh.height == geometry->height &&
        mesh.origin_x == geometry->origin_x && mesh.origin_y == geometry->origin_y &&
        !memcmp(mesh.shape, g_ram + 0x6b90, cells) && !memcmp(mesh.elevation, g_ram + 0x7390, cells))
        return true;
    mesh.valid = false;
    mesh.count = mesh.wall_count = 0;
    double (*centers)[2] = realloc(mesh.centers, cells * sizeof(*centers));
    if (!centers) return false;
    mesh.centers = centers;
    double (*walls)[2] = realloc(mesh.walls, cells * 2 * sizeof(*walls));
    if (!walls) return false;
    mesh.walls = walls;
    /* Outer and void-facing cliffs end at elevation zero, where the
     * original projected cliff artwork ends (measured against native 1-1). */
    const double base_height = 0;
    static const double corners[5][2] = {{0,0},{1,0},{1,1},{0,1},{0.5,0.5}};
    static const int neighbor_x[4] = {0,1,0,-1};
    static const int neighbor_y[4] = {-1,0,1,0};
    /* The original atlas only depicts the two camera-facing cliff sides.
     * Reuse their live palette for newly exposed backs; never bake artwork
     * or invent a per-course material index from the collision flags. */
    for (unsigned y = 0; y < geometry->height; ++y) {
        for (unsigned x = 0; x < geometry->width; ++x) {
            for (unsigned edge = 0; edge < 2; ++edge) {
                double u = edge ? 1 : 0.5, v = edge ? 0.5 : 0, z, neighbor;
                if (!KdcCourseCellHeight(geometry, x, y, u, v, &z)) continue;
                int nx = (int)x + neighbor_x[edge], ny = (int)y + neighbor_y[edge];
                if (nx >= 0 && ny >= 0 && KdcCourseCellHeight(geometry,
                    nx, ny, x + u - nx, y + v - ny, &neighbor)) continue;
                if (z <= base_height) continue;
                KdcCourseGeometryProject(geometry, x + u, y + v, (z + base_height) * 0.5,
                    &mesh.walls[mesh.wall_count][0], &mesh.walls[mesh.wall_count][1]);
                ++mesh.wall_count;
            }
        }
    }
    for (unsigned y = 0; y < geometry->height; ++y) {
        for (unsigned x = 0; x < geometry->width; ++x) {
            const unsigned cell = y * geometry->width + x;
            MeshVertex vertices[5];
            bool valid = true;
            for (unsigned i = 0; i < 5; ++i) {
                MeshVertex *v = &vertices[i];
                v->x = x + corners[i][0]; v->y = y + corners[i][1];
                if (!KdcCourseCellHeight(geometry, x, y,
                    corners[i][0], corners[i][1], &v->z)) { valid = false; break; }
                KdcCourseGeometryProject(geometry, v->x, v->y, v->z, &v->u, &v->v);
            }
            mesh.centers[cell][0] = mesh.centers[cell][1] = 0;
            if (!valid) continue;
            mesh.centers[cell][0] = vertices[4].u;
            mesh.centers[cell][1] = vertices[4].v;
            for (unsigned i = 0; i < 4; ++i) {
                unsigned next = (i + 1) & 3;
                if (!MeshPush(vertices[i], vertices[next], vertices[4], cell, kMeshTop)) return false;
                /* Adjacent cells may meet at different heights. Build the
                 * vertical exposed face rather than stretching the top. */
                MeshVertex lower_a = vertices[i], lower_b = vertices[next];
                int nx = (int)x + neighbor_x[i], ny = (int)y + neighbor_y[i];
                double za = base_height, zb = base_height;
                if (nx >= 0 && ny >= 0) {
                    (void)KdcCourseCellHeight(geometry, nx, ny,
                        lower_a.x - nx, lower_a.y - ny, &za);
                    (void)KdcCourseCellHeight(geometry, nx, ny,
                        lower_b.x - nx, lower_b.y - ny, &zb);
                }
                lower_a.z = fmin(lower_a.z, za); lower_b.z = fmin(lower_b.z, zb);
                KdcCourseGeometryProject(geometry, lower_a.x, lower_a.y, lower_a.z,
                                         &lower_a.u, &lower_a.v);
                KdcCourseGeometryProject(geometry, lower_b.x, lower_b.y, lower_b.z,
                                         &lower_b.u, &lower_b.v);
                const int kind = i < 2 ? kMeshWallArt : kMeshWallFlat;
                if (!MeshPush(vertices[i], lower_a, lower_b, cell, kind) ||
                    !MeshPush(vertices[i], lower_b, vertices[next], cell, kind)) return false;
            }
        }
    }
    mesh.width = geometry->width; mesh.height = geometry->height;
    mesh.origin_x = geometry->origin_x; mesh.origin_y = geometry->origin_y;
    memcpy(mesh.shape, g_ram + 0x6b90, cells);
    memcpy(mesh.elevation, g_ram + 0x7390, cells);
    mesh.valid = true;
    return true;
}

/* Project the mesh for this camera. The projection is affine, so three
 * multiplications replace trigonometry per vertex. */
static bool ProjectFaces(Render *render)
{
    if (!MeshUpdate(&render->geometry)) return false;
    const int width = render->width, height = render->height;
    const KdcCameraPoint o = KdcCameraProject(&render->camera, 0, 0, 0, width, height);
    const KdcCameraPoint px = KdcCameraProject(&render->camera, 1, 0, 0, width, height);
    const KdcCameraPoint py = KdcCameraProject(&render->camera, 0, 1, 0, width, height);
    const KdcCameraPoint pz = KdcCameraProject(&render->camera, 0, 0, 1, width, height);
    const Texture *texture = &render->texture;

    unsigned r = 0, g = 0, b = 0, samples = 0;
    for (unsigned i = 0; i < mesh.wall_count; ++i) {
        const uint32_t color = SourceColor(render->terrain, mesh.walls[i][0], mesh.walls[i][1]);
        if (!color) continue;
        r += (color >> 16) & 255; g += (color >> 8) & 255; b += color & 255; ++samples;
    }
    const uint32_t wall_color = samples ? 0xff000000u | (r / samples << 16) |
        (g / samples << 8) | (b / samples) : 0xff5a5348;

    unsigned count = 0, last_cell = ~0u;
    uint32_t top_color = 0;
    for (unsigned i = 0; i < mesh.count; ++i) {
        const MeshTriangle *triangle = &mesh.triangles[i];
        KdcCameraPoint p[3];
        for (unsigned k = 0; k < 3; ++k) {
            const MeshVertex *v = &triangle->vertex[k];
            p[k].x = o.x + (px.x - o.x) * v->x + (py.x - o.x) * v->y + (pz.x - o.x) * v->z;
            p[k].y = o.y + (px.y - o.y) * v->x + (py.y - o.y) * v->y + (pz.y - o.y) * v->z;
            p[k].depth = o.depth + (px.depth - o.depth) * v->x + (py.depth - o.depth) * v->y +
                         (pz.depth - o.depth) * v->z;
        }
        const double area = Edge(p[0], p[1], p[2].x, p[2].y);
        if (fabs(area) < 0.000001) continue;
        Face *face = &mesh.faces[count];
        face->left = (int)fmax(0, floor(fmin(p[0].x, fmin(p[1].x, p[2].x))));
        face->right = (int)fmin(width - 1, ceil(fmax(p[0].x, fmax(p[1].x, p[2].x))));
        face->top = (int)fmax(0, floor(fmin(p[0].y, fmin(p[1].y, p[2].y))));
        face->bottom = (int)fmin(height - 1, ceil(fmax(p[0].y, fmax(p[1].y, p[2].y))));
        if (face->left > face->right || face->top > face->bottom) continue;
        /* weight[k] is the barycentric weight of vertex k at a pixel centre. */
        for (unsigned k = 0; k < 2; ++k) {
            const KdcCameraPoint a = p[(k + 1) % 3], c = p[(k + 2) % 3];
            const double wx = (c.y - a.y) / area, wy = -(c.x - a.x) / area;
            face->weight[k][0] = wx;
            face->weight[k][1] = wy;
            face->weight[k][2] = (-a.x * (c.y - a.y) + a.y * (c.x - a.x)) / area + 0.5 * (wx + wy);
        }
        for (unsigned k = 0; k < 3; ++k)
            face->weight[2][k] = (k == 2) - face->weight[0][k] - face->weight[1][k];
        double value[3][3];
        for (unsigned k = 0; k < 3; ++k) {
            value[0][k] = p[k].depth;
            value[1][k] = (triangle->vertex[k].u - texture->left) * texture->scale;
            value[2][k] = (triangle->vertex[k].v - texture->top) * texture->scale;
        }
        double *plane[3] = {face->depth, face->u, face->v};
        for (unsigned n = 0; n < 3; ++n)
            for (unsigned k = 0; k < 3; ++k)
                plane[n][k] = value[n][0] * face->weight[0][k] + value[n][1] * face->weight[1][k] +
                              value[n][2] * face->weight[2][k];
        if (triangle->kind == kMeshTop) {
            if (triangle->cell != last_cell) {
                last_cell = triangle->cell;
                top_color = SourceColor(render->terrain, mesh.centers[last_cell][0],
                                        mesh.centers[last_cell][1]);
                if (!top_color) top_color = 0xff739a52;
            }
            face->color = top_color;
        } else {
            face->color = wall_color;
        }
        face->textured = triangle->kind != kMeshWallFlat && texture->texels;
        ++count;
    }
    render->faces = mesh.faces;
    render->face_count = count;
    return true;
}

static inline uint32_t Mix(uint32_t a, uint32_t b, unsigned weight)
{
    const uint32_t rb = ((a & 0xff00ff) * (256 - weight) + (b & 0xff00ff) * weight) >> 8 & 0xff00ff;
    const uint32_t g = ((a & 0x00ff00) * (256 - weight) + (b & 0x00ff00) * weight) >> 8 & 0x00ff00;
    return 0xff000000u | rb | g;
}

static inline uint32_t Texel(const Texture *t, double u, double v, uint32_t fallback)
{
    const int x = Floor(u), y = Floor(v);
    if ((unsigned)x >= (unsigned)t->width || (unsigned)y >= (unsigned)t->height) return fallback;
    const uint32_t color = t->colors[t->texels[(size_t)y * t->stride + x]];
    return color ? color : fallback;
}

/* Antialiased nearest sampling: flat areas keep their exact colour; a texel
 * boundary becomes a one-pixel ramp at any angle or magnification, instead
 * of the uneven steps point sampling leaves on a rotated edge. Aligned 1:1
 * sampling returns the texels unchanged. */
static inline uint32_t SmoothTexel(const Texture *t, double u, double v, double sharp_u,
                                   double sharp_v, uint32_t fallback)
{
    const double a = u - 0.5, b = v - 0.5;
    const int x = Floor(a), y = Floor(b);
    if ((unsigned)x >= (unsigned)(t->width - 1) || (unsigned)y >= (unsigned)(t->height - 1))
        return Texel(t, u, v, fallback);
    const uint8_t *texel = t->texels + (size_t)y * t->stride + x;
    const unsigned t00 = texel[0], t10 = texel[1], t01 = texel[t->stride], t11 = texel[t->stride + 1];
    if (t00 == t10 && t00 == t01 && t00 == t11) return t00 ? t->colors[t00] : fallback;
    double fx = (a - x - 0.5) * sharp_u + 0.5, fy = (b - y - 0.5) * sharp_v + 0.5;
    fx = fx < 0 ? 0 : fx > 1 ? 1 : fx;
    fy = fy < 0 ? 0 : fy > 1 ? 1 : fy;
    const unsigned wx = (unsigned)(fx * 256 + 0.5), wy = (unsigned)(fy * 256 + 0.5);
    const uint32_t c00 = t00 ? t->colors[t00] : fallback, c10 = t10 ? t->colors[t10] : fallback;
    const uint32_t c01 = t01 ? t->colors[t01] : fallback, c11 = t11 ? t->colors[t11] : fallback;
    return Mix(Mix(c00, c10, wx), Mix(c01, c11, wx), wy);
}

/* Rasterize the faces that reach rows [y0,y1). */
static void TerrainRows(const Render *render, int y0, int y1)
{
    const Texture *texture = &render->texture;
    const int samples = render->samples;
    for (unsigned i = 0; i < render->face_count; ++i) {
        const Face *face = &render->faces[i];
        if (face->bottom < y0 || face->top >= y1) continue;
        const int top = face->top > y0 ? face->top : y0;
        const int bottom = face->bottom < y1 - 1 ? face->bottom : y1 - 1;
        /* Output pixels per texel along each texture axis; above one, the
         * ramp between texels narrows to stay a single pixel wide. */
        double sharp_u = 1 / hypot(face->u[0], face->u[1]), sharp_v = 1 / hypot(face->v[0], face->v[1]);
        if (!(sharp_u > 1)) sharp_u = 1;
        if (!(sharp_v > 1)) sharp_v = 1;
        if (!(sharp_u < 64)) sharp_u = 64;
        if (!(sharp_v < 64)) sharp_v = 64;
        for (int y = top; y <= bottom; ++y) {
            /* The span of pixel centres inside all three edges. */
            double low = face->left, high = face->right;
            bool empty = false;
            for (unsigned k = 0; k < 3; ++k) {
                const double slope = face->weight[k][0];
                const double base = face->weight[k][1] * y + face->weight[k][2] + 0.000001;
                if (slope > 0) low = fmax(low, -base / slope);
                else if (slope < 0) high = fmin(high, -base / slope);
                else if (base < 0) empty = true;
            }
            if (empty || !(low <= high + 1)) continue;
            int x0 = (int)ceil(low - 1e-9), x1 = (int)floor(high + 1e-9);
            if (x0 < face->left) x0 = face->left;
            if (x1 > face->right) x1 = face->right;
            if (x0 > x1) continue;
            uint32_t *row = (uint32_t *)(render->pixels + (size_t)y * render->pitch);
            float *depth_row = render->depth + (size_t)y * render->width;
            double depth = face->depth[0] * x0 + face->depth[1] * y + face->depth[2];
            const double depth_step = face->depth[0];
            if (!face->textured) {
                for (int x = x0; x <= x1; ++x, depth += depth_step) {
                    if (depth < depth_row[x]) continue;
                    depth_row[x] = (float)depth;
                    row[x] = face->color;
                }
                continue;
            }
            double u = face->u[0] * x0 + face->u[1] * y + face->u[2];
            double v = face->v[0] * x0 + face->v[1] * y + face->v[2];
            const double u_step = face->u[0], v_step = face->v[0];
            if (samples > 1) {
                /* Zoomed out, one output pixel spans several source texels.
                 * Average a grid across its footprint instead of
                 * point-sampling one, which makes fine checkerboards
                 * shimmer. The texture is affine in screen space. */
                for (int x = x0; x <= x1; ++x, depth += depth_step, u += u_step, v += v_step) {
                    if (depth < depth_row[x]) continue;
                    depth_row[x] = (float)depth;
                    unsigned r = 0, g = 0, blue = 0;
                    for (int j = 0; j < samples; ++j) {
                        for (int k = 0; k < samples; ++k) {
                            const double dx = (k + 0.5) / samples - 0.5, dy = (j + 0.5) / samples - 0.5;
                            const uint32_t sample = Texel(texture, u + dx * face->u[0] + dy * face->u[1],
                                v + dx * face->v[0] + dy * face->v[1], face->color);
                            r += (sample >> 16) & 255; g += (sample >> 8) & 255; blue += sample & 255;
                        }
                    }
                    const unsigned n = (unsigned)(samples * samples);
                    row[x] = 0xff000000u | (r / n) << 16 | (g / n) << 8 | blue / n;
                }
            } else if (render->smooth) {
                for (int x = x0; x <= x1; ++x, depth += depth_step, u += u_step, v += v_step) {
                    if (depth < depth_row[x]) continue;
                    depth_row[x] = (float)depth;
                    row[x] = SmoothTexel(texture, u, v, sharp_u, sharp_v, face->color);
                }
            } else {
                for (int x = x0; x <= x1; ++x, depth += depth_step, u += u_step, v += v_step) {
                    if (depth < depth_row[x]) continue;
                    depth_row[x] = (float)depth;
                    row[x] = Texel(texture, u, v, face->color);
                }
            }
        }
    }
}

static bool SceneryLayout(const Ppu *ppu)
{
    /* The same course scenery signature as the native wide margins. */
    return ppu->bgXsc[2] == 0x3c && PPU_bgTileAdr(ppu, 2) == 0x4000 && !PPU_bigTiles(ppu, 2);
}

/* Backdrop, clean scenery and an empty depth buffer for native rows
 * [row0,row1). The scenery does not depend on the camera. */
static void BackgroundRows(const Render *render, int row0, int row1)
{
    const int scale = render->scale, native_width = render->width / scale;
    const int extra = (native_width - 256) / 2;
    const KdcScenery *scenery = render->scenery;
    const uint16_t scroll = render->ppu->hScroll[2];
    for (int y = row0 * scale; y < row1 * scale; ++y) {
        float *depth = render->depth + (size_t)y * render->width;
        for (int x = 0; x < render->width; ++x) depth[x] = -FLT_MAX;
    }
    for (int row = row0; row < row1; ++row) {
        uint32_t *out = (uint32_t *)(render->pixels + (size_t)row * scale * render->pitch);
        if (!scenery) {
            for (int sy = 0; sy < scale; ++sy) {
                uint32_t *line = (uint32_t *)((uint8_t *)out + (size_t)sy * render->pitch);
                for (int x = 0; x < render->width; ++x) line[x] = render->backdrop;
            }
            continue;
        }
        const unsigned v = (unsigned)(row + render->ppu->vScroll[2] + 1) & 255;
        const bool enlarged = scenery->hd && scale > 1;
        for (int x = 0; x < native_width;) {
            const int screen_x = x - extra;
            const int first = (screen_x + (scroll & 7)) & 7;
            int run = 8 - first;
            if (run > native_width - x) run = native_width - x;
            /* The clean map's opposite edges do not join. As in the native
             * widescreen margins, reflect whole tiles outside the field. */
            bool flip;
            const unsigned column = KdcReflectedColumn(screen_x, scroll, &flip) & 31;
            for (int sy = 0; sy < scale; ++sy) {
                uint32_t *line = (uint32_t *)((uint8_t *)out + (size_t)sy * render->pitch) + x * scale;
                if (enlarged) {
                    const uint8_t *source = scenery->hd +
                        (size_t)(v * 4 + (unsigned)((2 * sy + 1) * 4 / (2 * scale))) * 1024 + column * 32;
                    if (scale == kKdcTerrainScale) {
                        /* At the texture's own scale each output pixel is
                         * the next texel. Avoid two runtime divisions per
                         * pixel over the entire HD backdrop. */
                        const int start = flip ? 31 - first * scale : first * scale;
                        const int step = flip ? -1 : 1;
                        for (int i = 0; i < run * scale; ++i) {
                            const unsigned index = source[start + i * step];
                            line[i] = index ? scenery->colors[index] : render->backdrop;
                        }
                        continue;
                    }
                    for (int i = 0; i < run * scale; ++i) {
                        const int tx = (first + i / scale) * 4 + (2 * (i % scale) + 1) * 4 / (2 * scale);
                        const unsigned index = source[flip ? 31 - tx : tx];
                        line[i] = index ? scenery->colors[index] : render->backdrop;
                    }
                } else {
                    const uint8_t *source = scenery->native + (size_t)v * 256 + column * 8;
                    for (int i = 0; i < run; ++i) {
                        const unsigned index = source[flip ? 7 - (first + i) : first + i];
                        const uint32_t color = index ? scenery->colors[index] : render->backdrop;
                        for (int sx = 0; sx < scale; ++sx) line[i * scale + sx] = color;
                    }
                }
            }
            x += run;
        }
    }
}

/* The HUD panel the game composites into BG3 over the clean scenery, for
 * native rows [row0,row1). It stays at native resolution. */
static void HudRows(const Render *render, int row0, int row1)
{
    if (!render->scenery || KdcHudActive(render->ppu)) return;
    const int scale = render->scale, native_width = render->width / scale;
    const int extra = (native_width - 256) / 2;
    const uint16_t scroll = render->ppu->hScroll[2];
    for (int row = row0; row < row1; ++row) {
        const unsigned v = (unsigned)(row + render->ppu->vScroll[2] + 1) & 255;
        for (int screen_x = 0; screen_x < 256; ++screen_x) {
            const unsigned u = (unsigned)(screen_x + scroll) & 255;
            const unsigned index = (v / 8) * 32 + u / 8;
            const unsigned tile = render->vram[0x3c00 + index];
            if (tile == KdcReadWord(g_ram, 0xdadc + 2 * index)) {
                screen_x += 7 - (int)(u & 7); /* the rest of this clean tile */
                continue;
            }
            const unsigned pixel = TilePixel(render->vram, tile, 0x4000, u & 7, v & 7, 2);
            if (!pixel) continue;
            const uint32_t color = Color(render->ppu->cgram[((tile >> 10) & 7) * 4 + pixel]);
            for (int sy = 0; sy < scale; ++sy) {
                uint32_t *line = (uint32_t *)(render->pixels + (size_t)(row * scale + sy) * render->pitch) +
                                 (screen_x + extra) * scale;
                for (int sx = 0; sx < scale; ++sx) line[sx] = color;
            }
        }
    }
}

static void CopyField(const Render *render, int row, int x0, int x1)
{
    const int scale = render->scale, native_width = render->width / scale;
    const uint32_t *field = (const uint32_t *)(const void *)render->field + (size_t)row * native_width;
    for (int sy = 0; sy < scale; ++sy) {
        uint32_t *line = (uint32_t *)(render->pixels + (size_t)(row * scale + sy) * render->pitch);
        for (int x = x0; x <= x1; ++x)
            for (int sx = 0; sx < scale; ++sx) line[x * scale + sx] = field[x];
    }
}

/* HUD pixels keep the native PPU's result, with its colour math and
 * portrait composition, for native rows [row0,row1). */
static void RestoreRows(const Render *render, int row0, int row1)
{
    if (!render->field || render->ppu->bgXsc[2] != 0x3c ||
        PPU_bgTileAdr(render->ppu, 2) != 0x4000) return;
    const int native_width = render->width / render->scale;
    const int extra = (native_width - 256) / 2;
    if (KdcHudActive(render->ppu)) {
        for (int row = row0; row < row1; ++row)
            for (int x = 0; x < native_width; ++x)
                if (KdcHudOverlayPixel(render->ppu, x - extra, row + 1)) CopyField(render, row, x, x);
        return;
    }
    /* Preserve the complete occupied horizontal span on each HUD row,
     * including transparent interiors that use the native PPU's color
     * math and object portraits. */
    for (int row = row0; row < row1; ++row) {
        const unsigned v = (unsigned)(row + render->ppu->vScroll[2] + 1) & 255;
        int left, right;
        if (!KdcHudSpan(render->vram + 0x3c00, g_ram, v, (uint16_t)render->ppu->hScroll[2],
                        &left, &right)) continue;
        left += extra;
        right += extra;
        if (left < 0) left = 0;
        if (right >= native_width) right = native_width - 1;
        CopyField(render, row, left, right);
    }
}

static void CollectSprite(const KdcOrbitSprite *sprite, void *context)
{
    (void)context;
    if (sprite_count < sizeof(sprite_buffer) / sizeof(sprite_buffer[0]))
        sprite_buffer[sprite_count++] = *sprite;
}

static int SpriteSize(const Ppu *ppu, const KdcOrbitSprite *sprite)
{
    static const uint8_t sizes[8][2] = {
        {8,16}, {8,32}, {8,64}, {16,32}, {16,64}, {32,64}, {16,32}, {16,32}
    };
    return sizes[PPU_objSize(ppu)][sprite->large != 0];
}

/* Mode 1 OBJ priority against the course planes: priority 0/1 objects lie
 * behind every opaque BG1/BG2 pixel, priority 2 behind high-priority tiles
 * and priority 3 in front. The game uses this to mask artwork, such as the
 * cup's interior showing only through the hole in the floor tiles. */
static bool NativeMasked(const Render *render, unsigned priority, double x, double y)
{
    const KdcTerrain *terrain = render->terrain;
    if (priority >= 3 || x < 0 || y < 0 || !terrain) return false;
    const int px = (int)x - terrain->left, py = (int)y - terrain->top;
    if (px < 0 || py < 0 || px >= terrain->width || py >= terrain->height) return false;
    return terrain->mask[(size_t)py * terrain->width + px] &
           (priority < 2 ? kKdcTerrainOpaque : kKdcTerrainPriority);
}

/* Original OBJ color at an unflipped piece position; 0 when transparent or
 * masked. */
static uint32_t PieceColor(const Render *render, const Piece *piece,
                           double source_x, double source_y)
{
    const KdcOrbitSprite *sprite = piece->sprite;
    const int size = piece->size;
    if (!(source_x >= 0 && source_x < size && source_y >= 0 && source_y < size)) return 0;
    int sx = (int)source_x, sy = (int)source_y;
    unsigned attr = sprite->attributes;
    if (piece->masked && NativeMasked(render, (attr >> 12) & 3,
            piece->native[0] + sprite->offset_x + sx, piece->native[1] + sprite->offset_y + sy))
        return 0;
    if (attr & 0x4000) sx = size - 1 - sx;
    if (attr & 0x8000) sy = size - 1 - sy;
    const uint8_t *art = piece->art ? piece->art[(sy >> 3) * (size / 8) + (sx >> 3)] : NULL;
    unsigned pixel;
    if (art) {
        /* The enlarged character, at the continuous flipped position. */
        const double tx = attr & 0x4000 ? nextafter(size - source_x, 0) : source_x;
        const double ty = attr & 0x8000 ? nextafter(size - source_y, 0) : source_y;
        pixel = art[((unsigned)(ty * kKdcHdScale) & (8 * kKdcHdScale - 1)) * (8 * kKdcHdScale) +
                    ((unsigned)(tx * kKdcHdScale) & (8 * kKdcHdScale - 1))];
    } else {
        unsigned view_tile = size <= 16 ? sprite->view_tiles[(sy >> 3) * (size / 8) + (sx >> 3)] : 0;
        if (view_tile && view_tile + 32 <= g_snes->cart->romImageSize) {
            const uint8_t *data = g_snes->cart->rom + view_tile + (sy & 7) * 2;
            unsigned shift = 7 - (sx & 7);
            pixel = ((data[0] >> shift) & 1) | (((data[1] >> shift) & 1) << 1) |
                    (((data[16] >> shift) & 1) << 2) | (((data[17] >> shift) & 1) << 3);
        } else {
            unsigned tile = ((((attr & 255) >> 4) + (sy >> 3)) << 4) |
                            (((attr & 15) + (sx >> 3)) & 15);
            pixel = TilePixel(render->vram, tile, piece->base, sx & 7, sy & 7, 4);
        }
    }
    return pixel ? piece->colors[pixel] : 0;
}

/* Zoomed out, average the source texels under a pixel; it is drawn when at
 * least half of them are opaque. Otherwise this is a point sample. */
static uint32_t Filtered(const Render *render, const Piece *piece,
                         double x, double y, double step)
{
    const int samples = piece->samples;
    if (samples == 1) return PieceColor(render, piece, x, y);
    unsigned r = 0, g = 0, b = 0, opaque = 0;
    for (int j = 0; j < samples; ++j) {
        for (int i = 0; i < samples; ++i) {
            uint32_t c = PieceColor(render, piece,
                x + (i + 0.5 - samples * 0.5) * step, y + (j + 0.5 - samples * 0.5) * step);
            if (!c) continue;
            r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; ++opaque;
        }
    }
    if (opaque * 2 < (unsigned)(samples * samples)) return 0;
    return 0xff000000u | (r / opaque) << 16 | (g / opaque) << 8 | b / opaque;
}

/* Ground artwork (tee, shadows, guide ground marks) was drawn in the
 * original projection. Paint it onto the visible terrain exactly as the
 * native camera would have projected it there, like the terrain artwork
 * itself: it turns with the course and follows slopes instead of standing
 * as a billboard that terrain can cut through. Terrain depth is retained,
 * so later bodies stand in front, and nearer occluders hide the decal. */
static void DecalRows(const Render *render, const Piece *piece, int band0, int band1)
{
    const KdcOrbitSprite *sprite = piece->sprite;
    const double tolerance = 1.5; /* elevation steps from the anchor plane */
    const int y0 = piece->y0 > band0 ? piece->y0 : band0;
    const int y1 = piece->y1 < band1 ? piece->y1 : band1;
    for (int y = y0; y < y1; ++y) {
        uint32_t *row = (uint32_t *)(render->pixels + (size_t)y * render->pitch);
        const float *depth_row = render->depth + (size_t)y * render->width;
        for (int x = piece->x0; x < piece->x1; ++x) {
            const float depth = depth_row[x];
            const double px = x + 0.5, py = y + 0.5;
            double sx, sy;
            if (depth == -FLT_MAX) {
                /* Over the void (a ball leaving the course) keep the plane. */
                sx = piece->plane[0][0] + piece->plane[1][0] * px + piece->plane[2][0] * py;
                sy = piece->plane[0][1] + piece->plane[1][1] * px + piece->plane[2][1] * py;
            } else {
                const double (*m)[3] = piece->inverse;
                const double z = m[0][2] + m[1][2] * px + m[2][2] * py + m[3][2] * depth;
                if (fabs(z - sprite->z) > tolerance) continue;
                sx = m[0][0] + m[1][0] * px + m[2][0] * py + m[3][0] * depth;
                sy = m[0][1] + m[1][1] * px + m[2][1] * py + m[3][1] * depth;
            }
            const uint32_t color = Filtered(render, piece, sx - piece->native[0] - sprite->offset_x,
                                            sy - piece->native[1] - sprite->offset_y, piece->step);
            if (color) row[x] = color;
        }
    }
}

/* An upright billboard (world), or a screen-anchored piece. */
static void BodyRows(const Render *render, const Piece *piece, bool world, int band0, int band1)
{
    const double scale = piece->scale;
    const int y0 = piece->y0 > band0 ? piece->y0 : band0;
    const int y1 = piece->y1 < band1 ? piece->y1 : band1;
    for (int y = y0; y < y1; ++y) {
        uint32_t *row = (uint32_t *)(render->pixels + (size_t)y * render->pitch);
        float *depth_row = render->depth + (size_t)y * render->width;
        /* Upright billboard above its anchor. Rows the original image
         * draws below the anchor (hole, shadows, feet) lie in front of
         * it on the ground plane instead of being buried beneath it. */
        const double below = (y + 0.5 - piece->point.y) / scale;
        const double depth = piece->point.depth + (below >= 0 ? 2 * below : -0.5 * below) + 0.25;
        for (int x = piece->x0; x < piece->x1; ++x) {
            const uint32_t color = Filtered(render, piece, (x + 0.5 - piece->left) / scale,
                (y + 0.5 - piece->top) / scale, 1 / scale / piece->samples);
            if (!color) continue;
            /* Compare at buffer precision: equal depths keep OBJ order. */
            if (world && (float)depth < depth_row[x]) continue;
            row[x] = color;
            if (world) depth_row[x] = (float)depth;
        }
    }
}

static Piece piece_buffer[sizeof(sprite_buffer) / sizeof(sprite_buffer[0])];
static const uint8_t *art_cells[16384];

/* Resolve the camera-dependent placement and artwork of every piece once,
 * in drawing order: decals, upright bodies, then screen pieces. */
static void PreparePieces(Render *render)
{
    unsigned count = 0, cells_used = 0;
    const bool enlarge = render->hd && render->scale > 1;
    for (unsigned i = 0; i < 128; ++i) render->obj_colors[i] = Color(render->ppu->cgram[128 + i]);
    for (unsigned pass = 0; pass < 3; ++pass) {
        const Piece *first = &piece_buffer[count];
        for (unsigned i = sprite_count; i > 0; --i) {
            const KdcOrbitSprite *sprite = &sprite_buffer[i - 1];
            const unsigned kind = !sprite->world ? 2 : sprite->ground ? 0 : 1;
            if (kind != pass) continue;
            Piece *piece = &piece_buffer[count];
            *piece = (Piece){.sprite = sprite, .size = SpriteSize(render->ppu, sprite), .samples = 1, .step = 1};
            const int size = piece->size;
            const unsigned attr = sprite->attributes;
            piece->base = attr & 0x100 ? PPU_objTileAdr2(render->ppu) : PPU_objTileAdr1(render->ppu);
            piece->colors = render->obj_colors + 16 * ((attr >> 9) & 7);
            if (sprite->world) {
                KdcCourseGeometryProject(&render->geometry, sprite->x, sprite->y, sprite->z,
                                         &piece->native[0], &piece->native[1]);
                piece->masked = true;
            }
            if (kind == 0) {
                /* Screen bounds of the piece on the anchor plane, widened for slopes. */
                double left = DBL_MAX, right = -DBL_MAX, top = DBL_MAX, bottom = -DBL_MAX;
                for (unsigned corner = 0; corner < 4; ++corner) {
                    double dx = sprite->offset_x + (corner & 1 ? size : 0);
                    double dy = sprite->offset_y + (corner & 2 ? size : 0);
                    KdcCameraPoint p = KdcCameraProject(&render->camera,
                        sprite->x + (dx / 32 + dy / 16) / 2, sprite->y + (dx / 32 - dy / 16) / 2,
                        sprite->z, render->width, render->height);
                    left = fmin(left, p.x); right = fmax(right, p.x);
                    top = fmin(top, p.y); bottom = fmax(bottom, p.y);
                }
                const double tolerance = 1.5;
                double margin = 16 * tolerance * render->camera.zoom + 1;
                piece->x0 = (int)fmax(0, floor(left - 1));
                piece->x1 = (int)fmin(render->width, ceil(right + 1));
                piece->y0 = (int)fmax(0, floor(top - margin));
                piece->y1 = (int)fmin(render->height, ceil(bottom + margin));
                /* Source pixels spanned by one output pixel on a horizontal plane. Both
                 * projections are affine, so this is the same at every pixel and height. */
                piece->samples = Samples(render->camera.zoom);
                bool valid = true;
                if (piece->samples > 1) {
                    double p[3][2], unused;
                    for (unsigned k = 0; k < 3 && valid; ++k) {
                        double wx, wy;
                        valid = KdcCameraUnprojectPlane(&render->camera, 0.5 + (k == 1), 0.5 + (k == 2),
                                sprite->z, render->width, render->height, &wx, &wy, &unused);
                        if (valid) KdcCourseGeometryProject(&render->geometry, wx, wy, sprite->z,
                                                            &p[k][0], &p[k][1]);
                    }
                    if (valid) piece->step = fmax(hypot(p[1][0] - p[0][0], p[1][1] - p[0][1]),
                                                  hypot(p[2][0] - p[0][0], p[2][1] - p[0][1])) / piece->samples;
                }
                /* Inverse orthographic projection is affine in screen X/Y and depth.
                 * Precompute it once instead of repeating trigonometry at every pixel. */
                for (unsigned k = 0; k < 4 && valid; ++k) {
                    double wx, wy, wz;
                    valid = KdcCameraUnproject(&render->camera, k == 1, k == 2, k == 3,
                            render->width, render->height, &wx, &wy, &wz);
                    if (!valid) break;
                    KdcCourseGeometryProject(&render->geometry, wx, wy, wz,
                                             &piece->inverse[k][0], &piece->inverse[k][1]);
                    piece->inverse[k][2] = wz;
                }
                for (unsigned k = 0; k < 3 && valid; ++k) {
                    double wx, wy, unused;
                    valid = KdcCameraUnprojectPlane(&render->camera, k == 1, k == 2, sprite->z,
                            render->width, render->height, &wx, &wy, &unused);
                    if (valid) KdcCourseGeometryProject(&render->geometry, wx, wy, sprite->z,
                                                        &piece->plane[k][0], &piece->plane[k][1]);
                }
                if (!valid) continue;
                for (unsigned k = 1; k < 4; ++k)
                    for (unsigned j = 0; j < 3; ++j) piece->inverse[k][j] -= piece->inverse[0][j];
                for (unsigned k = 1; k < 3; ++k)
                    for (unsigned j = 0; j < 2; ++j) piece->plane[k][j] -= piece->plane[0][j];
            } else {
                const bool world = sprite->world;
                double scale = world ? render->camera.zoom : render->scale;
                double z = sprite->z, surface;
                /* Guest heights are fixed point; the mesh is continuous. Treat an
                 * anchor within a quarter step below the surface as resting on it. */
                if (world && KdcCourseGeometrySample(&render->geometry, sprite->x, sprite->y, &surface) &&
                    surface > z && surface - z <= 0.25) z = surface;
                piece->point = world ? KdcCameraProject(&render->camera,
                    sprite->x, sprite->y, z, render->width, render->height) :
                    (KdcCameraPoint){((int)(render->width / render->scale) - 256) / 2.0 * render->scale,
                                     0, 0};
                double left = piece->point.x + sprite->offset_x * scale;
                if (!world) left += (double)render->scale * KdcHudPortraitShift(render->ppu,
                    sprite->offset_x, sprite->offset_y, sprite->attributes);
                double top = piece->point.y + sprite->offset_y * scale;
                piece->samples = world ? Samples(scale) : 1;
                piece->left = left; piece->top = top; piece->scale = scale;
                piece->x0 = (int)fmax(0, floor(left));
                piece->x1 = (int)fmin(render->width, ceil(left + size * scale));
                piece->y0 = (int)fmax(0, floor(top));
                piece->y1 = (int)fmin(render->height, ceil(top + size * scale));
            }
            if (piece->x0 >= piece->x1 || piece->y0 >= piece->y1) continue;
            const unsigned cells = (unsigned)((size / 8) * (size / 8));
            if (enlarge && sprite->world && cells_used + cells <= sizeof(art_cells) / sizeof(art_cells[0])) {
                /* Cells are indexed in the character's own (flipped) space. */
                for (unsigned cell = 0; cell < cells; ++cell) {
                    const unsigned cx = cell % (unsigned)(size / 8), cy = cell / (unsigned)(size / 8);
                    const unsigned tile = ((((attr & 255) >> 4) + cy) << 4) | (((attr & 15) + cx) & 15);
                    const unsigned view_tile = size <= 16 ? sprite->view_tiles[cell] : 0;
                    const bool from_rom = view_tile && view_tile + 32 <= g_snes->cart->romImageSize;
                    art_cells[cells_used + cell] = HdArtTileLookup(render,
                        from_rom ? view_tile : (piece->base + tile * 16) & 0x7fff, from_rom,
                        0x80 + 16 * ((attr >> 9) & 7), 4);
                }
                piece->art = &art_cells[cells_used];
                cells_used += cells;
            }
            ++count;
        }
        const unsigned added = (unsigned)(&piece_buffer[count] - first);
        if (pass == 0) { render->decals = first; render->decal_count = added; }
        else if (pass == 1) { render->bodies = first; render->body_count = added; }
        else { render->screen = first; render->screen_count = added; }
    }
}

static KdcCourseCamera ViewCamera(Render *render)
{
    int width = render->width, height = render->height;
    KdcCourseCamera view = camera;
    double cx = KdcCourseScroll(render->ppu->hScroll[0], KdcReadWord(g_ram, 0x86)) + 128;
    /* Visible SNES scanlines begin at one, while the output rows begin at
     * zero. Preserve that phase for terrain and billboard origins. */
    double cy = KdcCourseScroll(render->ppu->vScroll[0], KdcReadWord(g_ram, 0x88)) + 113;
    double horizontal = (cx - render->geometry.origin_x) / 32;
    double vertical = (cy - render->geometry.origin_y) / 16;
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
    double lead = fmax(0, cos(view.yaw)) * fmin(1, view.zoom);
    view.target_x = ball_x;
    view.target_y = ball_y;
    view.target_z = focus_height;
    view.lead_x = (center_x - ball_x) * lead;
    view.lead_y = (center_y - ball_y) * lead;
    /* The ground pivot keeps bounces from shaking the course, but zoomed
     * in a high ball would leave the frame. Shift the pivot height only as
     * far as needed to keep the ball and a 16-pixel sprite above it on
     * screen; raising it one step moves the view 16 pixels per zoom down. */
    KdcCameraPoint ball = KdcCameraProject(&view, ball_x, ball_y,
        BallHeight(focus_player), width, height);
    double top = 8 + 16 * view.zoom, bottom = height - 8.0;
    if (ball.y < top)
        view.target_z += (top - ball.y) / (16 * view.zoom);
    else if (ball.y > bottom)
        view.target_z -= (ball.y - bottom) / (16 * view.zoom);
    return view;
}

static bool Ready(void)
{
    return camera.active && focus_ready && g_snes && g_snes->ppu && g_snes->cart &&
        KdcIsCourse(g_snes->ppu) && !PPU_forcedBlank(g_snes->ppu) &&
        PPU_brightness(g_snes->ppu) == 15 && PPU_mosaicSize(g_snes->ppu) <= 1;
}

/* Whether the latest presentation used KdcOrbitDraw's composition. */
static bool composed;

bool KdcOrbitComposing(void)
{
    return composed && Ready();
}

static bool Nearby(const KdcOrbitSprite *a, const KdcOrbitSprite *b)
{
    return fabs(a->x - b->x) <= 4 && fabs(a->y - b->y) <= 4 && fabs(a->z - b->z) <= 32;
}

static void ReportPresentation(void)
{
    static bool checked;
    static FILE *log;
    if (!checked) {
        checked = true;
        const char *path = getenv("KDC_PRESENTATION_LOG");
        if (path && *path) {
            log = fopen(path, "w");
            if (log) fputs("frame,continuous,pieces,world,identified,matched\n", log);
        }
    }
    if (!log) return;
    unsigned world = 0, identified = 0, matched = 0;
    for (unsigned i = 0; i < current.count; ++i) {
        world += current.sprites[i].world;
        identified += current.sprites[i].world && current.sprites[i].motion_id != 0;
        matched += continuous && previous_anchor[i] >= 0;
    }
    fprintf(log, "%u,%u,%u,%u,%u,%u\n", current.frame, continuous,
            current.count, world, identified, matched);
}

void KdcOrbitCapture(unsigned frame, int width, int height)
{
    if (!high_refresh) return;
    previous = current;
    current.valid = continuous = false;
    if (!Ready() || width < 1 || width > kPpuBufWidth || height < 1 || height > 240) return;
    if (KdcReadWord(g_ram, 0xabb2) != map_id ||
        KdcReadWord(g_ram, 0x1803) != focus_player) return;
    Render render = {.width = width, .height = height, .ppu = g_snes->ppu};
    if (!KdcCourseGeometryInit(&render.geometry, g_ram, sizeof(g_ram),
            g_snes->cart->rom, g_snes->cart->romImageSize)) return;
    current.camera = ViewCamera(&render);
    current.frame = frame;
    current.map = KdcReadWord(g_ram, 0xabb2);
    current.player = KdcReadWord(g_ram, 0x1803);
    current.width = width; current.height = height;
    sprite_count = 0;
    KdcVisitOrbitSprites(render.ppu, g_ram, CollectSprite, NULL);
    current.count = sprite_count;
    memcpy(current.sprites, sprite_buffer, sprite_count * sizeof(*sprite_buffer));
    current.valid = true;
    continuous = previous.valid && frame == previous.frame + 1 &&
        current.map == previous.map && current.player == previous.player &&
        width == previous.width && height == previous.height &&
        fabs(current.camera.target_x - previous.camera.target_x) <= 4 &&
        fabs(current.camera.target_y - previous.camera.target_y) <= 4 &&
        fabs(current.camera.target_z - previous.camera.target_z) <= 32 &&
        fabs(current.camera.lead_x - previous.camera.lead_x) <= 2 &&
        fabs(current.camera.lead_y - previous.camera.lead_y) <= 2 &&
        fabs(remainder(current.camera.yaw - previous.camera.yaw, 6.283185307179586)) < 0.2 &&
        fabs(current.camera.zoom - previous.camera.zoom) < 0.5;
    if (!continuous) { ReportPresentation(); return; }
    /* Resolve correspondence once per simulation boundary, never per draw.
     * Ambiguous duplicate pieces retain current offsets. Multiple pieces of
     * one actor may still share a verified, identical world anchor. */
    for (unsigned i = 0; i < current.count; ++i) {
        const KdcOrbitSprite *s = &current.sprites[i];
        int anchor = -1, piece = -1;
        bool ambiguous_anchor = false, ambiguous_piece = false;
        if (s->motion_id && s->world && !s->unresolved) {
            for (unsigned j = 0; j < previous.count; ++j) {
                const KdcOrbitSprite *p = &previous.sprites[j];
                if (p->motion_id != s->motion_id || !p->world || p->unresolved ||
                    p->ground != s->ground) continue;
                if (anchor >= 0) {
                    const KdcOrbitSprite *a = &previous.sprites[anchor];
                    if (a->x != p->x || a->y != p->y || a->z != p->z ||
                        a->motion_x != p->motion_x || a->motion_y != p->motion_y) ambiguous_anchor = true;
                }
                anchor = (int)j;
                if (p->piece_id != s->piece_id || p->attributes != s->attributes ||
                    p->bucket != s->bucket || p->large != s->large) continue;
                if (piece >= 0) ambiguous_piece = true;
                piece = (int)j;
            }
        }
        previous_anchor[i] = !ambiguous_anchor && anchor >= 0 && Nearby(s, &previous.sprites[anchor]) &&
            fabs(s->motion_x - previous.sprites[anchor].motion_x) <= 64 &&
            fabs(s->motion_y - previous.sprites[anchor].motion_y) <= 64 ? anchor : -1;
        previous_piece[i] = !ambiguous_piece && piece >= 0 &&
            fabs(s->offset_x - previous.sprites[piece].offset_x) <= 64 &&
            fabs(s->offset_y - previous.sprites[piece].offset_y) <= 64 ? piece : -1;
    }
    ReportPresentation();
}

static double Blend(double a, double b, double alpha) { return a + (b - a) * alpha; }

static void Interpolate(Render *render, double alpha)
{
    render->camera = current.camera;
    sprite_count = current.count;
    memcpy(sprite_buffer, current.sprites, sprite_count * sizeof(*sprite_buffer));
    if (!continuous || !isfinite(alpha) || alpha >= 1) return;
    alpha = fmax(0, alpha);
    const KdcCourseCamera *a = &previous.camera, *b = &current.camera;
    render->camera.yaw = a->yaw + remainder(b->yaw - a->yaw, 6.283185307179586) * alpha;
    render->camera.zoom = Blend(a->zoom, b->zoom, alpha);
    render->camera.target_x = Blend(a->target_x, b->target_x, alpha);
    render->camera.target_y = Blend(a->target_y, b->target_y, alpha);
    render->camera.target_z = Blend(a->target_z, b->target_z, alpha);
    render->camera.lead_x = Blend(a->lead_x, b->lead_x, alpha);
    render->camera.lead_y = Blend(a->lead_y, b->lead_y, alpha);
    for (unsigned i = 0; i < sprite_count; ++i) {
        KdcOrbitSprite *s = &sprite_buffer[i];
        if (previous_anchor[i] < 0) continue;
        const KdcOrbitSprite *p = &previous.sprites[previous_anchor[i]];
        s->x = Blend(p->x, s->x, alpha); s->y = Blend(p->y, s->y, alpha);
        s->z = Blend(p->z, s->z, alpha);
        if (previous_piece[i] < 0) {
            /* Actor motion remains smooth when its animation changes to a
             * different layout; the new artwork's local offsets stay exact. */
            s->offset_x += Blend(p->motion_x, s->motion_x, alpha) - s->motion_x;
            s->offset_y += Blend(p->motion_y, s->motion_y, alpha) - s->motion_y;
            continue;
        }
        p = &previous.sprites[previous_piece[i]];
        s->offset_x = Blend(p->offset_x, s->offset_x, alpha);
        s->offset_y = Blend(p->offset_y, s->offset_y, alpha);
    }
}

static void ReportDraw(double alpha)
{
    static bool checked;
    static FILE *log;
    if (!checked) {
        checked = true;
        const char *path = getenv("KDC_PRESENTATION_DRAW_LOG");
        if (path && *path) {
            log = fopen(path, "w");
            if (log) fputs("frame,alpha,interpolated_objects\n", log);
        }
    }
    if (!log) return;
    unsigned moved = 0;
    for (unsigned i = 0; i < sprite_count; ++i) {
        const KdcOrbitSprite *a = &current.sprites[i], *b = &sprite_buffer[i];
        moved += a->x != b->x || a->y != b->y || a->z != b->z ||
                 a->offset_x != b->offset_x || a->offset_y != b->offset_y;
    }
    fprintf(log, "%u,%.6f,%u\n", current.frame, alpha, moved);
}


enum { kBandRows = 8 }; /* native rows per band: one BG3 character row */

static void DrawBand(void *context, unsigned index)
{
    const Render *render = context;
    const int native_height = render->height / render->scale;
    const int row0 = (int)index * kBandRows;
    const int row1 = row0 + kBandRows < native_height ? row0 + kBandRows : native_height;
    const int y0 = row0 * render->scale, y1 = row1 * render->scale;
    BackgroundRows(render, row0, row1);
    TerrainRows(render, y0, y1);
    for (unsigned i = 0; i < render->decal_count; ++i) DecalRows(render, &render->decals[i], y0, y1);
    for (unsigned i = 0; i < render->body_count; ++i) BodyRows(render, &render->bodies[i], true, y0, y1);
    HudRows(render, row0, row1);
    for (unsigned i = 0; i < render->screen_count; ++i) BodyRows(render, &render->screen[i], false, y0, y1);
    RestoreRows(render, row0, row1);
}

static double Milliseconds(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return now.tv_sec * 1000.0 + now.tv_nsec / 1000000.0;
}

/* Developer diagnostic: KDC_ORBIT_PROFILE=1 prints mean preparation and
 * drawing time, and the terrain work done, every 300 composed frames, and
 * the same for any single present slower than kSlowPresent. */
enum { kSlowPresent = 8 };
static void ReportProfile(double prepared, double drawn)
{
    static int enabled = -1;
    static double prepare_total, draw_total;
    static unsigned frames;
    static KdcTerrainStats before, previous;
    if (enabled < 0) {
        const char *value = getenv("KDC_ORBIT_PROFILE");
        enabled = value && *value && *value != '0';
    }
    if (!enabled) return;
    KdcTerrainStats now;
    KdcTerrainGetStats(&now);
    if (prepared + drawn > kSlowPresent)
        fprintf(stderr, "[orbit] slow present: prepare_ms=%.3f draw_ms=%.3f terrain_rebuilds=%llu "
                "composed_tiles=%llu scaled_tiles=%llu scenery_builds=%llu scenery_enlargements=%llu "
                "scenery_cache_hits=%llu\n", prepared, drawn,
                (unsigned long long)(now.rebuilds - previous.rebuilds),
                (unsigned long long)(now.composed_tiles - previous.composed_tiles),
                (unsigned long long)(now.scaled_tiles - previous.scaled_tiles),
                (unsigned long long)(now.scenery_builds - previous.scenery_builds),
                (unsigned long long)(now.scenery_enlargements - previous.scenery_enlargements),
                (unsigned long long)(now.scenery_cache_hits - previous.scenery_cache_hits));
    previous = now;
    prepare_total += prepared;
    draw_total += drawn;
    if (++frames < 300) return;
    fprintf(stderr, "[orbit] frames=%u prepare_ms=%.3f draw_ms=%.3f threads=%u faces=%u "
            "terrain_rebuilds=%llu composed_tiles=%llu scaled_tiles=%llu scenery_enlargements=%llu "
            "scenery_cache_hits=%llu\n", frames,
            prepare_total / frames, draw_total / frames, KdcParallelThreads(), mesh.count,
            (unsigned long long)(now.rebuilds - before.rebuilds),
            (unsigned long long)(now.composed_tiles - before.composed_tiles),
            (unsigned long long)(now.scaled_tiles - before.scaled_tiles),
            (unsigned long long)(now.scenery_enlargements - before.scenery_enlargements),
            (unsigned long long)(now.scenery_cache_hits - before.scenery_cache_hits));
    before = now;
    prepare_total = draw_total = 0;
    frames = 0;
}

static int Draw(uint8_t *dst, size_t pitch, const uint8_t *field,
                int width, int height, double alpha, unsigned scale)
{
    if (!scale || scale > kKdcHdScale || !camera.active || !focus_ready || !dst || width < 1 || width > kPpuBufWidth ||
        height < 1 || height > 240 || pitch < (size_t)width * scale * 4 ||
        !g_snes || !g_snes->ppu || !g_snes->cart || !KdcIsCourse(g_snes->ppu)) return 0;
    /* Preserve original fades, forced blank and mosaic transitions until
     * the fully visible course returns. Camera state is retained. */
    if (PPU_forcedBlank(g_snes->ppu) || PPU_brightness(g_snes->ppu) != 15 ||
        PPU_mosaicSize(g_snes->ppu) > 1) return 0;
    const double started = Milliseconds();
    Render render = {.pixels = dst, .pitch = pitch, .width = width, .height = height,
                     .depth = depth_buffer, .ppu = g_snes->ppu, .hd = KdcHdEnabled(), .scale = scale,
                     .vram = PpuRenderVram(g_snes->ppu), .field = field};
    if (!KdcCourseGeometryInit(&render.geometry, g_ram, sizeof(g_ram),
        g_snes->cart->rom, g_snes->cart->romImageSize)) return 0;
    render.camera = ViewCamera(&render);
    if (high_refresh) {
        if (!current.valid || current.width != width || current.height != height) return 0;
        Interpolate(&render, alpha);
        ReportDraw(alpha);
    } else {
        sprite_count = 0;
        KdcVisitOrbitSprites(render.ppu, g_ram, CollectSprite, NULL);
    }
    /* Select artwork after interpolation, so the view uses the camera of
     * this presentation while the animation metadata remains frame-latched. */
    for (unsigned i = 0; i < sprite_count; ++i) {
        KdcOrbitSprite *sprite = &sprite_buffer[i];
        if (sprite->world && !sprite->ground && sprite->view.family) {
            unsigned base = sprite->attributes & 0x100 ?
                PPU_objTileAdr2(render.ppu) : PPU_objTileAdr1(render.ppu);
            if (base + (sprite->attributes & 255) * 16 !=
                0x6000u + (sprite->attributes & 0x1ffu) * 16) continue;
            KdcApplySpriteView(g_snes->cart->rom, g_snes->cart->romImageSize,
                render.vram, sprite->view, render.camera.yaw,
                SpriteSize(render.ppu, sprite), sprite);
        }
    }
    /* Scale only the presentation transform, after native-space camera
     * framing and interpolation. Lead offsets are intentionally unzoomed. */
    render.width *= scale;
    render.height *= scale;
    render.camera.zoom *= scale;
    render.camera.lead_x *= scale;
    render.camera.lead_y *= scale;
    OrderAttachments();
    ++art_generation;

    /* The terrain texture is rebuilt only where its inputs changed. Above
     * one output pixel per source pixel the enlarged level adds detail;
     * at or below it the source image already has more than is shown. */
    render.terrain = KdcTerrainUpdate(render.ppu, g_ram, render.hd);
    if (render.terrain) {
        const KdcTerrain *terrain = render.terrain;
        const bool enlarged = terrain->hd && render.camera.zoom > 1.000001;
        render.texture = (Texture){
            .texels = enlarged ? terrain->hd : terrain->native,
            .scale = enlarged ? kKdcTerrainScale : 1,
            .left = terrain->left, .top = terrain->top, .colors = terrain->colors,
        };
        render.texture.width = terrain->width * render.texture.scale;
        render.texture.height = terrain->height * render.texture.scale;
        render.texture.stride = render.texture.width;
        render.smooth = enlarged;
    }
    render.samples = render.smooth ? 1 : Samples(render.camera.zoom);
    render.backdrop = Color(render.ppu->cgram[0]);
    if (SceneryLayout(render.ppu))
        render.scenery = KdcSceneryUpdate(render.ppu, g_ram, render.hd && scale > 1);
    if (!ProjectFaces(&render)) return 0;
    PreparePieces(&render);
    const double prepared = Milliseconds();
    KdcParallelFor((unsigned)((height + kBandRows - 1) / kBandRows), DrawBand, &render);
    ReportProfile(prepared - started, Milliseconds() - prepared);
    return 1;
}

int KdcOrbitDraw(uint8_t *dst, size_t pitch, const uint8_t *field,
                  int width, int height, double alpha)
{
    composed = Draw(dst, pitch, field, width, height, alpha, 1) != 0;
    return composed;
}

int KdcOrbitDrawHd(uint8_t *dst, size_t pitch, const uint8_t *field,
                  int width, int height, double alpha, unsigned scale)
{
    composed = Draw(dst, pitch, field, width, height, alpha, scale) != 0;
    return composed;
}
