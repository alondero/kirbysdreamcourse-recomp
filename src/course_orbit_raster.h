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
} KdcOrbitTexture;

typedef bool KdcOrbitTerrainRaster(const KdcOrbitFace *, unsigned count,
    const KdcOrbitTexture *, int samples, bool smooth,
    uint8_t *pixels, size_t pitch, float *depth, int width, int height);

void KdcOrbitSetTerrainRaster(KdcOrbitTerrainRaster *raster);
bool KdcOrbitGlTerrain(const KdcOrbitFace *, unsigned count,
    const KdcOrbitTexture *, int samples, bool smooth,
    uint8_t *pixels, size_t pitch, float *depth, int width, int height);

#endif
