#include "course_terrain.h"
#include "course_view.h"
#include "hd_image.h"
#include "hd_scaler.h"
#include "host_parallel.h"
#include "snes/ppu.h"

#include <stdlib.h>
#include <string.h>

enum {
    kRows = 1024,          /* rows of the three row tables at $8BB0/$93B0/$9BB0 */
    kMapBytes = 2 * 0x6800, /* both planes' packed tile words at $13000 */
    kCharWords = 0x4000,   /* 1024 4bpp characters from VRAM word 0 */
    kMaxTiles = 1 << 16,   /* refuse implausible maps instead of allocating */
    kStrip = 4,            /* character rows enlarged per parallel piece */
    kReach = (kKdcHdImageReach + 7) / 8,
    /* Updates a palette's luma must hold still before the enlargement's edge
     * decisions follow it. Colours always come from the live palette; only
     * where edges are drawn waits, so cycling animations cost nothing while
     * a fade or a newly loaded palette is caught up once it settles. */
    kSettle = 30,
};

_Static_assert((int)kKdcTerrainScale == (int)kKdcHdImageScale, "one enlargement factor");

static struct {
    KdcTerrain view;
    uint8_t *native, *mask, *hd, *dirty, *reach;
    uint16_t *words;       /* both planes' tile word for every grid cell */
    int columns, rows;     /* grid size in characters, including the border */
    int first_column, first_row;
    bool valid, hd_valid;
    uint8_t tables[3 * 2 * kRows];
    uint8_t map[kMapBytes];
    uint16_t chars[kCharWords];
    uint16_t cgram[128];
    float luma[256];       /* the luma the enlargement was made with */
    float seen[128];       /* the luma of the previous update */
    uint8_t steady[8];     /* updates each palette's luma has held still */
    KdcTerrainStats stats;
} state;

static uint32_t Color(unsigned rgb15)
{
    unsigned r = rgb15 & 31, g = (rgb15 >> 5) & 31, b = (rgb15 >> 10) & 31;
    return 0xff000000u | ((r << 3 | r >> 2) << 16) |
           ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
}

enum { kSceneryFrames = 4 };
typedef struct SceneryFrame {
    uint8_t native[256 * 256];
    float luma[32];
    uint8_t *hd;
    bool valid;
} SceneryFrame;

static struct {
    KdcScenery view;
    uint8_t native[256 * 256];
    uint8_t *hd;
    bool valid, hd_valid;
    uint8_t map[2 * 1024];
    uint16_t chars[0x2000];
    float luma[256];   /* the luma the enlargement was made with */
    float seen[32];    /* the luma of the previous update */
    uint8_t steady;    /* updates the luma has held still (kSettle) */
    SceneryFrame frames[kSceneryFrames];
    unsigned next_frame;
} scenery;

static void FreeSceneryFrames(void)
{
    for (unsigned i = 0; i < kSceneryFrames; ++i) {
        free(scenery.frames[i].hd);
        scenery.frames[i].hd = NULL;
        scenery.frames[i].valid = false;
    }
    scenery.next_frame = 0;
    scenery.hd = NULL;
    scenery.view.hd = NULL;
    scenery.hd_valid = false;
}

void KdcTerrainReset(void)
{
    FreeSceneryFrames();
    scenery.valid = scenery.hd_valid = false;
    free(state.native);
    free(state.mask);
    free(state.hd);
    free(state.dirty);
    free(state.reach);
    free(state.words);
    state.native = state.mask = state.hd = state.dirty = state.reach = NULL;
    state.words = NULL;
    state.valid = state.hd_valid = false;
}

void KdcTerrainGetStats(KdcTerrainStats *stats)
{
    if (stats) *stats = state.stats;
}

static void ReadTables(const uint8_t *ram, uint8_t *tables)
{
    memcpy(tables, ram + 0x8bb0, 2 * kRows);
    memcpy(tables + 2 * kRows, ram + 0x93b0, 2 * kRows);
    memcpy(tables + 4 * kRows, ram + 0x9bb0, 2 * kRows);
}

