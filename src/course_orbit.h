#ifndef KDC_COURSE_ORBIT_H
#define KDC_COURSE_ORBIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Host presentation hooks. Input advances once per simulation tick; drawing
 * is read-only with respect to the emulated machine. */
void KdcOrbitInput(int horizontal, int vertical, bool reset);
void KdcOrbitReset(void);
/* High-refresh history is captured once after the native raster completes. */
void KdcOrbitConfigure(bool high_refresh, bool orbit_controls);
void KdcOrbitCapture(unsigned frame, int width, int height);
int KdcOrbitDraw(uint8_t *dst, size_t pitch, const uint8_t *field,
                  int width, int height, double alpha);
/* True when the latest KdcOrbitDraw composed the presented frame and the
 * view is still ready, so the PPU's own HD tile composition would not be
 * shown. A declined draw clears it, so the next frame keeps its HD tiles. */
bool KdcOrbitComposing(void);

#endif
