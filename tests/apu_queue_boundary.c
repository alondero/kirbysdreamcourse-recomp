/* Synthetic CPU port events; no game code or assets. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "snes/apu.h"

uint64_t g_apu_timer0_total_ticks;
int snes_frame_counter;
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}

int main(void) {
  Apu *apu = apu_init();
  assert(apu);
  apu_reset(apu);
  assert(apu_schedulePortWrite(apu, 0, 0x12, 100));
  assert(apu_runToGuestCycle(apu, 100, 0));
  assert(apu->portClock == 0 && apu->inPorts[0] == 0x12);
  assert(apu_schedulePortWrite(apu, 1, 0x34, 100));
  assert(apu_runToGuestCycle(apu, 100, 0));
  assert(apu->portClock == 0 && apu->inPorts[1] == 0x34);
  assert(apu->cpuCyclesLeft == 7 && apu->cycles == 0);
  assert(apu->dsp->sampleWrite == 0);
  /* Budget exhaustion must leave the next event pending. */
  assert(apu_schedulePortWrite(apu, 2, 0x56, 105));
  assert(!apu_runToGuestCycle(apu, 105, 4));
  assert(apu->portClock == 4 && apu->inPorts[2] == 0);
  assert(apu_runToGuestCycle(apu, 105, 1));
  assert(apu->portClock == 5 && apu->inPorts[2] == 0x56);
  assert(apu_portQueueDepth(apu) == 0);
  /* An earlier read must not drain a later event, even if SPC is ahead. */
  assert(apu_schedulePortWrite(apu, 0, 0x78, 110));
  assert(apu_schedulePortWrite(apu, 0, 0x9a, 110));
  assert(apu_schedulePortWrite(apu, 1, 0xbc, 111));
  apu_runCycles(apu, 5);
  assert(apu->portClock == 10 && apu_portQueueDepth(apu) == 3);
  assert(apu_runToGuestCycle(apu, 109, 0));
  assert(apu->inPorts[0] == 0x12 && apu_portQueueDepth(apu) == 3);
  assert(apu_runToGuestCycle(apu, 110, 0));
  assert(apu->portClock == 10 && apu->inPorts[0] == 0x9a);
  assert(apu->inPorts[1] == 0x34 && apu_portQueueDepth(apu) == 1);
  assert(apu_runToGuestCycle(apu, 111, 1));
  assert(apu->portClock == 11 && apu->inPorts[1] == 0xbc);
  puts("queue boundary: passed");
  apu_free(apu);
  return 0;
}
