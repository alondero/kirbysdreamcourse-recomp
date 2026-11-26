#include "course_orbit_raster.h"

#include "gl_core_3_1.h"
#include "course_orbit_gpu_shader.h"
#include "snes/ppu.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    kColumnPixels = 16,
    kRowPixels = 8,
    kWordColumns = 1024,
    kFaceWords = 48,
    kPieceWords = 64,
    kMaxColumns = (kPpuBufWidth * 4 + kColumnPixels - 1) / kColumnPixels,
    kMaxBins = kMaxColumns * (240 * 4 / kRowPixels),
    kAtlasTileSide = 32,
    kAtlasCols = 64,
    kAtlasRows = 64,
    kAtlasSlots = kAtlasCols * kAtlasRows,
    kPersistentSlots = 3840
};

enum {
    kFaces,
    kBins,
    kTerrain,
    kPalette,
    kTerrainMask,
    kScenery,
    kPieces,
    kPieceCells,
    kSpriteAtlas,
    kRects,
    kHudMap,
    kHudMask,
    kColorOutput,
    kDepthOutput,
    kTextureCount
};

enum {
    kProgTerrain,
    kProgBg,
    kProgDecal,
    kProgWorldBody,
    kProgScreenBody,
    kProgRect,
    kProgHud,
    kProgCount
};

enum {
    kRingSlots = 2,
    kInitFaceRows = 128,
    kInitBinRows = 128,
    kInitPieceRows = 16,
    kInitCellRows = 16,
    kInitRectRows = 4
};

static GLuint programs[kProgCount], fbo, hw_depth_rbo, vao, texture[kTextureCount];
/* Second ring slot for per-frame dynamic textures to avoid CPU-GPU sync stalls
 * when uploading frame N while frame N-1 is still in flight on the GPU. */
static GLuint ring_tex[kRingSlots][kTextureCount];
static unsigned ring_index;
static GLint max_size;

/* Cached uniform locations. */
static struct {
    GLint columns, samples, smooth;
} u_terrain;
static struct {
    GLint colors, backdrop, scale, extra, hscroll, vscroll, has_scenery, scenery_hd;
} u_bg;
static struct {
    GLint output_size, base_instance, terrain_origin, has_terrain_mask;
} u_decal, u_world_body, u_screen_body;
static struct {
    GLint output_size, color;
} u_rect;
static struct {
    GLint output_size, y_range, scale, row_offset, hud_mode;
} u_hud;

static int output_w, output_h;
static int terrain_w, terrain_h, mask_w, mask_h;
static int hud_w, hud_h;
static uint64_t terrain_revision = UINT64_MAX;
static uint64_t mask_revision = UINT64_MAX;
static const uint8_t *terrain_ptr, *mask_ptr;
static bool unavailable, reported;

/* LRU cache of uploaded scenery textures so looping animated skies never
 * re-upload 1 MiB frames once seen. */
enum { kSceneryGpuSlots = 48 };
static struct {
    GLuint tex;
    const uint8_t *ptr;
    uint64_t revision;
    uint64_t last_use;
    int side;
} scenery_cache[kSceneryGpuSlots];
static uint64_t scenery_use_clock;
static GLuint active_scenery_tex;

static uint32_t *faces_buf, *bins_buf, *pieces_buf, *cells_buf, *rects_buf;
static unsigned face_rows, bin_rows, piece_rows, cell_rows, rect_rows;
static uint32_t bin_counts[kMaxBins];

/* Persistent 32x32 HD tile atlas + per-frame 8x8 scratch slots. */
static uint64_t atlas_epoch = 1;
static unsigned atlas_persistent, atlas_uploaded, atlas_scratch;
static bool atlas_overflowed;
static uint32_t atlas_staging[kAtlasSlots][kAtlasTileSide * kAtlasTileSide];
static unsigned native_pending[kAtlasSlots - kPersistentSlots];
static unsigned native_pending_count;

static bool IsRingUnit(unsigned unit)
{
    return unit == kFaces || unit == kBins || unit == kPalette ||
           unit == kPieces || unit == kPieceCells || unit == kRects ||
           unit == kHudMap || unit == kHudMask;
}

static GLuint UnitTex(unsigned unit)
{
    return IsRingUnit(unit) ? ring_tex[ring_index][unit] : texture[unit];
}

static void Bind(unsigned unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, UnitTex(unit));
}

static GLuint CompileShader(GLenum type, const char *header,
                            const char *part1, const char *part2, const char *part3)
{
    const char *sources[4] = {header, part1, part2 ? part2 : "", part3 ? part3 : ""};
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 4, sources, NULL);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) return shader;
    char log[1024] = {0};
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    fprintf(stderr, "OpenGL orbit shader: %s\n", log);
    glDeleteShader(shader);
    return 0;
}

