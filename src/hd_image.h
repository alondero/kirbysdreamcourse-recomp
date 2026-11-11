#ifndef KDC_HD_IMAGE_H
#define KDC_HD_IMAGE_H

#include <stdint.h>

/* Palette-locked 4x enlargement of a whole indexed image.
 *
 * Unlike a scaler that sees one 8x8 character at a time, every pixel is
 * enlarged with its real neighbours, so edges continue across character
 * boundaries. Two reconstructions are combined:
 *
 * 1. Flat convex shapes become exact polygons. A 4-connected region of one
 *    index, no larger than kKdcHdImageExtent, qualifies when it is exactly
 *    the set of pixel centres inside straight edges with 1:2, 1:1, 2:1,
 *    horizontal or vertical slopes. Each edge lies midway between the last
 *    row of centres inside and the first outside. An edge that excludes no
 *    centre is dropped, so two slanted edges meet in a point instead of the
 *    blunt end pixel art has to use. An isometric checker square therefore
 *    comes out as a true rhombus with straight sides and sharp corners.
 * 2. Everything else uses the edge-directed scaler in hd_scaler.h.
 *
 * The output for any texel is a pure function of the source image. Updating
 * a rectangle after a change therefore gives exactly the texels a complete
 * rebuild would. Output indices are always indices present nearby in the
 * source, so live palettes keep working. */
enum {
    kKdcHdImageScale = 4,
    /* Largest polygon bounding box, in source pixels per axis. */
    kKdcHdImageExtent = 40,
    /* The widest overlay (a line drawn over a shape) a fit looks across for
     * the shape's other pieces. */
    kKdcHdImageOverlay = 6,
    /* A changed source pixel can alter output texels up to this many source
     * pixels away, in each direction: a shape's pieces span less than the
     * extent, and the search for them looks one pixel past an overlay. */
    kKdcHdImageReach = kKdcHdImageExtent + kKdcHdImageOverlay + 1,
};

typedef struct KdcHdImage {
    /* width x height indices, rows src_stride bytes apart. 0 is transparent. */
    const uint8_t *src;
    int width, height, src_stride;
    /* (width*4) x (height*4) indices, rows dst_stride bytes apart. */
    uint8_t *dst;
    int dst_stride;
    /* 256 luma values, as for KdcXbrScale: -1 for transparency. */
    const float *luma;
} KdcHdImage;

/* Writes the output texels of source rectangle [x0,x1) x [y0,y1), clipped to
 * the image. Calls for disjoint rectangles may run concurrently. Returns
 * false, leaving the rectangle unspecified, only if memory is exhausted. */
int KdcHdImageUpdate(const KdcHdImage *image, int x0, int y0, int x1, int y1);

#endif