/* Size the grid to the rows the game's own lookup accepts (KdcCourseTile). */
static bool Layout(const uint8_t *ram)
{
    int left = 1 << 30, right = -1, top = -1, bottom = -1;
    for (int row = 0; row < kRows; ++row) {
        int first = KdcReadWord(ram, 0x93b0 + 2 * row), last = KdcReadWord(ram, 0x9bb0 + 2 * row);
        if (first >= last) continue;
        if (top < 0) top = row;
        bottom = row;
        if (first < left) left = first;
        if (last > right) right = last;
    }
    if (top < 0) return false;
    const int columns = right - left + 3, rows = bottom - top + 3;
    if (columns > kMaxTiles || rows > kMaxTiles || (long long)columns * rows > kMaxTiles)
        return false;
    KdcTerrainReset();
    const size_t tiles = (size_t)columns * rows;
    state.native = calloc(tiles, 64);
    state.mask = calloc(tiles, 64);
    state.dirty = calloc(tiles, 1);
    state.reach = calloc(tiles, 1);
    state.words = calloc(tiles, 2 * sizeof(*state.words));
    if (!state.native || !state.mask || !state.dirty || !state.reach || !state.words) {
        KdcTerrainReset();
        return false;
    }
    state.columns = columns;
    state.rows = rows;
    state.first_column = left - 1;
    state.first_row = top - 1;
    state.view.left = state.first_column * 8;
    state.view.top = state.first_row * 8;
    state.view.width = columns * 8;
    state.view.height = rows * 8;
    state.view.native = state.native;
    state.view.mask = state.mask;
    state.view.hd = NULL;
    return true;
}

typedef struct Compose {
    const uint8_t *ram;
    const uint16_t *vram;
    const uint8_t *char_dirty; /* NULL when no character changed */
    unsigned luma_dirty;       /* palettes whose luma changed */
    bool all;
} Compose;

/* One character row of the grid: decode cells whose inputs changed. */
static void ComposeRow(void *context, unsigned index)
{
    const Compose *job = context;
    const int row = (int)index;
    for (int column = 0; column < state.columns; ++column) {
        const size_t cell = (size_t)row * state.columns + column;
        uint16_t words[2];
        bool changed = job->all;
        for (unsigned layer = 0; layer < 2; ++layer) {
            words[layer] = KdcCourseTile(job->ram, layer, state.first_column + column,
                                         state.first_row + row);
            changed |= words[layer] != state.words[cell * 2 + layer] ||
                       (job->char_dirty && job->char_dirty[words[layer] & 1023]);
        }
        if (!changed) {
            if (job->luma_dirty & (1u << ((words[0] >> 10) & 7) | 1u << ((words[1] >> 10) & 7)))
                state.dirty[cell] = 1;
            continue;
        }
        state.words[cell * 2] = words[0];
        state.words[cell * 2 + 1] = words[1];
        state.dirty[cell] = 2;
        /* BG1 is in front unless only BG2's character has priority. */
        const bool second_first = (words[1] & 0x2000) && !(words[0] & 0x2000);
        for (int y = 0; y < 8; ++y) {
            uint8_t pixels[2][8];
            for (unsigned layer = 0; layer < 2; ++layer) {
                const unsigned word = words[layer];
                const unsigned at = (word & 1023) * 16 + (word & 0x8000 ? 7 - y : y);
                const unsigned low = job->vram[at], high = job->vram[at + 8];
                for (int x = 0; x < 8; ++x) {
                    const int bit = word & 0x4000 ? x : 7 - x;
                    pixels[layer][x] = (uint8_t)((low >> bit & 1) | (low >> (bit + 7) & 2) |
                                                 (high >> bit & 1) << 2 | (high >> (bit + 7) & 2) << 2);
                }
            }
            uint8_t *native = state.native + ((size_t)row * 8 + y) * state.view.width + column * 8;
            uint8_t *mask = state.mask + ((size_t)row * 8 + y) * state.view.width + column * 8;
            for (int x = 0; x < 8; ++x) {
                unsigned layer = pixels[0][x] ? 0 : 1;
                if (pixels[1][x] && second_first) layer = 1;
                const unsigned pixel = pixels[layer][x];
                native[x] = pixel ? (uint8_t)(((words[layer] >> 10) & 7) * 16 + pixel) : 0;
                mask[x] = (uint8_t)((pixels[0][x] || pixels[1][x] ? kKdcTerrainOpaque : 0) |
                    ((pixels[0][x] && (words[0] & 0x2000)) || (pixels[1][x] && (words[1] & 0x2000)) ?
                     kKdcTerrainPriority : 0));
            }
        }
    }
}

typedef struct Enlarge {
    KdcHdImage image;
    const uint8_t *marked; /* per grid cell, or NULL for every cell */
} Enlarge;

