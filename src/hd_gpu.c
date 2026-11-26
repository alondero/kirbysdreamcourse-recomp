#include "hd_gpu.h"

#include "gl_core_3_1.h"
#include "hd_gpu_shader.h"
#include "snes/ppu.h"
#include "snes/ppu_hd_gpu.h"

#include <stdio.h>
#include <string.h>

enum {
    kRows = 240,
    kWordsWidth = 1024,
    kWordsRows = 768,
    kTileSide = 32,
    kAtlasColumns = 32,
    kAtlasSlots = 4096
};

enum { kNative, kOffsets, kWords, kPalettes, kAtlas, kOutput, kTextures };
static GLuint textures[kTextures], program, framebuffer, vao;
static GLint scale_location;
static unsigned width, height, scale, tiles, uploaded;
static uint64_t atlas_epoch;
static bool unavailable;
static bool drew_frame;
static uint32_t offsets[kPpuBufWidth * kRows], words[kWordsWidth * kWordsRows];
static uint16_t palettes[kRows][258];
static uint8_t cpu_rows[kRows];
static uint32_t atlas[kAtlasSlots][kTileSide * kTileSide];
static PpuHdGpuFrame capture;

static GLuint Compile(GLenum type, const char *text)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) return shader;
    char log[1024] = {0};
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    fprintf(stderr, "OpenGL compositor shader: %s\n", log);
    glDeleteShader(shader);
    return 0;
}

static void Bind(unsigned unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, textures[unit]);
}

static void Allocate(unsigned unit, GLenum format, GLenum type, unsigned w, unsigned h)
{
    Bind(unit);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, GL_RED_INTEGER, type, NULL);
}

unsigned KdcGlProgram(const char *fragment_source)
{
    GLuint linked = 0;
    GLuint vertex = Compile(GL_VERTEX_SHADER, kHdGpuVertex);
    GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragment_source);
    if (vertex && fragment) {
        linked = glCreateProgram();
        glAttachShader(linked, vertex);
        glAttachShader(linked, fragment);
        glLinkProgram(linked);
    }
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    if (!linked) return 0;
    GLint ok;
    glGetProgramiv(linked, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetProgramInfoLog(linked, sizeof(log), NULL, log);
        fprintf(stderr, "OpenGL compositor program: %s\n", log);
        glDeleteProgram(linked);
        return 0;
    }
    return linked;
}

static bool Init(void)
{
    GLint max_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    if (max_size < kTileSide * (kAtlasSlots / kAtlasColumns)) return false;
    program = KdcGlProgram(kHdGpuFragment);
    if (!program) return false;
    glUseProgram(program);
    const char *uniforms[] = {"nativeImage", "offsets", "records", "palettes", "atlas"};
    for (unsigned k = 0; k < sizeof(uniforms) / sizeof(*uniforms); ++k)
        glUniform1i(glGetUniformLocation(program, uniforms[k]), (GLint)k);
    scale_location = glGetUniformLocation(program, "scale");
    glGenVertexArrays(1, &vao);
    glGenFramebuffers(1, &framebuffer);
    glGenTextures(kTextures, textures);
    Allocate(kWords, GL_R32UI, GL_UNSIGNED_INT, kWordsWidth, kWordsRows);
    Allocate(kAtlas, GL_R32UI, GL_UNSIGNED_INT,
        kTileSide * kAtlasColumns, kTileSide * (kAtlasSlots / kAtlasColumns));
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    if (glGetError() == GL_NO_ERROR) return true;
    glDeleteTextures(kTextures, textures);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    program = 0;
    return false;
}

static bool AllocateOutput(unsigned w, unsigned h)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures[kOutput]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, textures[kOutput], 0);
    bool ready = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return glGetError() == GL_NO_ERROR && ready;
}

static unsigned AddTile(const uint8_t *image, bool rgba)
{
    if (tiles == kAtlasSlots) return 0;
    uint32_t *target = atlas[tiles++];
    const unsigned side = scale * 8;
    for (unsigned row = 0; row < side; ++row) {
        if (rgba) {
            memcpy(target + row * kTileSide, image + row * side * 4, side * 4);
        } else {
            for (unsigned col = 0; col < side; ++col)
                target[row * kTileSide + col] = image[row * side + col];
        }
    }
    return tiles;
}

PpuHdGpuFrame *KdcHdGpuBegin(unsigned w, unsigned h, unsigned s)
{
    if (unavailable || w > kPpuBufWidth || h > kRows || s < 2 || s > 4)
        return NULL;
    if (!program && !Init()) {
        unavailable = true;
        fprintf(stderr, "GPU HD unavailable; using CPU composition\n");
        return NULL;
    }
    const bool resized = width != w || height != h;
    if (resized) {
        Allocate(kNative, GL_R32UI, GL_UNSIGNED_INT, w, h);
        Allocate(kOffsets, GL_R32UI, GL_UNSIGNED_INT, w, h);
        Allocate(kPalettes, GL_R16UI, GL_UNSIGNED_SHORT, 258, h);
    }
    if ((resized || scale != s) && !AllocateOutput(w * s, h * s)) {
        unavailable = true;
        return NULL;
    }
    width = w;
    height = h;
    if (scale != s || tiles == kAtlasSlots) {
        tiles = uploaded = 0;
        ++atlas_epoch;
        scale = s;
    }
    memset(offsets, 0, (size_t)w * h * sizeof(*offsets));
    memset(cpu_rows, 0, h);
    capture = (PpuHdGpuFrame){
        .width = w, .height = h, .scale = s, .offsets = offsets, .words = words,
        .length = 1, .capacity = sizeof(words) / sizeof(*words), .palette = palettes,
        .cpu_rows = cpu_rows, .atlas_epoch = atlas_epoch, .add_tile = AddTile
    };
    return &capture;
}

unsigned KdcHdGpuDraw(const PpuHdGpuFrame *frame, const uint8_t *native,
                      const uint32_t *cpu)
{
    if (!drew_frame) {
        fprintf(stderr, "GPU HD composition active\n");
        drew_frame = true;
    }
    Bind(kNative);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED_INTEGER, GL_UNSIGNED_INT, native);
    Bind(kOffsets);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED_INTEGER, GL_UNSIGNED_INT, offsets);
    Bind(kWords);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kWordsWidth,
        (GLsizei)((frame->length + kWordsWidth - 1) / kWordsWidth),
        GL_RED_INTEGER, GL_UNSIGNED_INT, words);
    Bind(kPalettes);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 258, height, GL_RED_INTEGER, GL_UNSIGNED_SHORT, palettes);
    Bind(kAtlas);
    for (; uploaded < tiles; ++uploaded) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, (uploaded % kAtlasColumns) * kTileSide,
            (uploaded / kAtlasColumns) * kTileSide, kTileSide, kTileSide,
            GL_RED_INTEGER, GL_UNSIGNED_INT, atlas[uploaded]);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width * scale, height * scale);
    glUseProgram(program);
    glUniform1i(scale_location, (GLint)scale);
    GLint previous_vao;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
    glBindVertexArray(vao);
    GLboolean dither = glIsEnabled(GL_DITHER);
    glDisable(GL_DITHER);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray((GLuint)previous_vao);
    if (dither) glEnable(GL_DITHER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures[kOutput]);
    for (unsigned y = 0; y < height;) {
        if (!frame->cpu_rows[y]) { ++y; continue; }
        unsigned first = y++;
        while (y < height && frame->cpu_rows[y]) ++y;
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, first * scale, width * scale, (y - first) * scale,
            GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, cpu + (size_t)first * width * scale * scale);
    }
    return textures[kOutput];
}
