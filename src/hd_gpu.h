#ifndef KDC_HD_GPU_H
#define KDC_HD_GPU_H

#include <stdint.h>

struct PpuHdGpuFrame;
unsigned KdcGlProgram(const char *fragment_source);
struct PpuHdGpuFrame *KdcHdGpuBegin(unsigned width, unsigned height, unsigned scale);
unsigned KdcHdGpuDraw(const struct PpuHdGpuFrame *frame, const uint8_t *native,
                      const uint32_t *cpu);

#endif
