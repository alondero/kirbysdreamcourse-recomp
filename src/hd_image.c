#include "hd_image.h"
#include "hd_scaler.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

enum {
    kPlanes = 16,
    kExtent = kKdcHdImageExtent,
    kScale = kKdcHdImageScale,
    /* Smaller marks do not establish straight edges. */
    kMinPixels = 12,
    /* Pixel centres an edge must pass through to count as evidence. */
    kMinSupport = 3,
    kMinRelaxedSupport = 2,
    /* Neighbouring colours tried as the background of a relaxed fit. */
    kCandidates = 3,
    /* The widest overlay a relaxed fit looks across for more of a shape. */
    kThin = kKdcHdImageOverlay,
    /* Centres are tested this far beyond a region's bounding box. */
    kRing = 2,
    kMaskSide = kExtent + 2 * kRing,
    kNearSide = 3 * kExtent,
};

/* Inward edge normals. Coordinates are doubled so that pixel centres are odd
 * integers and the boundary midway to the next row of centres is integral. */
static const int8_t normal[kPlanes][2] = {
    {1, 0}, {-1, 0}, {0, 1}, {0, -1},
    {1, 2}, {1, -2}, {-1, 2}, {-1, -2},
    {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
    {2, 1}, {2, -1}, {-2, 1}, {-2, -1},
};
/* Among equally supported edges, drop the less likely slope first. */
static const uint8_t removal_rank[kPlanes] = {2, 2, 2, 2, 3, 3, 3, 3, 1, 1, 1, 1, 0, 0, 0, 0};

typedef struct Blob {
    int x0, y0, x1, y1; /* inclusive source bounds */
    int first;          /* raster position of the first member: canonical order */
    int bound[kPlanes]; /* inside when n.Q >= bound, Q in eighths of a pixel */
    uint8_t planes[kPlanes], plane_count;
    uint8_t color;
    /* A relaxed fit only constrains the outline against the background, the
     * colour most of its border meets; other colours lie over it. */
    bool relaxed;
    uint8_t background;
} Blob;

enum { kUnvisited, kRejected, kMember, kPending };

typedef struct Work {
    const KdcHdImage *image;
    int x0, y0, x1, y1; /* window, exclusive end, within the image */
    int stride;
    uint8_t *state;
    int32_t *owner;
    Blob *blobs;
    unsigned count, capacity;
    /* The region's pixels, then any found across an overlay (Companions). */
    int members[kExtent * kExtent][2];
    uint16_t outside[kMaskSide * kMaskSide];
    /* Companions' visited marks: equal to stamp when visited in this call. */
    uint32_t near[kNearSide * kNearSide];
    uint32_t stamp;
    bool failed;
} Work;

static uint8_t Source(const KdcHdImage *image, int x, int y)
{
    if (x < 0 || y < 0 || x >= image->width || y >= image->height) return 0;
    return image->src[y * image->src_stride + x];
}

static int At(const Work *w, int x, int y)
{
    return (y - w->y0) * w->stride + (x - w->x0);
}

static bool InWindow(const Work *w, int x, int y)
{
    return x >= w->x0 && x < w->x1 && y >= w->y0 && y < w->y1;
}

/* The colours 4-adjacent to the region, most frequent first and the lower
 * index first on a tie. Returns how many were written, at most kCandidates. */
static int Neighbours(const Work *w, int count, uint8_t colors[kCandidates])
{
    static const int8_t step[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int tally[256] = {0};
    for (int i = 0; i < count; ++i)
        for (int d = 0; d < 4; ++d) {
            const int x = w->members[i][0] + step[d][0], y = w->members[i][1] + step[d][1];
            if (InWindow(w, x, y) && w->state[At(w, x, y)] == kPending) continue;
            ++tally[Source(w->image, x, y)];
        }
    int found = 0;
    while (found < kCandidates) {
        unsigned best = 0;
        for (unsigned i = 1; i < 256; ++i)
            if (tally[i] > tally[best]) best = i;
        if (!tally[best]) break;
        colors[found++] = (uint8_t)best;
        tally[best] = 0;
    }
    return found;
}

/* Pixels of the region's colour across at most kThin pixels that are neither
 * that colour, the background nor transparent: the other pieces of a shape
 * an overlay cuts through. They are appended to w->members after the
 * region's own count, and the bounds grow to include them. Returns the new
 * total, or count when there are none or they would exceed kExtent. */
static int Companions(Work *w, int count, int bounds[4], uint8_t color, uint8_t background)
{
    static const int8_t step[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    const int ox = bounds[0] - kExtent, oy = bounds[1] - kExtent;
    int b[4] = {bounds[0], bounds[1], bounds[2], bounds[3]};
    if (++w->stamp == 0) { /* the marks outlive calls; never reuse an old one */
        memset(w->near, 0, sizeof(w->near));
        w->stamp = 1;
    }
    const uint32_t stamp = w->stamp;
    for (int i = 0; i < count; ++i)
        w->near[(w->members[i][1] - oy) * kNearSide + (w->members[i][0] - ox)] = stamp;
    int total = count;
    for (int next = 0; next < total; ++next) {
        for (int d = 0; d < 4; ++d) {
            for (int s = 1; s <= kThin + 1; ++s) {
                const int x = w->members[next][0] + s * step[d][0];
                const int y = w->members[next][1] + s * step[d][1];
                const uint8_t value = Source(w->image, x, y);
                if (!value || value == background) break;
                if (value != color) continue;
                /* Within the near box whenever the bounds stay in kExtent. */
                const int x0 = x < b[0] ? x : b[0], x1 = x > b[2] ? x : b[2];
                const int y0 = y < b[1] ? y : b[1], y1 = y > b[3] ? y : b[3];
                if (x1 - x0 >= kExtent || y1 - y0 >= kExtent) return count;
                uint32_t *mark = &w->near[(y - oy) * kNearSide + (x - ox)];
                if (*mark != stamp) {
                    *mark = stamp;
                    b[0] = x0; b[1] = y0; b[2] = x1; b[3] = y1;
                    w->members[total][0] = x;
                    w->members[total++][1] = y;
                }
                break;
            }
        }
    }
    if (total > count) memcpy(bounds, b, sizeof(b));
    return total;
}

/* Decide whether the flooded region is an exact polygon and record it.
 *
 * A strict fit (relaxed false) must separate the region from every other
 * pixel. A relaxed fit, tried when the strict one fails, only separates it
 * from the given background colour and ignores the others: a line painted
 * across a checker square cuts it into pieces that are each still part of
 * the square's rhombus. Its evidence, w->members[0, total), may include the
 * other pieces; only the region's own count pixels are painted. */
static bool Analyse(Work *w, int count, int total, const int bounds[4], uint8_t color,
                    bool relaxed, uint8_t background)
{
    if (total < kMinPixels) return false;
    const int bx0 = bounds[0], by0 = bounds[1], bx1 = bounds[2], by1 = bounds[3];
    int low[kPlanes], support[kPlanes];
    for (int k = 0; k < kPlanes; ++k) {
        low[k] = INT32_MAX;
        support[k] = 0;
    }
    int first = INT32_MAX;
    for (int i = 0; i < total; ++i) {
        const int px = 2 * w->members[i][0] + 1, py = 2 * w->members[i][1] + 1;
        const int at = w->members[i][1] * w->image->width + w->members[i][0];
        if (i < count && at < first) first = at;
        for (int k = 0; k < kPlanes; ++k) {
            const int value = normal[k][0] * px + normal[k][1] * py;
            if (value < low[k]) { low[k] = value; support[k] = 1; }
            else if (value == low[k]) ++support[k];
        }
    }
    /* Which edges exclude each centre that is not part of the region. A
     * centre no edge excludes means the region is not this polygon. */
    uint16_t *outside = w->outside;
    int outside_count = 0;
    for (int y = by0 - kRing; y <= by1 + kRing; ++y) {
        for (int x = bx0 - kRing; x <= bx1 + kRing; ++x) {
            if (InWindow(w, x, y) && w->state[At(w, x, y)] == kPending) continue;
            if (relaxed && Source(w->image, x, y) != background) continue;
            unsigned mask = 0;
            for (int k = 0; k < kPlanes; ++k)
                if (normal[k][0] * (2 * x + 1) + normal[k][1] * (2 * y + 1) < low[k])
                    mask |= 1u << k;
            if (!mask) return false;
            outside[outside_count++] = (uint16_t)mask;
        }
    }
    /* Keep only the edges the pixels require, weakest evidence first. */
    uint8_t order[kPlanes];
    for (int k = 0; k < kPlanes; ++k) {
        int at = k;
        while (at > 0) {
            const int other = order[at - 1];
            if (support[other] < support[k] ||
                (support[other] == support[k] && removal_rank[other] <= removal_rank[k])) break;
            order[at] = order[at - 1];
            --at;
        }
        order[at] = (uint8_t)k;
    }
    unsigned active = (1u << kPlanes) - 1;
    for (int i = 0; i < kPlanes; ++i) {
        const unsigned bit = 1u << order[i];
        bool needed = false;
        for (int j = 0; j < outside_count && !needed; ++j)
            needed = (outside[j] & active) == bit;
        if (!needed) active &= ~bit;
    }
    Blob blob = {.x0 = bx0, .y0 = by0, .x1 = bx1, .y1 = by1, .first = first, .color = color,
                 .relaxed = relaxed, .background = background};
    for (int k = 0; k < kPlanes; ++k) {
        if (!(active & (1u << k))) continue;
        /* A required edge resting on one or two pixels is an irregular
         * outline, not a straight side. A piece cut from a square keeps as
         * little as two steps of a side, so a relaxed fit accepts two. */
        if (support[k] < (relaxed ? kMinRelaxedSupport : kMinSupport)) return false;
        /* n.P >= low - 1 in doubled coordinates; Q = 4P. An edge through
         * texel centres belongs to one side only, so neighbours agree. */
        blob.bound[k] = 4 * low[k] - 4 +
            (normal[k][0] < 0 || (normal[k][0] == 0 && normal[k][1] < 0));
        blob.planes[blob.plane_count++] = (uint8_t)k;
    }
    if (blob.plane_count < 3) return false;
    if (w->count == w->capacity) {
        unsigned capacity = w->capacity ? w->capacity * 2 : 64;
        Blob *grown = realloc(w->blobs, capacity * sizeof(*grown));
        if (!grown) { w->failed = true; return false; }
        w->blobs = grown;
        w->capacity = capacity;
    }
    w->blobs[w->count] = blob;
    return true;
}

static void Flood(Work *w, int seed_x, int seed_y)
{
    const KdcHdImage *image = w->image;
    const uint8_t color = Source(image, seed_x, seed_y);
    int count = 0, next = 0;
    int bx0 = seed_x, bx1 = seed_x, by0 = seed_y, by1 = seed_y;
    bool large = false;
    w->members[count][0] = seed_x;
    w->members[count++][1] = seed_y;
    w->state[At(w, seed_x, seed_y)] = kPending;
    while (next < count && !large) {
        const int x = w->members[next][0], y = w->members[next][1];
        ++next;
        static const int8_t step[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (int d = 0; d < 4; ++d) {
            const int nx = x + step[d][0], ny = y + step[d][1];
            if (nx < 0 || ny < 0 || nx >= image->width || ny >= image->height ||
                Source(image, nx, ny) != color) continue;
            /* Leaving the window, or meeting part of a region already found
             * to be too large, means this one is too large as well. */
            if (!InWindow(w, nx, ny) || w->state[At(w, nx, ny)] == kRejected) { large = true; break; }
            if (w->state[At(w, nx, ny)] != kUnvisited) continue;
            const int nx0 = nx < bx0 ? nx : bx0, nx1 = nx > bx1 ? nx : bx1;
            const int ny0 = ny < by0 ? ny : by0, ny1 = ny > by1 ? ny : by1;
            if (nx1 - nx0 >= kExtent || ny1 - ny0 >= kExtent) { large = true; break; }
            bx0 = nx0; bx1 = nx1; by0 = ny0; by1 = ny1;
            w->members[count][0] = nx;
            w->members[count++][1] = ny;
            w->state[At(w, nx, ny)] = kPending;
        }
    }
    const int bounds[4] = {bx0, by0, bx1, by1};
    bool polygon = !large && Analyse(w, count, count, bounds, color, false, 0);
    /* Specks are not tried: they are most of a textured image's regions and
     * their outline is too small to matter. */
    if (!large && !polygon && count >= kMinPixels) {
        /* The background is the neighbour a fit constrains on enough sides;
         * a line beside a thin piece can outnumber it. The whole shape is
         * better evidence than one piece, which can be too small alone. */
        uint8_t candidates[kCandidates];
        const int found = Neighbours(w, count, candidates);
        /* With a single neighbouring colour there is nothing over the shape
         * and the strict verdict stands. */
        for (int i = 0; found > 1 && i < found && !polygon; ++i) {
            int whole[4] = {bx0, by0, bx1, by1};
            const int total = Companions(w, count, whole, color, candidates[i]);
            if (total > count)
                polygon = Analyse(w, count, total, whole, color, true, candidates[i]);
            if (!polygon)
                polygon = Analyse(w, count, count, bounds, color, true, candidates[i]);
        }
    }
    for (int i = 0; i < count; ++i) {
        const int at = At(w, w->members[i][0], w->members[i][1]);
        w->state[at] = polygon ? kMember : kRejected;
        w->owner[at] = polygon ? (int32_t)w->count : -1;
    }
    if (polygon) ++w->count;
}

/* The first required edge that excludes a texel centre, or -1 when inside. */
static int Excluded(const Blob *blob, int qx, int qy)
{
    for (unsigned i = 0; i < blob->plane_count; ++i) {
        const int k = blob->planes[i];
        if (normal[k][0] * qx + normal[k][1] * qy < blob->bound[k]) return k;
    }
    return -1;
}

static bool Touches(const Work *w, int index, int x, int y)
{
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (!InWindow(w, x + dx, y + dy)) continue;
            const int at = At(w, x + dx, y + dy);
            if (w->state[at] == kMember && w->owner[at] == index) return true;
        }
    return false;
}

static void Paint(const Work *w, int index, bool inside_pass, int rx0, int ry0, int rx1, int ry1)
{
    const KdcHdImage *image = w->image;
    const Blob *blob = &w->blobs[index];
    const int x0 = blob->x0 - 1 > rx0 ? blob->x0 - 1 : rx0;
    const int y0 = blob->y0 - 1 > ry0 ? blob->y0 - 1 : ry0;
    const int x1 = blob->x1 + 2 < rx1 ? blob->x1 + 2 : rx1;
    const int y1 = blob->y1 + 2 < ry1 ? blob->y1 + 2 : ry1;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const int at = At(w, x, y);
            const bool member = w->state[at] == kMember && w->owner[at] == index;
            /* Another polygon's own pixels are entirely its business in the
             * first pass; this one may still extend a corner into them. */
            if (!member && ((!inside_pass && w->state[at] == kMember) || !Touches(w, index, x, y)))
                continue;
            if (member && inside_pass) continue;
            const uint8_t native = image->src[y * image->src_stride + x];
            /* What lies over a relaxed polygon keeps its own enlargement. */
            if (!member && blob->relaxed && native != blob->background) continue;
            uint8_t *out = image->dst + (size_t)(y * kScale) * image->dst_stride + x * kScale;
            for (int sy = 0; sy < kScale; ++sy) {
                for (int sx = 0; sx < kScale; ++sx) {
                    uint8_t *texel = out + sy * image->dst_stride + sx;
                    /* Where the general scaler smoothed an overlay into or
                     * beside a relaxed shape, the overlay keeps it: the shape
                     * only redraws its outline against its background. */
                    if (blob->relaxed && *texel != blob->color && *texel != blob->background)
                        continue;
                    const int k = Excluded(blob, 8 * x + 2 * sx + 1, 8 * y + 2 * sy + 1);
                    if (inside_pass) {
                        if (k < 0) *texel = blob->color;
                    } else if (!member) {
                        /* Undo the general scaler's own rounding of this
                         * shape; the polygon decides its outline. */
                        if (*texel == blob->color) *texel = native;
                    } else if (k < 0) {
                        *texel = blob->color;
                    } else {
                        /* A staircase corner beyond the straight edge shows
                         * the pixel across that edge. */
                        const int nx = normal[k][0], ny = normal[k][1];
                        const bool vertical = abs(ny) >= abs(nx);
                        *texel = Source(image, x - (vertical ? 0 : (nx > 0) - (nx < 0)),
                                        y - (vertical ? (ny > 0) - (ny < 0) : 0));
                    }
                }
            }
        }
    }
}

static int ByFirstMember(const void *a, const void *b)
{
    const Blob *const *x = a, *const *y = b;
    return ((*x)->first > (*y)->first) - ((*x)->first < (*y)->first);
}

/* Each thread keeps its working memory between calls. Allocating and zeroing
 * it on every call queued the workers on the heap and cost more than the
 * work itself on small rectangles. */
typedef struct Scratch {
    Work *work;
    uint8_t *state;
    int32_t *owner;
    size_t area;
} Scratch;
static _Thread_local Scratch scratch;

static Work *Acquire(size_t area)
{
    if (!scratch.work && !(scratch.work = calloc(1, sizeof(*scratch.work)))) return NULL;
    if (scratch.area < area) {
        free(scratch.state);
        free(scratch.owner);
        scratch.state = malloc(area);
        scratch.owner = malloc(area * sizeof(*scratch.owner));
        scratch.area = scratch.state && scratch.owner ? area : 0;
        if (!scratch.area) return NULL;
    }
    Work *w = scratch.work;
    memset(scratch.state, kUnvisited, area);
    w->state = scratch.state;
    w->owner = scratch.owner;
    w->count = 0;
    w->failed = false;
    return w;
}

int KdcHdImageUpdate(const KdcHdImage *image, int x0, int y0, int x1, int y1)
{
    if (!image || !image->src || !image->dst || !image->luma) return 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > image->width) x1 = image->width;
    if (y1 > image->height) y1 = image->height;
    if (x0 >= x1 || y0 >= y1) return 1;
    KdcXbrScaleRegion(image->src, image->width, image->height, image->src_stride,
                      x0, y0, x1, y1, image->luma, kScale, image->dst, image->dst_stride);

    const int wx0 = x0 - kKdcHdImageReach > 0 ? x0 - kKdcHdImageReach : 0;
    const int wy0 = y0 - kKdcHdImageReach > 0 ? y0 - kKdcHdImageReach : 0;
    const int wx1 = x1 + kKdcHdImageReach < image->width ? x1 + kKdcHdImageReach : image->width;
    const int wy1 = y1 + kKdcHdImageReach < image->height ? y1 + kKdcHdImageReach : image->height;
    Work *w = Acquire((size_t)(wx1 - wx0) * (size_t)(wy1 - wy0));
    if (!w) return 0;
    w->image = image;
    w->x0 = wx0;
    w->y0 = wy0;
    w->x1 = wx1;
    w->y1 = wy1;
    w->stride = wx1 - wx0;
    /* Every polygon with a pixel in or beside the rectangle. */
    for (int y = y0 - 1; y <= y1; ++y)
        for (int x = x0 - 1; x <= x1; ++x)
            if (InWindow(w, x, y) && w->state[At(w, x, y)] == kUnvisited && Source(image, x, y))
                Flood(w, x, y);
    int ok = !w->failed;
    const Blob **order = ok && w->count ? malloc(w->count * sizeof(*order)) : NULL;
    if (ok && w->count && !order) ok = 0;
    if (order) {
        for (unsigned i = 0; i < w->count; ++i) order[i] = &w->blobs[i];
        qsort(order, w->count, sizeof(*order), ByFirstMember);
        for (int pass = 0; pass < 2; ++pass)
            for (unsigned i = 0; i < w->count; ++i)
                Paint(w, (int)(order[i] - w->blobs), pass != 0, x0, y0, x1, y1);
    }
    free(order);
    return ok;
}
