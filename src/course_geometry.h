#ifndef KDC_COURSE_GEOMETRY_H
#define KDC_COURSE_GEOMETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Read-only view of the supported USA game's collision terrain. X/Y are
 * cell coordinates. Z is upward, in elevation steps (16 projected pixels).
 * ROM data remains in the player's verified cartridge, never compiled in. */
typedef struct KdcCourseGeometry {
    const uint8_t *ram, *rom;
    unsigned width, height;
    double origin_x, origin_y;
} KdcCourseGeometry;

bool KdcCourseGeometryInit(KdcCourseGeometry *view,
                           const uint8_t *ram, size_t ram_size,
                           const uint8_t *rom, size_t rom_size);

/* Sample a particular cell, including its edges. Separate calls on either
 * side of a cliff intentionally return different heights. A cell consists
 * of four planar triangles joining its corners to its centre. */
bool KdcCourseCellHeight(const KdcCourseGeometry *view,
                         unsigned column, unsigned row, double u, double v,
                         double *height);
bool KdcCourseGeometrySample(const KdcCourseGeometry *view,
                             double x, double y, double *height);

/* Original isometric projection, before subtracting the guest camera. */
void KdcCourseGeometryProject(const KdcCourseGeometry *view,
                              double x, double y, double height,
                              double *screen_x, double *screen_y);

#endif