static GLuint LinkProgram(const char *header, const char *vsrc,
                          const char *f1, const char *f2, const char *f3)
{
    GLuint vs = CompileShader(GL_VERTEX_SHADER, header, vsrc, NULL, NULL);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, header, f1, f2, f3);
    GLuint prog = 0;
    if (vs && fs) {
        prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (!prog) return 0;
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "OpenGL orbit program: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static void SetSamplers(GLuint prog, const char *const *names, const unsigned *units, unsigned count)
{
    glUseProgram(prog);
    for (unsigned i = 0; i < count; ++i)
        glUniform1i(glGetUniformLocation(prog, names[i]), (GLint)units[i]);
}

static bool Init(void)
{
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    if (max_size < kAtlasTileSide * kAtlasCols || max_size < kAtlasTileSide * kAtlasRows)
        return false;
    const char *header_fp64 = ogl_IsVersionGEQ(4, 0)
        ? "#version 400 core\n"
        : "#version 330 core\n"
          "#extension GL_ARB_gpu_shader_fp64 : require\n"
          "#extension GL_ARB_gpu_shader5 : require\n";
    const char *header_330 = "#version 330 core\n";

    programs[kProgTerrain] = LinkProgram(header_fp64, kOrbitFullscreenVertex,
                                         kOrbitDepthHelper, kOrbitTerrainFragment, NULL);
    programs[kProgBg] = LinkProgram(header_330, kOrbitBgVertex,
                                    kOrbitBgFragment, NULL, NULL);
    programs[kProgDecal] = LinkProgram(header_fp64, kOrbitPieceVertex,
                                       kOrbitPieceCommon, kOrbitDecalFragment, NULL);
    programs[kProgWorldBody] = LinkProgram(header_fp64, kOrbitPieceVertex,
                                           kOrbitDepthHelper, kOrbitPieceCommon,
                                           kOrbitWorldBodyFragment);
    programs[kProgScreenBody] = LinkProgram(header_fp64, kOrbitPieceVertex,
                                            kOrbitPieceCommon, kOrbitScreenBodyFragment, NULL);
    programs[kProgRect] = LinkProgram(header_330, kOrbitRectVertex,
                                      kOrbitRectFragment, NULL, NULL);
    programs[kProgHud] = LinkProgram(header_330, kOrbitHudVertex,
                                     kOrbitHudFragment, NULL, NULL);
    for (unsigned i = 0; i < kProgCount; ++i)
        if (!programs[i]) return false;

    {
        const char *n[] = {"faceData", "binData", "terrain", "palette"};
        const unsigned u[] = {kFaces, kBins, kTerrain, kPalette};
        SetSamplers(programs[kProgTerrain], n, u, 4);
        u_terrain.columns = glGetUniformLocation(programs[kProgTerrain], "columns");
        u_terrain.samples = glGetUniformLocation(programs[kProgTerrain], "samples");
        u_terrain.smooth = glGetUniformLocation(programs[kProgTerrain], "smoothTexture");
    }
    {
        const char *n[] = {"sceneryTex"};
        const unsigned u[] = {kScenery};
        SetSamplers(programs[kProgBg], n, u, 1);
        u_bg.colors = glGetUniformLocation(programs[kProgBg], "sceneryColors");
        u_bg.backdrop = glGetUniformLocation(programs[kProgBg], "backdrop");
        u_bg.scale = glGetUniformLocation(programs[kProgBg], "scale");
        u_bg.extra = glGetUniformLocation(programs[kProgBg], "extra");
        u_bg.hscroll = glGetUniformLocation(programs[kProgBg], "hScroll");
        u_bg.vscroll = glGetUniformLocation(programs[kProgBg], "vScroll");
        u_bg.has_scenery = glGetUniformLocation(programs[kProgBg], "hasScenery");
        u_bg.scenery_hd = glGetUniformLocation(programs[kProgBg], "sceneryHd");
    }
    {
        const char *n[] = {"pieceData", "pieceCells", "spriteAtlas", "terrainMask", "depthTex"};
        const unsigned u[] = {kPieces, kPieceCells, kSpriteAtlas, kTerrainMask, kDepthOutput};
        SetSamplers(programs[kProgDecal], n, u, 5);
        u_decal.output_size = glGetUniformLocation(programs[kProgDecal], "outputSize");
        u_decal.base_instance = glGetUniformLocation(programs[kProgDecal], "baseInstance");
        u_decal.terrain_origin = glGetUniformLocation(programs[kProgDecal], "terrainOrigin");
        u_decal.has_terrain_mask = glGetUniformLocation(programs[kProgDecal], "hasTerrainMask");
    }
    {
        const char *n[] = {"pieceData", "pieceCells", "spriteAtlas", "terrainMask"};
        const unsigned u[] = {kPieces, kPieceCells, kSpriteAtlas, kTerrainMask};
        SetSamplers(programs[kProgWorldBody], n, u, 4);
        u_world_body.output_size = glGetUniformLocation(programs[kProgWorldBody], "outputSize");
        u_world_body.base_instance = glGetUniformLocation(programs[kProgWorldBody], "baseInstance");
        u_world_body.terrain_origin = glGetUniformLocation(programs[kProgWorldBody], "terrainOrigin");
        u_world_body.has_terrain_mask = glGetUniformLocation(programs[kProgWorldBody], "hasTerrainMask");
        SetSamplers(programs[kProgScreenBody], n, u, 4);
        u_screen_body.output_size = glGetUniformLocation(programs[kProgScreenBody], "outputSize");
        u_screen_body.base_instance = glGetUniformLocation(programs[kProgScreenBody], "baseInstance");
        u_screen_body.terrain_origin = glGetUniformLocation(programs[kProgScreenBody], "terrainOrigin");
        u_screen_body.has_terrain_mask = glGetUniformLocation(programs[kProgScreenBody], "hasTerrainMask");
    }
    {
        const char *n[] = {"rectData"};
        const unsigned u[] = {kRects};
        SetSamplers(programs[kProgRect], n, u, 1);
        u_rect.output_size = glGetUniformLocation(programs[kProgRect], "outputSize");
        u_rect.color = glGetUniformLocation(programs[kProgRect], "rectColor");
    }
    {
        const char *n[] = {"hudMap", "hudMask", "spriteAtlas"};
        const unsigned u[] = {kHudMap, kHudMask, kSpriteAtlas};
        SetSamplers(programs[kProgHud], n, u, 3);
        u_hud.output_size = glGetUniformLocation(programs[kProgHud], "outputSize");
        u_hud.y_range = glGetUniformLocation(programs[kProgHud], "yRange");
        u_hud.scale = glGetUniformLocation(programs[kProgHud], "scale");
        u_hud.row_offset = glGetUniformLocation(programs[kProgHud], "rowOffset");
        u_hud.hud_mode = glGetUniformLocation(programs[kProgHud], "hudMode");
    }
    glUseProgram(0);

    glGenVertexArrays(1, &vao);
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &hw_depth_rbo);
    glGenTextures(kTextureCount, texture);
    for (unsigned i = 0; i < kTextureCount; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, texture[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ring_tex[0][i] = texture[i];
        if (IsRingUnit(i)) {
            glGenTextures(1, &ring_tex[1][i]);
            glBindTexture(GL_TEXTURE_2D, ring_tex[1][i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        } else {
            ring_tex[1][i] = texture[i];
        }
    }
    for (unsigned r = 0; r < kRingSlots; ++r) {
        glActiveTexture(GL_TEXTURE0 + kPalette);
        glBindTexture(GL_TEXTURE_2D, ring_tex[r][kPalette]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 128, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
    }
    Bind(kSpriteAtlas);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, kAtlasTileSide * kAtlasCols,
                 kAtlasTileSide * kAtlasRows, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
    const uint8_t zero8 = 0;
    Bind(kTerrainMask);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 1, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &zero8);
    Bind(kScenery);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 1, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &zero8);
    active_scenery_tex = texture[kScenery];
    glActiveTexture(GL_TEXTURE0);
    return glGetError() == GL_NO_ERROR;
}

static unsigned AddHdTile(const uint32_t *pixels32x32)
{
    if (atlas_persistent >= kPersistentSlots) {
        atlas_overflowed = true;
        return 0;
    }
    memcpy(atlas_staging[atlas_persistent], pixels32x32,
           kAtlasTileSide * kAtlasTileSide * sizeof(uint32_t));
    return ++atlas_persistent;
}

static unsigned AddNativeTile(const uint32_t *pixels8x8)
{
    if (atlas_scratch >= kAtlasSlots - kPersistentSlots) return 0;
    const unsigned slot_index = kPersistentSlots + atlas_scratch++;
    uint32_t *dst = atlas_staging[slot_index];
    for (unsigned r = 0; r < 8; ++r)
        memcpy(dst + r * kAtlasTileSide, pixels8x8 + r * 8, 8 * sizeof(uint32_t));
    native_pending[native_pending_count++] = slot_index;
    return slot_index + 1;
}

static bool BindOutput(int width, int height);
static bool ResizeWords(uint32_t **buffer, unsigned *rows, unsigned wanted, unsigned unit);
static bool EnsureHudTextures(int native_w, int native_h);

void KdcOrbitGlPrewarm(int width, int height)
{
    if (unavailable || programs[kProgTerrain]) return;
    if (!Init()) {
        fprintf(stderr, "GPU orbit composition unavailable; using CPU rasterization\n");
        unavailable = true;
        return;
    }
    /* Pre-allocate dynamic ring textures and FBO at target size, then execute
     * a tiny draw of every program so drivers finish JIT compilation at boot. */
    if (!ResizeWords(&faces_buf, &face_rows, kInitFaceRows, kFaces) ||
        !ResizeWords(&bins_buf, &bin_rows, kInitBinRows, kBins) ||
        !ResizeWords(&pieces_buf, &piece_rows, kInitPieceRows, kPieces) ||
        !ResizeWords(&cells_buf, &cell_rows, kInitCellRows, kPieceCells) ||
        !ResizeWords(&rects_buf, &rect_rows, kInitRectRows, kRects) ||
        !EnsureHudTextures(kPpuBufWidth, 240))
        return;
    memset(faces_buf, 0, kWordColumns * sizeof(uint32_t));
    memset(bins_buf, 0, kWordColumns * sizeof(uint32_t));
    memset(pieces_buf, 0, kWordColumns * sizeof(uint32_t));
    memset(cells_buf, 0, kWordColumns * sizeof(uint32_t));
    memset(rects_buf, 0, kWordColumns * sizeof(uint32_t));
    const uint8_t zero8 = 0;
    Bind(kTerrain);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 1, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &zero8);
    terrain_w = terrain_h = 1;
    terrain_revision = UINT64_MAX;
    terrain_ptr = NULL;
    const int pw = width > 0 ? width : 16;
    const int ph = height > 0 ? height : 16;
    if (!BindOutput(pw, ph)) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glActiveTexture(GL_TEXTURE0);
        return;
    }
    for (unsigned i = 0; i < kColorOutput; ++i) Bind(i);
    glActiveTexture(GL_TEXTURE0 + kScenery);
    glBindTexture(GL_TEXTURE_2D, active_scenery_tex);
    GLint prev_vao;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glViewport(0, 0, 16, 16);
    glBindVertexArray(vao);
    const GLenum mrt[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, mrt);
    glUseProgram(programs[kProgTerrain]);
    glUniform1i(u_terrain.columns, 1);
    glUniform1i(u_terrain.samples, 1);
    glUniform1i(u_terrain.smooth, 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glUseProgram(programs[kProgBg]);
    glUniform1i(u_bg.has_scenery, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    Bind(kDepthOutput);
    glUseProgram(programs[kProgDecal]);
    glUniform2i(u_decal.output_size, 16, 16);
    glUniform1ui(u_decal.base_instance, 0);
    glUniform1i(u_decal.has_terrain_mask, 0);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(programs[kProgWorldBody]);
    glUniform2i(u_world_body.output_size, 16, 16);
    glUniform1ui(u_world_body.base_instance, 0);
    glUniform1i(u_world_body.has_terrain_mask, 0);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 1);
    glUseProgram(programs[kProgScreenBody]);
    glUniform2i(u_screen_body.output_size, 16, 16);
    glUniform1ui(u_screen_body.base_instance, 0);
    glUniform1i(u_screen_body.has_terrain_mask, 0);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 1);
    glUseProgram(programs[kProgRect]);
    glUniform2i(u_rect.output_size, 16, 16);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, 1);
    glUseProgram(programs[kProgHud]);
    glUniform2i(u_hud.output_size, 16, 16);
    glUniform2i(u_hud.y_range, 0, 4);
    glUniform1i(u_hud.scale, 4);
    glUniform1i(u_hud.row_offset, 0);
    glUniform1i(u_hud.hud_mode, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glUseProgram(0);
    glBindVertexArray((GLuint)prev_vao);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glFinish();
    (void)glGetError();
}

