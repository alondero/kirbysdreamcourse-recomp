#ifndef KDC_COURSE_ORBIT_RASTER_H
#define KDC_COURSE_ORBIT_RASTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A projected terrain triangle. Barycentric weights, depth and texture
 * coordinates are all a*x + b*y + c at the centre of output pixel (x,y). */
typedef struct KdcOrbitFace {
    int left, right, top, bottom;
    double weight[3][3], inv_slope[3];
    int8_t slope_sign[3];
    double depth[3], u[3], v[3];
    double sharp_u, sharp_v;
    uint32_t color;
    bool textured;
} KdcOrbitFace;

typedef struct KdcOrbitTexture {
    const uint8_t *texels;
    int stride, width, height;
    int scale, left, top;
    const uint32_t *colors;
    uint64_t revision;
    int dirty_y0, dirty_y1; /* in texel rows */
} KdcOrbitTexture;

typedef struct KdcOrbitGpuPiece {
    int x0, x1, y0, y1;
    int size, samples;
    unsigned attributes;
    bool masked;
    unsigned cell_offset; /* into KdcOrbitGpuScene.piece_cells */
    double native_x, native_y;
    double offset_x, offset_y;
    double sprite_z;
    double inverse[4][3], plane[3][2], step;
    double point_y, point_depth, left, top, scale;
} KdcOrbitGpuPiece;

typedef struct KdcOrbitGpuRect {
    int x0, x1, y0, y1;
} KdcOrbitGpuRect;

/* Persistent 32x32 HD tile atlas + per-frame 8x8 native scratch slots. */
typedef struct KdcOrbitGpuAtlas {
    uint64_t epoch;
    unsigned (*add_hd_tile)(const uint32_t *pixels32x32);
    unsigned (*add_native_tile)(const uint32_t *pixels8x8);
} KdcOrbitGpuAtlas;

typedef struct KdcOrbitGpuScene {
    int width, height, scale;
    const KdcOrbitFace *faces;
    unsigned face_count;
    const KdcOrbitTexture *texture;
    int samples;
    bool smooth;
    /* Terrain mask for OBJ priority masking (same revision as terrain). */
    const uint8_t *terrain_mask;
    int terrain_left, terrain_top, terrain_width, terrain_height;
    int terrain_dirty_y0, terrain_dirty_y1;
    uint64_t terrain_mask_revision;
    /* Background / scenery. */
    uint32_t backdrop;
    bool has_scenery;
    const uint8_t *scenery_texels;
    bool scenery_hd;
    uint64_t scenery_revision;
    const uint32_t *scenery_colors;
    uint16_t scenery_hscroll, scenery_vscroll;
    /* Sprites in draw order. */
    const KdcOrbitGpuPiece *decals, *bodies, *screen;
    unsigned decal_count, body_count, screen_count;
    const uint32_t *piece_cells;
    unsigned piece_cell_count;
    /* HUD passes. */
    bool hd_hud;
    const KdcOrbitGpuRect *window_rects;
    unsigned window_rect_count;
    uint32_t window_color;
    /* Pre-screen native BG3 HUD (when !hd_hud). 0 = skip, else 0xffRRGGBB. */
    const uint32_t *pre_hud;
    int pre_hud_y0, pre_hud_y1;
    /* Post-screen HD BG3 HUD (when hd_hud). 0 = skip, bit31=0 => solid,
     * bit31=1 => atlas tile descriptor. */
    const uint32_t *post_hd_hud;
    int post_hd_hud_y0, post_hd_hud_y1;
    /* Post-screen native field restore (when !hd_hud). */
    const uint32_t *restore_pixels;
    const uint8_t *restore_mask;
    int restore_y0, restore_y1;
} KdcOrbitGpuScene;

typedef const KdcOrbitGpuAtlas *KdcOrbitGpuBeginFn(void);
typedef unsigned KdcOrbitGpuComposeFn(const KdcOrbitGpuScene *scene);

/* Registers the desktop OpenGL orbit compositor without coupling course_orbit.c
 * to OpenGL in standalone fixtures or Android builds. */
void KdcOrbitSetGpuCompositor(KdcOrbitGpuBeginFn *begin, KdcOrbitGpuComposeFn *compose);

/* Pre-compiles OpenGL orbit shaders and allocates FBO once the context is active. */
void KdcOrbitGlPrewarm(int width, int height);
/* Returns NULL if OpenGL orbit composition is unavailable. */
const KdcOrbitGpuAtlas *KdcOrbitGlBegin(void);
/* Composes the complete orbit frame on OpenGL and returns its RGBA8 texture,
 * or 0 on error (caller should fall back to CPU). */
unsigned KdcOrbitGlCompose(const KdcOrbitGpuScene *scene);
/* Reads back a completed GPU orbit texture for differential tests. */
bool KdcOrbitGlReadback(unsigned texture, uint8_t *pixels, size_t pitch,
                        int width, int height);

#endif
