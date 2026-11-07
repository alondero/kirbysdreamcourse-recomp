#ifndef KDC_COURSE_CHECKER_H
#define KDC_COURSE_CHECKER_H

#include <stdbool.h>
#include <stdint.h>

/* Analytic reconstruction of opaque, two-colour isometric tile edges.
 * Coordinates are unflipped tile coordinates [0,8); colours stay indexed.
 * Supported boundaries have x +/- y or x +/- 2y normals, at half-integer
 * source-centre thresholds. Models are a half-plane, intersection of two
 * nonparallel half-planes (including its non-convex exterior via colour
 * complement), or their XOR. Corner polarity is independent of scan order. */
typedef struct KdcCheckerEdge { int slope, threshold, reverse; } KdcCheckerEdge;
typedef struct KdcChecker {
    KdcCheckerEdge edge[2];
    uint8_t colors[2], operation;
} KdcChecker;

enum { kKdcCheckerBorderSize = 80 };
/* Two rows above/below the tile, then two columns on either side. */
static inline void KdcCheckerBorderPosition(int index, int *x, int *y)
{
    if (index < 48) {
        int row = index / 12;
        *x = index % 12 - 2;
        *y = row < 2 ? row - 2 : row + 6;
    } else {
        int column = (index - 48) / 8;
        *x = column < 2 ? column - 2 : column + 6;
        *y = (index - 48) % 8;
    }
}

/* Cheap eligibility check before fetching neighbouring tile data. */
bool KdcCheckerNeedsContext(const uint16_t words[16]);

/* Rejects ambiguous fits or mismatches at the 64 pixel centres and their
 * two-pixel border. Two rows establish the slope of a corner beyond the
 * interior centres; a single row can leave its direction ambiguous.
 * No ROM artwork, tile numbers or palette values are built into the model. */
bool KdcCheckerFit(const uint16_t words[16], const uint8_t border[kKdcCheckerBorderSize], KdcChecker *checker);
uint8_t KdcCheckerSample(const KdcChecker *checker, double x, double y);

#endif