const KdcOrbitGpuAtlas *KdcOrbitGlBegin(void)
{
    static KdcOrbitGpuAtlas atlas = {
        .add_hd_tile = AddHdTile,
        .add_native_tile = AddNativeTile,
    };
    if (unavailable) return NULL;
    if (!programs[kProgTerrain] && !Init()) {
        fprintf(stderr, "GPU orbit composition unavailable; using CPU rasterization\n");
        unavailable = true;
        return NULL;
    }
    if (atlas_overflowed || atlas_persistent >= kPersistentSlots) {
        atlas_persistent = atlas_uploaded = 0;
        atlas_overflowed = false;
        ++atlas_epoch;
    }
    atlas_scratch = 0;
    native_pending_count = 0;
    atlas.epoch = atlas_epoch;
    return &atlas;
}

static unsigned WordRows(size_t words)
{
    return (unsigned)((words + kWordColumns - 1) / kWordColumns);
}

static bool ResizeWords(uint32_t **buffer, unsigned *rows, unsigned wanted, unsigned unit)
{
    if (!wanted) return true;
    if (wanted <= *rows) return true;
    unsigned next = *rows ? *rows * 2 : wanted;
    if (next < wanted) next = wanted;
    if (next > (unsigned)max_size) next = (unsigned)max_size;
    if (wanted > next) return false;
    uint32_t *grown = realloc(*buffer, (size_t)next * kWordColumns * sizeof(*grown));
    if (!grown) return false;
    *buffer = grown;
    const unsigned slots = IsRingUnit(unit) ? kRingSlots : 1;
    glActiveTexture(GL_TEXTURE0 + unit);
    for (unsigned r = 0; r < slots; ++r) {
        glBindTexture(GL_TEXTURE_2D, ring_tex[r][unit]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, kWordColumns, next, 0,
                     GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
    }
    glBindTexture(GL_TEXTURE_2D, UnitTex(unit));
    if (glGetError() != GL_NO_ERROR) return false;
    *rows = next;
    return true;
}

static void UploadWords(unsigned unit, uint32_t *words, size_t count)
{
    if (!count) return;
    unsigned rows = WordRows(count);
    if ((size_t)rows * kWordColumns > count)
        memset(words + count, 0, ((size_t)rows * kWordColumns - count) * sizeof(*words));
    Bind(unit);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kWordColumns, rows,
                    GL_RED_INTEGER, GL_UNSIGNED_INT, words);
}

