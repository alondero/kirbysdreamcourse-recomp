#ifndef KDC_CPU_OPPONENT_H
#define KDC_CPU_OPPONENT_H
#include <stdbool.h>
#include <stdint.h>

/* Optional USA-game controller. Zero leaves both human seats untouched. */
void KdcCpuInitialize(void);
void KdcCpuSelect(unsigned difficulty);
unsigned KdcCpuDifficulty(void);
const char *KdcCpuName(unsigned difficulty);
void KdcCpuReset(void);
void KdcCpuFrame(uint32_t *inputs);
#endif
