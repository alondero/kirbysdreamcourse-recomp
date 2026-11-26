#include "course_terrain.h"
#include "course_view.h"
#include "hd_image.h"
#include "hd_scaler.h"
#include "host_parallel.h"
#include "snes/ppu.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#ifndef KDC_TERRAIN_BLOCK_CACHE_LIMIT
#ifdef __ANDROID__
#define KDC_TERRAIN_BLOCK_CACHE_LIMIT (16u * 1024u * 1024u)
#else
#define KDC_TERRAIN_BLOCK_CACHE_LIMIT (48u * 1024u * 1024u)
#endif
#endif

#ifndef KDC_TERRAIN_PROGRESSIVE_PIXELS
#define KDC_TERRAIN_PROGRESSIVE_PIXELS (128 * 1024)
#endif

enum {
    kRows = 1024,          /* rows of the three row tables at $8BB0/$93B0/$9BB0 */
    kMapBytes = 2 * 0x6800, /* both planes' packed tile words at $13000 */
    kCharWords = 0x4000,   /* 1024 4bpp characters from VRAM word 0 */
    kMaxTiles = 1 << 16,   /* refuse implausible maps instead of allocating */
    kStrip = 4,            /* character rows enlarged per parallel piece */
    kBlockCols = 8,        /* character columns per cached animated terrain block */
    kTerrainBlockSlots = 16,
    kMaxTerrainBlockCacheBytes = KDC_TERRAIN_BLOCK_CACHE_LIMIT,
    kReach = (kKdcHdImageReach + 7) / 8,
    kProgressiveSourcePixels = KDC_TERRAIN_PROGRESSIVE_PIXELS,
    /* Updates a palette's luma must hold still before the enlargement's edge
     * decisions follow it. Colours always come from the live palette; only
     * where edges are drawn waits, so cycling animations cost nothing while
     * a fade or a newly loaded palette is caught up once it settles. */
    kSettle = 30,
};

_Static_assert((int)kKdcTerrainScale == (int)kKdcHdImageScale, "one enlargement factor");

typedef struct TerrainBlockEntry {
    uint64_t hash;
    uint64_t last_use;
    float luma[128];
    uint8_t *padded;
    uint8_t *hd;
    size_t padded_bytes, hd_bytes;
    bool valid;
} TerrainBlockEntry;

typedef struct TerrainBlockCache {
    TerrainBlockEntry entries[kTerrainBlockSlots];
    uint64_t clock;
} TerrainBlockCache;

static struct {
    KdcTerrain view;
    uint8_t *native, *mask, *hd, *dirty, *reach;
    uint16_t *words;       /* both planes' tile word for every grid cell */
    int columns, rows;     /* grid size in characters, including the border */
    int first_column, first_row;
    bool valid, hd_valid, prewarm_active;
    unsigned prewarm_next_strip;
    uint8_t tables[3 * 2 * kRows];
    uint8_t map[kMapBytes];
    uint16_t chars[kCharWords];
    uint16_t cgram[128];
    float luma[256];       /* the luma the enlargement was made with */
    float seen[128];       /* the luma of the previous update */
    uint8_t steady[8];     /* updates each palette's luma has held still */
    TerrainBlockCache *block_cache;
    unsigned block_strips, block_cols;
    _Atomic size_t block_cache_bytes;
    KdcTerrainStats stats;
} state;