static void PackFace(uint32_t *dst, const KdcOrbitFace *face)
{
    dst[0] = (uint32_t)face->left;
    dst[1] = (uint32_t)face->right;
    dst[2] = (uint32_t)face->top;
    dst[3] = (uint32_t)face->bottom;
    dst[4] = 0;
    for (unsigned edge = 0; edge < 3; ++edge)
        dst[4] |= ((unsigned)face->slope_sign[edge] & 3u) << (edge * 2);
    dst[5] = face->color;
    dst[6] = face->textured;
    dst[7] = 0;
    const double coefficient[] = {
        face->weight[0][1], face->weight[1][1], face->weight[2][1],
        face->weight[0][2], face->weight[1][2], face->weight[2][2],
        face->inv_slope[0], face->inv_slope[1], face->inv_slope[2],
        face->depth[0], face->depth[1], face->depth[2],
        face->u[0], face->u[1], face->u[2], face->v[0], face->v[1], face->v[2],
        face->sharp_u, face->sharp_v,
    };
    _Static_assert(sizeof(coefficient) == (kFaceWords - 8) * sizeof(uint32_t),
                   "terrain shader face layout");
    memcpy(dst + 8, coefficient, sizeof(coefficient));
}

static bool UploadGeometry(const KdcOrbitFace *geometry, unsigned count, int columns, int rows)
{
    const unsigned bin_count = (unsigned)(columns * rows);
    if (!bin_count || bin_count > kMaxBins) return false;
    if (count) {
        const size_t face_words = (size_t)count * kFaceWords;
        if (!ResizeWords(&faces_buf, &face_rows, WordRows(face_words), kFaces))
            return false;
        memset(bin_counts, 0, bin_count * sizeof(*bin_counts));
        for (unsigned i = 0; i < count; ++i) {
            const KdcOrbitFace *face = &geometry[i];
            PackFace(faces_buf + (size_t)i * kFaceWords, face);
            const int y0 = face->top / kRowPixels, y1 = face->bottom / kRowPixels;
            const int x0 = face->left / kColumnPixels, x1 = face->right / kColumnPixels;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    ++bin_counts[y * columns + x];
        }
        unsigned total = bin_count + 1;
        for (unsigned i = 0; i < bin_count; ++i) total += bin_counts[i];
        if (!ResizeWords(&bins_buf, &bin_rows, WordRows(total), kBins)) return false;
        unsigned next = bin_count + 1;
        for (unsigned i = 0; i < bin_count; ++i) {
            bins_buf[i] = next;
            next += bin_counts[i];
            bin_counts[i] = bins_buf[i];
        }
        bins_buf[bin_count] = next;
        for (unsigned i = 0; i < count; ++i) {
            const KdcOrbitFace *face = &geometry[i];
            const int y0 = face->top / kRowPixels, y1 = face->bottom / kRowPixels;
            const int x0 = face->left / kColumnPixels, x1 = face->right / kColumnPixels;
            const uint32_t base = i * kFaceWords;
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    bins_buf[bin_counts[y * columns + x]++] = base;
        }
        UploadWords(kFaces, faces_buf, face_words);
        UploadWords(kBins, bins_buf, total);
    } else {
        const unsigned total = bin_count + 1;
        if (!ResizeWords(&bins_buf, &bin_rows, WordRows(total), kBins)) return false;
        for (unsigned i = 0; i <= bin_count; ++i) bins_buf[i] = total;
        UploadWords(kBins, bins_buf, total);
    }
    return true;
}