/* A strip of character rows: enlarge each run of marked columns. */
static void EnlargeStrip(void *context, unsigned index)
{
    const Enlarge *job = context;
    const int r0 = (int)index * kStrip;
    const int r1 = r0 + kStrip < state.rows ? r0 + kStrip : state.rows;
    for (int column = 0; column < state.columns;) {
        bool marked = !job->marked;
        for (int row = r0; row < r1 && !marked; ++row)
            marked = job->marked[(size_t)row * state.columns + column];
        if (!marked) { ++column; continue; }
        int end = column + 1;
        for (; end < state.columns; ++end) {
            marked = !job->marked;
            for (int row = r0; row < r1 && !marked; ++row)
                marked = job->marked[(size_t)row * state.columns + end];
            if (!marked) break;
        }
        (void)KdcHdImageUpdate(&job->image, column * 8, r0 * 8, end * 8, r1 * 8);
        column = end;
    }
}

static void EnlargeMarked(const uint8_t *marked)
{
    Enlarge job = {
        .image = {.src = state.native, .width = state.view.width, .height = state.view.height,
                  .src_stride = state.view.width, .dst = state.hd,
                  .dst_stride = state.view.width * kKdcHdImageScale, .luma = state.luma},
        .marked = marked,
    };
    KdcParallelFor((unsigned)((state.rows + kStrip - 1) / kStrip), EnlargeStrip, &job);
}

const KdcTerrain *KdcTerrainUpdate(const struct Ppu *ppu, const uint8_t *ram, bool hd)
{
    if (!ppu || !ram) return NULL;
    const uint16_t *vram = PpuRenderVram(ppu);
    uint8_t tables[sizeof(state.tables)];
    ReadTables(ram, tables);
    bool all = !state.valid || memcmp(tables, state.tables, sizeof(tables));
    if (all) {
        state.valid = false;
        if (!Layout(ram)) return NULL;
        memcpy(state.tables, tables, sizeof(tables));
        ++state.stats.rebuilds;
    }
    /* Colours are applied when drawing; only edge decisions use their luma. */
    float luma[128] = {0};
    luma[0] = -1;
    unsigned moved = 0;
    for (unsigned i = 1; i < 128; ++i) {
        state.view.colors[i] = Color(ppu->cgram[i]);
        luma[i] = i & 15 ? KdcXbrLuma555(ppu->cgram[i]) : -1;
        if (luma[i] != state.seen[i]) moved |= 1u << (i >> 4);
    }
    state.view.colors[0] = 0;
    memcpy(state.seen, luma, sizeof(luma));
    const bool enlarge_all = hd && !state.hd_valid;
    unsigned luma_dirty = 0;
    for (unsigned p = 0; p < 8; ++p) {
        if (moved & (1u << p)) state.steady[p] = 0;
        else if (state.steady[p] < kSettle) ++state.steady[p];
        if (enlarge_all || all) continue;
        if (state.steady[p] >= kSettle &&
            memcmp(state.luma + p * 16, luma + p * 16, 16 * sizeof(*luma))) luma_dirty |= 1u << p;
    }
    /* A complete enlargement uses the current luma; otherwise only settled
     * palettes move on. */
    for (unsigned p = 0; p < 8; ++p)
        if (enlarge_all || all || (luma_dirty & (1u << p)))
            memcpy(state.luma + p * 16, luma + p * 16, 16 * sizeof(*luma));
    const bool chars_changed = all || memcmp(vram, state.chars, sizeof(state.chars));
    const bool map_changed = all || memcmp(ram + 0x13000, state.map, kMapBytes);
    if (!hd && state.hd_valid) {
        free(state.hd);
        state.hd = NULL;
        state.view.hd = NULL;
        state.hd_valid = false;
    }
    if (!hd) luma_dirty = 0;
    if (!chars_changed && !map_changed && !luma_dirty && !enlarge_all) return &state.view;

    uint8_t char_dirty[1024];
    if (chars_changed && !all)
        for (unsigned i = 0; i < 1024; ++i)
            char_dirty[i] = memcmp(vram + i * 16, state.chars + i * 16, 16 * sizeof(*vram)) != 0;
    Compose compose = {.ram = ram, .vram = vram, .luma_dirty = luma_dirty, .all = all,
                       .char_dirty = chars_changed && !all ? char_dirty : NULL};
    if (chars_changed || map_changed || luma_dirty)
        KdcParallelFor((unsigned)state.rows, ComposeRow, &compose);
    memcpy(state.chars, vram, sizeof(state.chars));
    memcpy(state.map, ram + 0x13000, kMapBytes);
    state.valid = true;

    /* Enlarge what changed together with everything a change can reach. */
    const size_t cells = (size_t)state.columns * state.rows;
    bool any = false;
    memset(state.reach, 0, cells);
    for (int row = 0; row < state.rows; ++row)
        for (int column = 0; column < state.columns; ++column) {
            uint8_t *dirty = &state.dirty[(size_t)row * state.columns + column];
            if (!*dirty) continue;
            state.stats.composed_tiles += *dirty == 2;
            *dirty = 0;
            any = true;
            if (!hd || enlarge_all) continue;
            const int c0 = column > kReach ? column - kReach : 0;
            const int c1 = column + kReach < state.columns - 1 ? column + kReach : state.columns - 1;
            const int r0 = row > kReach ? row - kReach : 0;
            const int r1 = row + kReach < state.rows - 1 ? row + kReach : state.rows - 1;
            for (int r = r0; r <= r1; ++r)
                memset(state.reach + (size_t)r * state.columns + c0, 1, (size_t)(c1 - c0 + 1));
        }
    if (!hd) return &state.view;
    if (enlarge_all) {
        state.hd = calloc((size_t)state.view.width * state.view.height,
                          kKdcHdImageScale * kKdcHdImageScale);
        if (!state.hd) return &state.view; /* drawn from the source image */
        state.hd_valid = true;
        state.view.hd = state.hd;
        EnlargeMarked(NULL);
        state.stats.scaled_tiles += cells;
    } else if (any) {
        EnlargeMarked(state.reach);
        for (size_t i = 0; i < cells; ++i) state.stats.scaled_tiles += state.reach[i];
    }
    return &state.view;
}

