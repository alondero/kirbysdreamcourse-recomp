#include "course_orbit_raster.h"

#include "gl_core_3_1.h"
#include "course_orbit_gpu_shader.h"
#include "hd_gpu.h"
#include "snes/ppu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    kColumnPixels = 32, kRowPixels = 16, kWordColumns = 1024, kFaceWords = 48,
    kMaxColumns = (kPpuBufWidth * 4 + kColumnPixels - 1) / kColumnPixels,
    kMaxBins = kMaxColumns * (240 * 4 / kRowPixels)
};
enum { kFaces, kBins, kTerrain, kPalette, kColorOutput, kDepthOutput, kTextureCount };
static GLuint program, fbo, vao, texture[kTextureCount];
static GLint max_size, columns_uniform, samples_uniform, smooth_uniform;
static int output_w, output_h, terrain_w, terrain_h;
static uint64_t terrain_revision = UINT64_MAX;
static bool unavailable, reported;
static uint32_t *faces, *bins;
static unsigned face_rows, bin_rows;
static uint32_t bin_counts[kMaxBins];

static void Bind(unsigned unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture[unit]);
}

static bool Init(void)
{
    const char extensions[] =
        "#version 330 core\n"
        "#extension GL_ARB_gpu_shader_fp64 : require\n"
        "#extension GL_ARB_gpu_shader5 : require\n";
    char source[sizeof(extensions) + sizeof(kOrbitTerrainFragment)];
    snprintf(source, sizeof(source), "%s%s",
             ogl_IsVersionGEQ(4, 0) ? "#version 400 core\n" : extensions,
             kOrbitTerrainFragment);
    program = KdcGlProgram(source);
    if (!program) return false;
    glUseProgram(program);
    const char *names[] = {"faceData", "binData", "terrain", "palette"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i)
        glUniform1i(glGetUniformLocation(program, names[i]), (GLint)i);
    columns_uniform = glGetUniformLocation(program, "columns");
    samples_uniform = glGetUniformLocation(program, "samples");
    smooth_uniform = glGetUniformLocation(program, "smoothTexture");
    glUseProgram(0);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    glGenVertexArrays(1, &vao);
    glGenFramebuffers(1, &fbo);
    glGenTextures(kTextureCount, texture);
    for (unsigned i = 0; i < kTextureCount; ++i) {
        Bind(i);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glActiveTexture(GL_TEXTURE0);
    return true;
}

static unsigned WordRows(size_t words)
{
    return (unsigned)((words + kWordColumns - 1) / kWordColumns);
}

static bool ResizeWords(uint32_t **buffer, unsigned *rows, unsigned wanted, unsigned unit)
{
    if (wanted > (unsigned)max_size || !wanted) return false;
    if (wanted <= *rows) return true;
    uint32_t *grown = realloc(*buffer, (size_t)wanted * kWordColumns * sizeof(*grown));
    if (!grown) return false;
    *buffer = grown;
    Bind(unit);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, kWordColumns, wanted, 0,
                 GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
    if (glGetError() != GL_NO_ERROR) return false;
    *rows = wanted;
    return true;
}

static void UploadWords(unsigned unit, uint32_t *words, size_t count)
{
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
    unsigned bin_count = (unsigned)(columns * rows);
    size_t face_words = (size_t)count * kFaceWords;
    if (!bin_count || bin_count > kMaxBins || !count ||
        !ResizeWords(&faces, &face_rows, WordRows(face_words), kFaces))
        return false;
    memset(bin_counts, 0, bin_count * sizeof(*bin_counts));
    for (unsigned i = 0; i < count; ++i) {
        const KdcOrbitFace *face = &geometry[i];
        PackFace(faces + i * kFaceWords, face);
        for (int y = face->top / kRowPixels; y <= face->bottom / kRowPixels; ++y)
            for (int x = face->left / kColumnPixels; x <= face->right / kColumnPixels; ++x)
                ++bin_counts[y * columns + x];
    }
    unsigned total = bin_count + 1;
    for (unsigned i = 0; i < bin_count; ++i) total += bin_counts[i];
    if (!ResizeWords(&bins, &bin_rows, WordRows(total), kBins)) return false;
    unsigned next = bin_count + 1;
    for (unsigned i = 0; i < bin_count; ++i) {
        bins[i] = next;
        next += bin_counts[i];
        bin_counts[i] = bins[i];
    }
    bins[bin_count] = next;
    /* The processor and GLSL both compare against a rounded float depth.
     * Keep original painter order when successive faces have equal depth. */
    for (unsigned i = 0; i < count; ++i) {
        const KdcOrbitFace *face = &geometry[i];
        for (int y = face->top / kRowPixels; y <= face->bottom / kRowPixels; ++y)
            for (int x = face->left / kColumnPixels; x <= face->right / kColumnPixels; ++x)
                bins[bin_counts[y * columns + x]++] = i * kFaceWords;
    }
    UploadWords(kFaces, faces, face_words);
    UploadWords(kBins, bins, total);
    return true;
}

static bool UploadTerrain(const KdcOrbitTexture *source)
{
    static const uint32_t black[128];
    const bool valid = source->texels != NULL;
    const int w = valid ? source->width : 1, h = valid ? source->height : 1;
    const uint8_t zero = 0;
    Bind(kTerrain);
    if (w != terrain_w || h != terrain_h || source->revision != terrain_revision) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, valid ? source->stride : 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, w, h, 0, GL_RED_INTEGER,
                     GL_UNSIGNED_BYTE, valid ? source->texels : &zero);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (glGetError() != GL_NO_ERROR) return false;
        terrain_w = w;
        terrain_h = h;
        terrain_revision = source->revision;
    }
    Bind(kPalette);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 128, 1, 0, GL_RED_INTEGER,
                 GL_UNSIGNED_INT, valid ? source->colors : black);
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
    const GLenum attachments[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, attachments);
    if (glGetError() != GL_NO_ERROR ||
        glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return false;
    output_w = width;
    output_h = height;
    return true;
}