static void PackPiece(uint32_t *dst, const KdcOrbitGpuPiece *p, bool is_decal)
{
    memset(dst, 0, kPieceWords * sizeof(*dst));
    dst[0] = (uint32_t)p->x0;
    dst[1] = (uint32_t)p->x1;
    dst[2] = (uint32_t)p->y0;
    dst[3] = (uint32_t)p->y1;
    dst[4] = (uint32_t)p->size;
    dst[5] = (uint32_t)p->samples;
    dst[6] = p->attributes;
    dst[7] = p->masked ? 1u : 0u;
    dst[8] = p->cell_offset;
    dst[9] = 0;
    double coeff[24] = {0};
    coeff[0] = p->native_x;
    coeff[1] = p->native_y;
    coeff[2] = p->offset_x;
    coeff[3] = p->offset_y;
    coeff[4] = p->sprite_z;
    if (is_decal) {
        for (unsigned k = 0; k < 4; ++k)
            for (unsigned j = 0; j < 3; ++j)
                coeff[5 + k * 3 + j] = p->inverse[k][j];
        for (unsigned k = 0; k < 3; ++k)
            for (unsigned j = 0; j < 2; ++j)
                coeff[17 + k * 2 + j] = p->plane[k][j];
        coeff[23] = p->step;
    } else {
        const double inv_scale = 1.0 / p->scale;
        coeff[5] = p->point_y;
        coeff[6] = p->point_depth;
        coeff[7] = p->left;
        coeff[8] = p->top;
        coeff[9] = inv_scale;
        coeff[10] = inv_scale / p->samples;
    }
    _Static_assert(10 + sizeof(coeff) / sizeof(uint32_t) <= kPieceWords,
                   "piece shader layout");
    memcpy(dst + 10, coeff, sizeof(coeff));
}

static bool UploadTerrain(const KdcOrbitTexture *source)
{
    static const uint32_t black[128];
    const bool valid = source && source->texels != NULL;
    const int w = valid ? source->width : 1, h = valid ? source->height : 1;
    const uint8_t zero = 0;
    if (w > max_size || h > max_size) return false;
    Bind(kTerrain);
    if (w != terrain_w || h != terrain_h ||
        (valid ? source->revision : 0) != terrain_revision ||
        (valid ? source->texels : NULL) != terrain_ptr) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, valid ? source->stride : 0);
        const bool partial = valid && w == terrain_w && h == terrain_h &&
            source->texels == terrain_ptr && source->revision == terrain_revision + 1 &&
            source->dirty_y0 >= 0 && source->dirty_y1 <= h;
        if (partial) {
            if (source->dirty_y0 < source->dirty_y1) {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, source->dirty_y0, w,
                                source->dirty_y1 - source->dirty_y0,
                                GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                                source->texels + (size_t)source->dirty_y0 * source->stride);
            }
        } else if (w == terrain_w && h == terrain_h) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED_INTEGER,
                            GL_UNSIGNED_BYTE, valid ? source->texels : &zero);
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, w, h, 0, GL_RED_INTEGER,
                         GL_UNSIGNED_BYTE, valid ? source->texels : &zero);
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (glGetError() != GL_NO_ERROR) return false;
        terrain_w = w;
        terrain_h = h;
        terrain_revision = valid ? source->revision : 0;
        terrain_ptr = valid ? source->texels : NULL;
    }
    Bind(kPalette);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 1, GL_RED_INTEGER,
                    GL_UNSIGNED_INT, valid ? source->colors : black);
    return true;
}

