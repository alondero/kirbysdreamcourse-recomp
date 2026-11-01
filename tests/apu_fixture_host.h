/* Link stubs for each standalone synthetic framework APU executable. */
#ifndef KDC_APU_FIXTURE_HOST_H
#define KDC_APU_FIXTURE_HOST_H

#include <stdint.h>

uint64_t g_apu_timer0_total_ticks;
int snes_frame_counter;
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}

#endif