static uint64_t HashBytes(uint64_t hash, const void *data, size_t bytes)
{
    const uint8_t *p = data;
    size_t i = 0;
    for (; i + 8 <= bytes; i += 8) {
        uint64_t w;
        memcpy(&w, p + i, sizeof(w));
        hash ^= w;
        hash *= UINT64_C(0x100000001b3);
        hash ^= hash >> 32;
    }
    for (; i < bytes; ++i) {
        hash ^= p[i];
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

static void FreeTerrainBlockCache(void)
{
    if (state.block_cache) {
        const size_t total = (size_t)state.block_strips * state.block_cols;
        for (size_t i = 0; i < total; ++i) {
            for (unsigned s = 0; s < kTerrainBlockSlots; ++s) {
                free(state.block_cache[i].entries[s].padded);
                free(state.block_cache[i].entries[s].hd);
            }
        }
        free(state.block_cache);
        state.block_cache = NULL;
    }
    state.block_strips = state.block_cols = 0;
    atomic_store_explicit(&state.block_cache_bytes, 0, memory_order_relaxed);
}

static uint32_t Color(unsigned rgb15)
{
    unsigned r = rgb15 & 31, g = (rgb15 >> 5) & 31, b = (rgb15 >> 10) & 31;
    return 0xff000000u | ((r << 3 | r >> 2) << 16) |
           ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
}

enum {
#ifdef __ANDROID__
    kSceneryFrames = 16,
    kMaxSceneryCacheBytes = 16u * 1024u * 1024u,
#else
    kSceneryFrames = 48,
    kMaxSceneryCacheBytes = 48u * 1024u * 1024u,
#endif
    kSceneryNativeBytes = 256u * 256u,
    kSceneryHdBytes = 1024u * 1024u,
    kSceneryFrameBytes = kSceneryNativeBytes + kSceneryHdBytes,
};
typedef struct SceneryFrame {
    uint64_t hash;
    uint64_t last_use;
    float luma[32];
    uint8_t *native;
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
    size_t cache_bytes;
    uint64_t use_clock;
} scenery;

static void FreeSceneryFrames(void)
{
    for (unsigned i = 0; i < kSceneryFrames; ++i) {
        free(scenery.frames[i].native);
        free(scenery.frames[i].hd);
        scenery.frames[i].native = NULL;
        scenery.frames[i].hd = NULL;
        scenery.frames[i].valid = false;
        scenery.frames[i].last_use = 0;
    }
    scenery.cache_bytes = 0;
    scenery.use_clock = 0;
    scenery.hd = NULL;
    scenery.view.hd = NULL;
    scenery.hd_valid = false;
}

void KdcTerrainReset(void)
{
    FreeSceneryFrames();
    FreeTerrainBlockCache();
    scenery.valid = scenery.hd_valid = false;
    free(state.native);
    free(state.mask);
    free(state.hd);
    free(state.dirty);
    free(state.reach);
    free(state.words);
    state.native = state.mask = state.hd = state.dirty = state.reach = NULL;
    state.words = NULL;
    state.valid = state.hd_valid = state.prewarm_active = false;
    state.prewarm_next_strip = 0;
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
    bool use_block_cache;
    unsigned first_strip;
    _Atomic uint64_t scaled_tiles;
} Enlarge;

/* Copy a padded source neighbourhood for [bx0,bx1) x [by0,by1) into dst. */
static size_t PackPaddedBlock(const KdcHdImage *image, int bx0, int by0, int bx1, int by1,
                              uint8_t *dst)
{
    const int wx0 = bx0 - kKdcHdImageReach > 0 ? bx0 - kKdcHdImageReach : 0;
    const int wy0 = by0 - kKdcHdImageReach > 0 ? by0 - kKdcHdImageReach : 0;
    const int wx1 = bx1 + kKdcHdImageReach < image->width ? bx1 + kKdcHdImageReach : image->width;
    const int wy1 = by1 + kKdcHdImageReach < image->height ? by1 + kKdcHdImageReach : image->height;
    const size_t row_bytes = (size_t)(wx1 - wx0);
    size_t offset = 0;
    for (int y = wy0; y < wy1; ++y) {
        memcpy(dst + offset, image->src + (size_t)y * image->src_stride + wx0, row_bytes);
        offset += row_bytes;
    }
    return offset;
}

/* Reserve a replacement's net bytes before allocating it. Each cache entry is
 * owned by one strip worker, but the shared ceiling is not: without making the
 * check and reservation one atomic step, parallel strips can all pass the check
 * before any of them records its allocation. */
static bool ReserveTerrainBlockBytes(size_t old_bytes, size_t new_bytes)
{
    size_t cur = atomic_load_explicit(&state.block_cache_bytes, memory_order_relaxed);
    for (;;) {
        if (cur < old_bytes || cur - old_bytes + new_bytes > kMaxTerrainBlockCacheBytes)
            return false;
        const size_t next = cur - old_bytes + new_bytes;
        if (atomic_compare_exchange_weak_explicit(&state.block_cache_bytes, &cur, next,
                                                  memory_order_relaxed, memory_order_relaxed))
            return true;
    }
}

static void ReleaseTerrainBlockBytes(size_t old_bytes, size_t new_bytes)
{
    if (new_bytes >= old_bytes)
        atomic_fetch_sub_explicit(&state.block_cache_bytes, new_bytes - old_bytes,
                                  memory_order_relaxed);
    else
        atomic_fetch_add_explicit(&state.block_cache_bytes, old_bytes - new_bytes,
                                  memory_order_relaxed);
}

static bool EnlargeBlockCached(const Enlarge *job, unsigned strip_index, unsigned bcol,
                               int c0, int c1, int r0, int r1)
{
    const KdcHdImage *image = &job->image;
    const int bx0 = c0 * 8, by0 = r0 * 8, bx1 = c1 * 8, by1 = r1 * 8;
    const size_t hd_row_bytes = (size_t)(bx1 - bx0) * kKdcHdImageScale;
    const int hd_rows = (by1 - by0) * kKdcHdImageScale;
    const size_t hd_bytes = hd_row_bytes * (size_t)hd_rows;
    enum { kMaxPadBytes = (kBlockCols * 8 + 2 * kKdcHdImageReach) * (kStrip * 8 + 2 * kKdcHdImageReach) };
    uint8_t padded[kMaxPadBytes];
    const size_t padded_bytes = PackPaddedBlock(image, bx0, by0, bx1, by1, padded);
    uint64_t hash = HashBytes(UINT64_C(0xcbf29ce484222325), state.luma, 128 * sizeof(*state.luma));
    hash = HashBytes(hash, padded, padded_bytes);

    TerrainBlockCache *cache = &state.block_cache[(size_t)strip_index * state.block_cols + bcol];
    ++cache->clock;
    TerrainBlockEntry *victim = NULL;
    for (unsigned i = 0; i < kTerrainBlockSlots; ++i) {
        TerrainBlockEntry *e = &cache->entries[i];
        if (e->valid && e->hash == hash && e->padded_bytes == padded_bytes &&
            e->hd_bytes == hd_bytes &&
            !memcmp(e->luma, state.luma, sizeof(e->luma)) &&
            !memcmp(e->padded, padded, padded_bytes)) {
            e->last_use = cache->clock;
            for (int y = 0; y < hd_rows; ++y) {
                memcpy(image->dst + (size_t)(by0 * kKdcHdImageScale + y) * image->dst_stride +
                           (size_t)bx0 * kKdcHdImageScale,
                       e->hd + (size_t)y * hd_row_bytes, hd_row_bytes);
            }
            return true;
        }
        if (!victim || (!e->valid && victim->valid) ||
            (e->valid && victim->valid && e->last_use < victim->last_use))
            victim = e;
    }

    (void)KdcHdImageUpdate(image, bx0, by0, bx1, by1);
    if (victim) {
        const bool need_alloc = victim->padded_bytes != padded_bytes || victim->hd_bytes != hd_bytes ||
                                !victim->padded || !victim->hd;
        if (need_alloc) {
            const size_t old_bytes = (victim->padded ? victim->padded_bytes : 0) +
                                     (victim->hd ? victim->hd_bytes : 0);
            const size_t new_bytes = padded_bytes + hd_bytes;
            if (!ReserveTerrainBlockBytes(old_bytes, new_bytes)) return false;
            uint8_t *np = malloc(padded_bytes);
            uint8_t *nh = malloc(hd_bytes);
            if (np && nh) {
                free(victim->padded);
                free(victim->hd);
                victim->padded = np;
                victim->hd = nh;
                victim->padded_bytes = padded_bytes;
                victim->hd_bytes = hd_bytes;
            } else {
                free(np);
                free(nh);
                ReleaseTerrainBlockBytes(old_bytes, new_bytes);
                return false;
            }
        }
        memcpy(victim->luma, state.luma, sizeof(victim->luma));
        memcpy(victim->padded, padded, padded_bytes);
        for (int y = 0; y < hd_rows; ++y) {
            memcpy(victim->hd + (size_t)y * hd_row_bytes,
                   image->dst + (size_t)(by0 * kKdcHdImageScale + y) * image->dst_stride +
                       (size_t)bx0 * kKdcHdImageScale,
                   hd_row_bytes);
        }
        victim->hash = hash;
        victim->last_use = cache->clock;
        victim->valid = true;
    }
    return false;
}

/* A strip of character rows: enlarge each run of marked columns. */
static void EnlargeStrip(void *context, unsigned index)
{
    Enlarge *job = context;
    const int r0 = (int)(job->first_strip + index) * kStrip;
    const int r1 = r0 + kStrip < state.rows ? r0 + kStrip : state.rows;
    if (job->use_block_cache && state.block_cache) {
        uint64_t missed_tiles = 0;
        for (unsigned bcol = 0; bcol < state.block_cols; ++bcol) {
            const int c0 = (int)bcol * kBlockCols;
            const int c1 = c0 + kBlockCols < state.columns ? c0 + kBlockCols : state.columns;
            uint64_t marked_count = 0;
            for (int row = r0; row < r1; ++row)
                for (int col = c0; col < c1; ++col)
                    marked_count += job->marked[(size_t)row * state.columns + col] != 0;
            if (!marked_count) continue;
            if (!EnlargeBlockCached(job, job->first_strip + index, bcol, c0, c1, r0, r1))
                missed_tiles += marked_count;
        }
        if (missed_tiles)
            atomic_fetch_add_explicit(&job->scaled_tiles, missed_tiles, memory_order_relaxed);
        return;
    }
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

static uint64_t EnlargeMarkedRange(const uint8_t *marked, unsigned first_strip, unsigned strip_count)
{
    const unsigned strips = (unsigned)((state.rows + kStrip - 1) / kStrip);
    const bool use_cache = marked != NULL;
    if (use_cache && !state.block_cache) {
        const unsigned bcols = (unsigned)((state.columns + kBlockCols - 1) / kBlockCols);
        state.block_cache = calloc((size_t)strips * bcols, sizeof(*state.block_cache));
        if (state.block_cache) {
            state.block_strips = strips;
            state.block_cols = bcols;
        }
    }
    Enlarge job = {
        .image = {.src = state.native, .width = state.view.width, .height = state.view.height,
                  .src_stride = state.view.width, .dst = state.hd,
                  .dst_stride = state.view.width * kKdcHdImageScale, .luma = state.luma},
        .marked = marked,
        .use_block_cache = use_cache && state.block_cache != NULL,
        .first_strip = first_strip,
    };
    atomic_init(&job.scaled_tiles, 0);
    KdcParallelFor(strip_count, EnlargeStrip, &job);
    return atomic_load_explicit(&job.scaled_tiles, memory_order_relaxed);
}

static uint64_t EnlargeMarked(const uint8_t *marked)
{
    const unsigned strips = (unsigned)((state.rows + kStrip - 1) / kStrip);
    return EnlargeMarkedRange(marked, 0, strips);
}

static bool ProgressiveEnlargeStep(void)
{
    const unsigned strips = (unsigned)((state.rows + kStrip - 1) / kStrip);
    if (state.prewarm_next_strip >= strips) return true;
    (void)EnlargeMarkedRange(NULL, state.prewarm_next_strip, 1);
    ++state.prewarm_next_strip;
    return state.prewarm_next_strip >= strips;
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
    const bool progressive_initial = enlarge_all &&
        (size_t)state.view.width * state.view.height >= kProgressiveSourcePixels;
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
        if ((enlarge_all && !state.prewarm_active) || all || (luma_dirty & (1u << p)))
            memcpy(state.luma + p * 16, luma + p * 16, 16 * sizeof(*luma));
    const bool chars_changed = all || memcmp(vram, state.chars, sizeof(state.chars));
    const bool map_changed = all || memcmp(ram + 0x13000, state.map, kMapBytes);
    if (!hd && state.hd_valid) {
        FreeTerrainBlockCache();
        free(state.hd);
        state.hd = NULL;
        state.view.hd = NULL;
        state.hd_valid = false;
        state.prewarm_active = false;
        state.prewarm_next_strip = 0;
    }
    if (!hd) luma_dirty = 0;
    if (!chars_changed && !map_changed && !luma_dirty && !enlarge_all)
        return &state.view;

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
    bool any = false, repair = false;
    memset(state.reach, 0, cells);
    for (int row = 0; row < state.rows; ++row)
        for (int column = 0; column < state.columns; ++column) {
            uint8_t *dirty = &state.dirty[(size_t)row * state.columns + column];
            if (!*dirty) continue;
            state.stats.composed_tiles += *dirty == 2;
            *dirty = 0;
            any = true;
            if (!hd || (enlarge_all && !progressive_initial)) continue;
            const int c0 = column > kReach ? column - kReach : 0;
            const int c1 = column + kReach < state.columns - 1 ? column + kReach : state.columns - 1;
            const int r0 = row > kReach ? row - kReach : 0;
            int r1 = row + kReach < state.rows - 1 ? row + kReach : state.rows - 1;
            if (progressive_initial) {
                /* Strips not built yet are enlarged fresh with current data,
                 * so only the part of the change's reach that overlaps an
                 * already-completed strip needs repairing. */
                const int built = (int)state.prewarm_next_strip * kStrip - 1;
                if (r1 > built) r1 = built;
                if (r0 > r1) continue;
            }
            repair = true;
            for (int r = r0; r <= r1; ++r)
                memset(state.reach + (size_t)r * state.columns + c0, 1, (size_t)(c1 - c0 + 1));
        }
    if (!hd) return &state.view;
    if (enlarge_all) {
        if (!state.hd)
            state.hd = malloc((size_t)state.view.width * state.view.height *
                              kKdcHdImageScale * kKdcHdImageScale);
        if (!state.hd) return &state.view; /* drawn from the source image */
        if (progressive_initial) {
            state.prewarm_active = true;
            const bool complete = ProgressiveEnlargeStep();
            if (repair) {
                const uint64_t scaled = EnlargeMarked(state.reach);
                if (state.block_cache)
                    state.stats.scaled_tiles += scaled;
                else
                    for (size_t i = 0; i < cells; ++i) state.stats.scaled_tiles += state.reach[i];
            }
            if (complete) {
                state.prewarm_active = false;
                state.prewarm_next_strip = 0;
                state.hd_valid = true;
                state.view.hd = state.hd;
                state.stats.scaled_tiles += cells;
            }
        } else {
            state.hd_valid = true;
            state.view.hd = state.hd;
            EnlargeMarked(NULL);
            state.stats.scaled_tiles += cells;
        }
    } else if (any) {
        const uint64_t scaled = EnlargeMarked(state.reach);
        if (state.block_cache)
            state.stats.scaled_tiles += scaled;
        else
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
    const bool changed = c1 >= 0;
    if (changed || full) {
        memcpy(scenery.map, ram + 0xdadc, sizeof(scenery.map));
        memcpy(scenery.chars, vram + 0x4000, sizeof(scenery.chars));
    }
    scenery.valid = true;
    const bool enlarge_all = hd && (!scenery.hd_valid || luma_changed);
    if (!changed && !enlarge_all) return &scenery.view;
    ++state.stats.scenery_builds;
    if (luma_changed || !scenery.hd_valid) memcpy(scenery.luma, luma, sizeof(luma));
    if (!hd) return &scenery.view;
    /* Backdrops often loop through animated frames. Compare a 64-bit hash
     * first, then verify the complete decoded image and settled luma, never
     * trusting a hash alone. Retaining lazily allocated frames up to
     * kSceneryFrames / kMaxSceneryCacheBytes (48 MiB desktop, 16 MiB Android;
     * ~1.06 MiB per distinct frame) eliminates cache thrashing on multi-frame
     * animated skies while strictly bounding memory usage. */
    uint64_t hash = HashBytes(UINT64_C(0xcbf29ce484222325), scenery.luma, 32 * sizeof(*scenery.luma));
    hash = HashBytes(hash, scenery.native, sizeof(scenery.native));
    ++scenery.use_clock;
    const bool can_grow = scenery.cache_bytes + kSceneryFrameBytes <= kMaxSceneryCacheBytes;
    SceneryFrame *victim = NULL;
    for (unsigned i = 0; i < kSceneryFrames; ++i) {
        SceneryFrame *frame = &scenery.frames[i];
        if (frame->valid && frame->native && frame->hd && frame->hash == hash &&
            !memcmp(frame->luma, scenery.luma, sizeof(frame->luma)) &&
            !memcmp(frame->native, scenery.native, kSceneryNativeBytes)) {
            frame->last_use = scenery.use_clock;
            scenery.view.hd = scenery.hd = frame->hd;
            scenery.hd_valid = true;
            ++state.stats.scenery_cache_hits;
            return &scenery.view;
        }
        const bool allocated = frame->native != NULL && frame->hd != NULL;
        if (!allocated && !can_grow) continue;
        if (!victim || (!frame->valid && victim->valid) ||
            (frame->valid && victim->valid && frame->last_use < victim->last_use))
            victim = frame;
    }
    SceneryFrame *frame = victim;
    if (!frame) {
        FreeSceneryFrames();
        return &scenery.view;
    }
    if (!frame->native || !frame->hd) {
        uint8_t *native_buf = frame->native ? frame->native : malloc(kSceneryNativeBytes);
        uint8_t *hd_buf = frame->hd ? frame->hd : malloc(kSceneryHdBytes);
        if (!native_buf || !hd_buf) {
            if (!frame->native) free(native_buf);
            if (!frame->hd) free(hd_buf);
            /* Allocation failure must never expose the previous frame's art. */
            FreeSceneryFrames();
            return &scenery.view;
        }
        if (!frame->native) scenery.cache_bytes += kSceneryNativeBytes;
        if (!frame->hd) scenery.cache_bytes += kSceneryHdBytes;
        frame->native = native_buf;
        frame->hd = hd_buf;
    }
    /* Partial updates are relative to the image shown last, which may be a
     * different cache entry. Seed the replacement before changing its area. */
    if (!enlarge_all && frame->hd != scenery.hd)
        memcpy(frame->hd, scenery.hd, kSceneryHdBytes);
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
    memcpy(frame->native, scenery.native, kSceneryNativeBytes);
    memcpy(frame->luma, scenery.luma, sizeof(frame->luma));
    frame->hash = hash;
    frame->last_use = scenery.use_clock;
    frame->valid = true;
    scenery.hd_valid = true;
    scenery.view.hd = scenery.hd;
    return &scenery.view;
}