static bool UploadTerrainMask(const KdcOrbitGpuScene *scene)
{
    if (!scene->terrain_mask) return true;
    const int w = scene->terrain_width, h = scene->terrain_height;
    if (w > max_size || h > max_size) return false;
    if (w == mask_w && h == mask_h && scene->terrain_mask_revision == mask_revision &&
        scene->terrain_mask == mask_ptr)
        return true;
    Bind(kTerrainMask);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const bool partial = w == mask_w && h == mask_h &&
        scene->terrain_mask == mask_ptr &&
        scene->terrain_mask_revision == mask_revision + 1 &&
        scene->terrain_dirty_y0 >= 0 && scene->terrain_dirty_y1 <= h;
    if (partial) {
        if (scene->terrain_dirty_y0 < scene->terrain_dirty_y1) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, scene->terrain_dirty_y0, w,
                            scene->terrain_dirty_y1 - scene->terrain_dirty_y0,
                            GL_RED_INTEGER, GL_UNSIGNED_BYTE,
                            scene->terrain_mask + (size_t)scene->terrain_dirty_y0 * w);
        }
    } else if (w == mask_w && h == mask_h) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED_INTEGER,
                        GL_UNSIGNED_BYTE, scene->terrain_mask);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, w, h, 0, GL_RED_INTEGER,
                     GL_UNSIGNED_BYTE, scene->terrain_mask);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (glGetError() != GL_NO_ERROR) return false;
    mask_w = w;
    mask_h = h;
    mask_revision = scene->terrain_mask_revision;
    mask_ptr = scene->terrain_mask;
    return true;
}

