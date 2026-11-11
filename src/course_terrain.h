#ifndef KDC_COURSE_TERRAIN_H
#define KDC_COURSE_TERRAIN_H

#include <stdbool.h>
#include <stdint.h>

struct Ppu;

/* The course's terrain planes (BG1 and BG2) composed, with their original
 * priority, into one image of the whole streamed map: the texture the
 * reconstructed course view draws from. It is rebuilt only where the live
 * tile maps, character graphics or palette have changed since the last call,
 * so a camera move never regenerates artwork. Reading it is thread-safe
 * between calls. Nothing here writes guest state. */
enum { kKdcTerrainOpaque = 1, kKdcTerrainPriority = 2 };
/* Texels per source pixel, per axis, in the enlarged images. */
enum { kKdcTerrainScale = 4 };

typedef struct KdcTerrain {
    /* Map position, in source pixels, of pixel (0,0), and the image size.
     * The image includes a border of one character around the map. */
    int left, top, width, height;
    /* width x height palette indices: CGRAM entries 1..127, 0 transparent. */
    const uint8_t *native;
    /* Per source pixel: kKdcTerrainOpaque when either plane is opaque there,
     * kKdcTerrainPriority when an opaque plane has its priority bit set. */
    const uint8_t *mask;
    /* The image enlarged by kKdcTerrainScale (hd_image.h), or NULL when it
     * was not requested. */
    const uint8_t *hd;
    /* 0xffRRGGBB for each palette index, from the live palette. */
    uint32_t colors[128];
} KdcTerrain;

/* Returns the up-to-date terrain, or NULL when the course map is absent or
 * implausible. hd also maintains the enlarged image. */
const KdcTerrain *KdcTerrainUpdate(const struct Ppu *ppu, const uint8_t *ram, bool hd);

/* The course's clean scenery plane (BG3 before the HUD is composited into
 * it): the 32x32 character map the game keeps at $DADC, drawn with the live
 * BG3 characters and palette. Opposite edges do not join; widescreen margins
 * mirror whole characters (KdcReflectedColumn). */
typedef struct KdcScenery {
    const uint8_t *native; /* 256 x 256 CGRAM indices 1..31, 0 transparent */
    const uint8_t *hd;     /* 1024 x 1024, or NULL when not requested */
    uint32_t colors[32];   /* 0xffRRGGBB */
} KdcScenery;

/* Rebuilt only when the map, its characters or its palette's luma change. */
const KdcScenery *KdcSceneryUpdate(const struct Ppu *ppu, const uint8_t *ram, bool hd);

/* Releases the images; the next update rebuilds everything. */
void KdcTerrainReset(void);

/* Cumulative work counters, for tests and diagnostics. */
typedef struct KdcTerrainStats {
    uint64_t rebuilds;       /* complete rebuilds (new map or first use) */
    uint64_t composed_tiles; /* characters decoded into the source image */
    uint64_t scaled_tiles;   /* characters whose enlargement was regenerated */
    uint64_t scenery_builds; /* scenery images redrawn (native or enlarged) */
} KdcTerrainStats;
void KdcTerrainGetStats(KdcTerrainStats *stats);

#endif