bool KdcOrbitGlTerrain(const KdcOrbitFace *geometry, unsigned count,
    const KdcOrbitTexture *source, int samples, bool smooth,
    uint8_t *pixels, size_t pitch, float *depth, int width, int height)
{
    if (unavailable) return false;
    if (!program && !Init()) {
        fprintf(stderr, "GPU orbit terrain unavailable; using CPU rasterization\n");
        unavailable = true;
        return false;
    }
    const int columns = (width + kColumnPixels - 1) / kColumnPixels;
    const int rows = (height + kRowPixels - 1) / kRowPixels;
    if (source->width > max_size || source->height > max_size ||
        !UploadGeometry(geometry, count, columns, rows) || !UploadTerrain(source)) {
        glActiveTexture(GL_TEXTURE0);
        return false;
    }
    bool ready = BindOutput(width, height);
    ready = (glGetError() == GL_NO_ERROR) && ready;
    if (ready) {
        GLint previous_vao;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
        glViewport(0, 0, width, height);
        glUseProgram(program);
        glUniform1i(columns_uniform, columns);
        glUniform1i(samples_uniform, samples);
        glUniform1i(smooth_uniform, smooth);
        glBindVertexArray(vao);
        GLboolean dither = glIsEnabled(GL_DITHER);
        glDisable(GL_DITHER);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray((GLuint)previous_vao);
        if (dither) glEnable(GL_DITHER);
        /* The existing scenery, decals, sprites and HUD still use CPU depth
         * and colour. Measure this transfer in the experimental path. */
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glPixelStorei(GL_PACK_ROW_LENGTH, (GLint)(pitch / sizeof(uint32_t)));
        glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        glReadPixels(0, 0, width, height, GL_RED, GL_FLOAT, depth);
        ready = glGetError() == GL_NO_ERROR;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    if (ready && !reported) {
        fprintf(stderr, "GPU orbit terrain active\n");
        reported = true;
    }
    return ready;
}