typedef struct SceneryJob {
    KdcHdImage image;
    int x0, y0, x1, y1; /* source rectangle, exclusive ends */
} SceneryJob;

/* One tile of the rectangle. Textured backdrops spend most of their time in
 * a few shapes, so half-width tiles balance the workers far better than
 * whole rows; smaller tiles analyse too many shapes twice. */
enum { kSceneryTileWidth = 128, kSceneryTileHeight = 32 };

static unsigned SceneryColumns(const SceneryJob *job)
{
    return (unsigned)((job->x1 - job->x0 + kSceneryTileWidth - 1) / kSceneryTileWidth);
}

static void EnlargeScenery(void *context, unsigned index)
{
    const SceneryJob *job = context;
    const unsigned columns = SceneryColumns(job);
    const int x0 = job->x0 + (int)(index % columns) * kSceneryTileWidth;
    const int y0 = job->y0 + (int)(index / columns) * kSceneryTileHeight;
    const int x1 = x0 + kSceneryTileWidth < job->x1 ? x0 + kSceneryTileWidth : job->x1;
    const int y1 = y0 + kSceneryTileHeight < job->y1 ? y0 + kSceneryTileHeight : job->y1;
    (void)KdcHdImageUpdate(&job->image, x0, y0, x1, y1);
}