static bool UploadScenery(const KdcOrbitGpuScene *scene)
{
    active_scenery_tex = texture[kScenery];
    if (!scene->has_scenery || !scene->scenery_texels) return true;
    const int side = scene->scenery_hd ? 1024 : 256;
    ++scenery_use_clock;
    unsigned victim = 0;
    for (unsigned i = 0; i < kSceneryGpuSlots; ++i) {
        if (scenery_cache[i].tex && scenery_cache[i].side == side &&
            scenery_cache[i].revision == scene->scenery_revision &&
            scenery_cache[i].ptr == scene->scenery_texels) {
            scenery_cache[i].last_use = scenery_use_clock;
            active_scenery_tex = scenery_cache[i].tex;
            return true;
        }
        if (!scenery_cache[i].tex) {
            victim = i;
            break;
        }
        if (scenery_cache[i].last_use < scenery_cache[victim].last_use)
            victim = i;
    }
    glActiveTexture(GL_TEXTURE0 + kScenery);
    if (!scenery_cache[victim].tex) {
        glGenTextures(1, &scenery_cache[victim].tex);
        glBindTexture(GL_TEXTURE_2D, scenery_cache[victim].tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else {
        glBindTexture(GL_TEXTURE_2D, scenery_cache[victim].tex);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (scenery_cache[victim].side == side)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, side, side, GL_RED_INTEGER,
                        GL_UNSIGNED_BYTE, scene->scenery_texels);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, side, side, 0, GL_RED_INTEGER,
                     GL_UNSIGNED_BYTE, scene->scenery_texels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (glGetError() != GL_NO_ERROR) return false;
    scenery_cache[victim].side = side;
    scenery_cache[victim].revision = scene->scenery_revision;
    scenery_cache[victim].ptr = scene->scenery_texels;
    scenery_cache[victim].last_use = scenery_use_clock;
    active_scenery_tex = scenery_cache[victim].tex;
    return true;
}

static void UploadAtlasUpdates(void)
{
    if (atlas_uploaded == atlas_persistent && !native_pending_count) return;
    Bind(kSpriteAtlas);
    for (; atlas_uploaded < atlas_persistent; ++atlas_uploaded) {
        glTexSubImage2D(GL_TEXTURE_2D, 0,
                        (GLint)((atlas_uploaded % kAtlasCols) * kAtlasTileSide),
                        (GLint)((atlas_uploaded / kAtlasCols) * kAtlasTileSide),
                        kAtlasTileSide, kAtlasTileSide,
                        GL_RED_INTEGER, GL_UNSIGNED_INT, atlas_staging[atlas_uploaded]);
    }
    for (unsigned i = 0; i < native_pending_count; ++i) {
        const unsigned slot = native_pending[i];
        glPixelStorei(GL_UNPACK_ROW_LENGTH, kAtlasTileSide);
        glTexSubImage2D(GL_TEXTURE_2D, 0,
                        (GLint)((slot % kAtlasCols) * kAtlasTileSide),
                        (GLint)((slot / kAtlasCols) * kAtlasTileSide),
                        8, 8, GL_RED_INTEGER, GL_UNSIGNED_INT, atlas_staging[slot]);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
}

static bool EnsureHudTextures(int native_w, int native_h)
{
    if (native_w <= hud_w && native_h <= hud_h) return true;
    const int nw = native_w > hud_w ? native_w : hud_w;
    const int nh = native_h > hud_h ? native_h : hud_h;
    for (unsigned r = 0; r < kRingSlots; ++r) {
        glActiveTexture(GL_TEXTURE0 + kHudMap);
        glBindTexture(GL_TEXTURE_2D, ring_tex[r][kHudMap]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, nw, nh, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
        glActiveTexture(GL_TEXTURE0 + kHudMask);
        glBindTexture(GL_TEXTURE_2D, ring_tex[r][kHudMask]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, nw, nh, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
    }
    if (glGetError() != GL_NO_ERROR) return false;
    hud_w = nw;
    hud_h = nh;
    return true;
}

static bool BindOutput(int width, int height)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (width == output_w && height == output_h) return true;
    Bind(kColorOutput);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texture[kColorOutput], 0);
    Bind(kDepthOutput);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                           texture[kDepthOutput], 0);
    glBindRenderbuffer(GL_RENDERBUFFER, hw_depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hw_depth_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    const GLenum attachments[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, attachments);
    if (glGetError() != GL_NO_ERROR ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return false;
    output_w = width;
    output_h = height;
    return true;
}

static void DrawHudMap(int width, int height, int scale, int native_w,
                       const uint32_t *map, const uint8_t *mask,
                       int y0, int y1, int mode, bool blend)
{
    if (!map || y0 >= y1) return;
    const int rows = y1 - y0;
    Bind(kHudMap);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, native_w, rows,
                    GL_RED_INTEGER, GL_UNSIGNED_INT, map);
    if (mask) {
        Bind(kHudMask);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, native_w, rows,
                        GL_RED_INTEGER, GL_UNSIGNED_BYTE, mask);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }
    if (blend) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    } else {
        glDisable(GL_BLEND);
    }
    glUseProgram(programs[kProgHud]);
    glUniform2i(u_hud.output_size, width, height);
    glUniform2i(u_hud.y_range, y0 * scale, y1 * scale);
    glUniform1i(u_hud.scale, scale);
    glUniform1i(u_hud.row_offset, y0);
    glUniform1i(u_hud.hud_mode, mode);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

unsigned KdcOrbitGlCompose(const KdcOrbitGpuScene *scene)
{
    if (unavailable || !programs[kProgTerrain] || !scene || scene->scale < 1)
        return 0;
    ring_index = (ring_index + 1u) % kRingSlots;
    const int width = scene->width, height = scene->height, scale = scene->scale;
    const int native_w = width / scale, native_h = height / scale;
    const int columns = (width + kColumnPixels - 1) / kColumnPixels;
    const int rows = (height + kRowPixels - 1) / kRowPixels;

    if (!UploadGeometry(scene->faces, scene->face_count, columns, rows)) {
        glActiveTexture(GL_TEXTURE0);
        return 0;
    }

    const unsigned total_pieces = scene->decal_count + scene->body_count + scene->screen_count;
    if (total_pieces) {
        const size_t piece_words = (size_t)total_pieces * kPieceWords;
        if (!ResizeWords(&pieces_buf, &piece_rows, WordRows(piece_words), kPieces))
            return 0;
        unsigned cursor = 0;
        for (unsigned i = 0; i < scene->decal_count; ++i)
            PackPiece(pieces_buf + (size_t)cursor++ * kPieceWords, &scene->decals[i], true);
        for (unsigned i = 0; i < scene->body_count; ++i)
            PackPiece(pieces_buf + (size_t)cursor++ * kPieceWords, &scene->bodies[i], false);
        for (unsigned i = 0; i < scene->screen_count; ++i)
            PackPiece(pieces_buf + (size_t)cursor++ * kPieceWords, &scene->screen[i], false);
        UploadWords(kPieces, pieces_buf, piece_words);
        if (scene->piece_cell_count) {
            if (!ResizeWords(&cells_buf, &cell_rows, WordRows(scene->piece_cell_count), kPieceCells))
                return 0;
            memcpy(cells_buf, scene->piece_cells, scene->piece_cell_count * sizeof(uint32_t));
            UploadWords(kPieceCells, cells_buf, scene->piece_cell_count);
        }
    }

    if (scene->window_rect_count) {
        const size_t rect_words = (size_t)scene->window_rect_count * 4;
        if (!ResizeWords(&rects_buf, &rect_rows, WordRows(rect_words), kRects))
            return 0;
        for (unsigned i = 0; i < scene->window_rect_count; ++i) {
            rects_buf[i * 4 + 0] = (uint32_t)scene->window_rects[i].x0;
            rects_buf[i * 4 + 1] = (uint32_t)scene->window_rects[i].x1;
            rects_buf[i * 4 + 2] = (uint32_t)scene->window_rects[i].y0;
            rects_buf[i * 4 + 3] = (uint32_t)scene->window_rects[i].y1;
        }
        UploadWords(kRects, rects_buf, rect_words);
    }

    if (!UploadTerrain(scene->texture) || !UploadTerrainMask(scene) ||
        !UploadScenery(scene) || !EnsureHudTextures(native_w, native_h)) {
        glActiveTexture(GL_TEXTURE0);
        return 0;
    }
    UploadAtlasUpdates();

    for (unsigned i = 0; i < kColorOutput; ++i) Bind(i);
    glActiveTexture(GL_TEXTURE0 + kScenery);
    glBindTexture(GL_TEXTURE_2D, active_scenery_tex);

    if (!BindOutput(width, height)) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glActiveTexture(GL_TEXTURE0);
        return 0;
    }

    GLint previous_vao;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
    GLboolean prev_dither = glIsEnabled(GL_DITHER);
    GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prev_blend = glIsEnabled(GL_BLEND);
    GLboolean prev_cull = glIsEnabled(GL_CULL_FACE);
    GLboolean prev_scissor = glIsEnabled(GL_SCISSOR_TEST);

    glViewport(0, 0, width, height);
    glBindVertexArray(vao);
    glDisable(GL_DITHER);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);

    /* Pass 1: Exact binned terrain resolving Color, raw R32F Depth and
     * bijective GL_DEPTH_COMPONENT32F (0.0 where uncovered). */
    const GLenum mrt[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, mrt);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    glUseProgram(programs[kProgTerrain]);
    glUniform1i(u_terrain.columns, columns);
    glUniform1i(u_terrain.samples, scene->samples);
    glUniform1i(u_terrain.smooth, scene->smooth ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    /* Subsequent passes only write Color (ATTACHMENT0). */
    glDrawBuffer(GL_COLOR_ATTACHMENT0);

    /* Pass 2: Background / Scenery where depth == 0.0 (Early-Z skips terrain). */
    glDepthFunc(GL_EQUAL);
    glDepthMask(GL_FALSE);
    glUseProgram(programs[kProgBg]);
    if (scene->has_scenery && scene->scenery_colors)
        glUniform1uiv(u_bg.colors, 32, scene->scenery_colors);
    glUniform1ui(u_bg.backdrop, scene->backdrop);
    glUniform1i(u_bg.scale, scale);
    glUniform1i(u_bg.extra, (native_w - 256) / 2);
    glUniform1i(u_bg.hscroll, (GLint)scene->scenery_hscroll);
    glUniform1i(u_bg.vscroll, (GLint)scene->scenery_vscroll);
    glUniform1i(u_bg.has_scenery, scene->has_scenery && scene->scenery_texels ? 1 : 0);
    glUniform1i(u_bg.scenery_hd, scene->scenery_hd ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    /* Pass 3: Ground Decals (read raw R32F depthTex, write Color only). */
    glDisable(GL_DEPTH_TEST);
    if (scene->decal_count) {
        Bind(kDepthOutput);
        glUseProgram(programs[kProgDecal]);
        glUniform2i(u_decal.output_size, width, height);
        glUniform1ui(u_decal.base_instance, 0);
        glUniform2i(u_decal.terrain_origin, scene->terrain_left, scene->terrain_top);
        glUniform1i(u_decal.has_terrain_mask, scene->terrain_mask != NULL ? 1 : 0);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)scene->decal_count);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    /* Pass 4: World Upright Bodies (hardware GL_GEQUAL depth test + write). */
    if (scene->body_count) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GEQUAL);
        glDepthMask(GL_TRUE);
        glUseProgram(programs[kProgWorldBody]);
        glUniform2i(u_world_body.output_size, width, height);
        glUniform1ui(u_world_body.base_instance, scene->decal_count);
        glUniform2i(u_world_body.terrain_origin, scene->terrain_left, scene->terrain_top);
        glUniform1i(u_world_body.has_terrain_mask, scene->terrain_mask != NULL ? 1 : 0);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)scene->body_count);
    }

    /* Pass 5: HUD & Screen Sprites (no depth test). */
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    if (scene->hd_hud) {
        if (scene->window_rect_count) {
            glDisable(GL_BLEND);
            glUseProgram(programs[kProgRect]);
            glUniform2i(u_rect.output_size, width, height);
            glUniform1ui(u_rect.color, scene->window_color);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)scene->window_rect_count);
        }
    } else if (scene->pre_hud) {
        DrawHudMap(width, height, scale, native_w, scene->pre_hud, NULL,
                   scene->pre_hud_y0, scene->pre_hud_y1, 0, false);
    }

    if (scene->screen_count) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glUseProgram(programs[kProgScreenBody]);
        glUniform2i(u_screen_body.output_size, width, height);
        glUniform1ui(u_screen_body.base_instance, scene->decal_count + scene->body_count);
        glUniform2i(u_screen_body.terrain_origin, scene->terrain_left, scene->terrain_top);
        glUniform1i(u_screen_body.has_terrain_mask, 0);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)scene->screen_count);
    }

    if (scene->hd_hud) {
        if (scene->post_hd_hud)
            DrawHudMap(width, height, scale, native_w, scene->post_hd_hud, NULL,
                       scene->post_hd_hud_y0, scene->post_hd_hud_y1, 2, true);
    } else if (scene->restore_pixels && scene->restore_mask) {
        DrawHudMap(width, height, scale, native_w, scene->restore_pixels, scene->restore_mask,
                   scene->restore_y0, scene->restore_y1, 1, false);
    }

    /* Restore OpenGL state cleanly for the host presenter. */
    glUseProgram(0);
    glBindVertexArray((GLuint)previous_vao);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    if (prev_dither) glEnable(GL_DITHER);
    if (prev_depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prev_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (prev_cull) glEnable(GL_CULL_FACE);
    if (prev_scissor) glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture[kColorOutput]);

    if (glGetError() != GL_NO_ERROR) return 0;
    if (!reported) {
        fprintf(stderr, "GPU orbit composition active\n");
        reported = true;
    }
    return texture[kColorOutput];
}

bool KdcOrbitGlReadback(unsigned tex, uint8_t *pixels, size_t pitch, int width, int height)
{
    if (!tex || !pixels || pitch < (size_t)width * 4) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ROW_LENGTH, (GLint)(pitch / sizeof(uint32_t)));
    glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return glGetError() == GL_NO_ERROR;
}
