#ifndef KDC_CPU_OPPONENT_H
#define KDC_CPU_OPPONENT_H
#include <stdbool.h>
#include <stdint.h>

/* Optional USA-game controller for the second seat. Difficulty 0 leaves both
 * human seats untouched; 1, 2 and 3 are Easy, Medium and Hard. */
#define KDC_CPU_EASY 1u
#define KDC_CPU_MEDIUM 2u
#define KDC_CPU_HARD 3u

void KdcCpuInitialize(void);
void KdcCpuSelect(unsigned difficulty);
unsigned KdcCpuDifficulty(void);
const char *KdcCpuName(unsigned difficulty);
/* The original handicap grid level the CPU plays under (0 EASY end, 2
 * DIFFICULT end): the weakest CPU takes the most handicapped settings. */
unsigned KdcCpuHandicap(unsigned difficulty);
void KdcCpuReset(void);
/* Called at the start of each host frame, before guest code runs. Owns the
 * second controller seat and performs any off-screen shot planning. */
void KdcCpuFrame(uint32_t *inputs);
/* True only while the planner holds its private rollback snapshot. */
bool KdcCpuPlanning(void);
#endif