const KdcScenery *KdcSceneryUpdate(const struct Ppu *ppu, const uint8_t *ram, bool hd)
{
    if (!ppu || !ram) return NULL;
    const uint16_t *vram = PpuRenderVram(ppu);
    float luma[32] = {0};
    luma[0] = -1;
    bool moved = false;
    for (unsigned i = 1; i < 32; ++i) {
        scenery.view.colors[i] = Color(ppu->cgram[i]);
        luma[i] = i & 3 ? KdcXbrLuma555(ppu->cgram[i]) : -1;
        moved |= luma[i] != scenery.seen[i];
    }
    scenery.view.colors[0] = 0;
    memcpy(scenery.seen, luma, sizeof(luma));
    if (moved) scenery.steady = 0;
    else if (scenery.steady < kSettle) ++scenery.steady;
    const bool luma_changed = scenery.steady >= kSettle &&
        memcmp(scenery.luma, luma, sizeof(luma)) != 0;
    scenery.view.native = scenery.native;
    if (!hd && scenery.hd_valid) {
        FreeSceneryFrames();
    }
    scenery.view.hd = scenery.hd;
    /* Only cells whose map word or character changed are redrawn: the HUD
     * streams its own BG3 characters during play, which the clean scenery
     * map does not use. */
    const bool full = !scenery.valid;
    int c0 = 32, r0 = 32, c1 = -1, r1 = -1; /* changed cells, inclusive */
    for (unsigned cell = 0; cell < 1024; ++cell) {
        const unsigned word = KdcReadWord(ram, 0xdadc + 2 * cell);
        const unsigned at = (word & 1023) * 8;
        if (!full && word == KdcReadWord(scenery.map, 2 * cell) &&
            !memcmp(vram + 0x4000 + at, scenery.chars + at, 8 * sizeof(*vram))) continue;
        const int column = (int)(cell % 32), row = (int)(cell / 32);
        if (column < c0) c0 = column;
        if (column > c1) c1 = column;
        if (row < r0) r0 = row;
        if (row > r1) r1 = row;
        for (int y = 0; y < 8; ++y) {
            const unsigned planes = vram[0x4000 + at + (word & 0x8000 ? 7 - y : y)];
            uint8_t *out = scenery.native + (row * 8 + y) * 256 + column * 8;
            for (int x = 0; x < 8; ++x) {
                const int bit = word & 0x4000 ? x : 7 - x;
                const unsigned pixel = (planes >> bit & 1) | (planes >> (bit + 7) & 2);
                out[x] = pixel ? (uint8_t)(((word >> 10) & 7) * 4 + pixel) : 0;
            }
        }
    }
    memcpy(scenery.map, ram + 0xdadc, sizeof(scenery.map));
    memcpy(scenery.chars, vram + 0x4000, sizeof(scenery.chars));
    scenery.valid = true;
    const bool changed = c1 >= 0;
    const bool enlarge_all = hd && (!scenery.hd_valid || luma_changed);
    if (!changed && !enlarge_all) return &scenery.view;
    ++state.stats.scenery_builds;
    if (luma_changed || !scenery.hd_valid) memcpy(scenery.luma, luma, sizeof(luma));
    if (!hd) return &scenery.view;
    /* Backdrops often loop through a few animation frames. Compare the
     * complete decoded image and the effective (settled) luma, not VRAM
     * addresses or a hash alone. Live colours are still applied by the draw.
     * Retaining four index images costs at most 4 MiB plus their keys. */
    for (unsigned i = 0; i < kSceneryFrames; ++i) {
        const SceneryFrame *frame = &scenery.frames[i];
        if (frame->valid && !memcmp(frame->luma, scenery.luma, sizeof(frame->luma)) &&
            !memcmp(frame->native, scenery.native, sizeof(frame->native))) {
            scenery.view.hd = scenery.hd = frame->hd;
            scenery.hd_valid = true;
            ++state.stats.scenery_cache_hits;
            return &scenery.view;
        }
    }
    SceneryFrame *frame = &scenery.frames[scenery.next_frame];
    if (!frame->hd) frame->hd = malloc(1024 * 1024);
    if (!frame->hd) {
        /* Allocation failure must never expose the previous frame's art. */
        FreeSceneryFrames();
        return &scenery.view;
    }
    /* Partial updates are relative to the image shown last, which may be a
     * different cache entry. Seed the replacement before changing its area. */
    if (!enlarge_all && frame->hd != scenery.hd)
        memcpy(frame->hd, scenery.hd, 1024 * 1024);
    frame->valid = false;
    scenery.hd = frame->hd;
    SceneryJob job = {
        .image = {.src = scenery.native, .width = 256, .height = 256, .src_stride = 256,
                  .dst = scenery.hd, .dst_stride = 1024, .luma = scenery.luma},
        .x0 = 0, .y0 = 0, .x1 = 256, .y1 = 256,
    };
    if (!enlarge_all) {
        /* Everything a change can reach (hd_image.h). */
        job.x0 = c0 * 8 - kKdcHdImageReach;
        job.y0 = r0 * 8 - kKdcHdImageReach;
        job.x1 = c1 * 8 + 8 + kKdcHdImageReach;
        job.y1 = r1 * 8 + 8 + kKdcHdImageReach;
        if (job.x0 < 0) job.x0 = 0;
        if (job.y0 < 0) job.y0 = 0;
        if (job.x1 > 256) job.x1 = 256;
        if (job.y1 > 256) job.y1 = 256;
    }
    const unsigned rows = (unsigned)((job.y1 - job.y0 + kSceneryTileHeight - 1) / kSceneryTileHeight);
    ++state.stats.scenery_enlargements;
    KdcParallelFor(rows * SceneryColumns(&job), EnlargeScenery, &job);
    memcpy(frame->native, scenery.native, sizeof(frame->native));
    memcpy(frame->luma, scenery.luma, sizeof(frame->luma));
    frame->valid = true;
    scenery.next_frame = (scenery.next_frame + 1) % kSceneryFrames;
    scenery.hd_valid = true;
    scenery.view.hd = scenery.hd;
    return &scenery.view;
}
