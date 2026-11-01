#ifndef KDC_COURSE_SPRITES_H
#define KDC_COURSE_SPRITES_H

#include <stdint.h>

struct CpuState;
struct Ppu;

/* Observers never write guest state or perform bus reads. The cfg registers
 * the same instruction sites for generated code as Initialize does for LLE. */
void KdcObserveSprites(const struct CpuState *cpu, uint32_t pc);
void KdcInitializeSprites(void);
void KdcResetSprites(struct Ppu *ppu);
void KdcPrepareSprites(struct Ppu *ppu, int extra);
void KdcReportSprites(int frame);

#endif
