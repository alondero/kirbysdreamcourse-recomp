/* One set of host definitions per standalone synthetic APU executable. */
#include "apu_fixture_host.h"

uint64_t g_apu_timer0_total_ticks;
int snes_frame_counter;
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}
