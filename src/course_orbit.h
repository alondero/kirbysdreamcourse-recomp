#ifndef KDC_COURSE_ORBIT_H
#define KDC_COURSE_ORBIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Host presentation hooks. Input advances once per simulation tick; drawing
 * is read-only with respect to the emulated machine. */
void KdcOrbitInput(int horizontal, int vertical, bool reset);
void KdcOrbitReset(void);
int KdcOrbitDraw(uint8_t *dst, size_t pitch, const uint8_t *field,
                  int width, int height, double alpha);

#endif
