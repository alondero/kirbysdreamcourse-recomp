/* Synthetic CPU port events; no game code or assets. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "snes/apu.h"

#include "apu_fixture_host.h"

static void check_upload_boundary(void) {
  Apu *apu = apu_init();
  assert(apu);
  apu_reset(apu);
  /* The upload handoff must not execute a cycle to publish a due write. */
  assert(apu_schedulePortWrite(apu, 0, 0x12, 100));
  assert(apu_runUntilPortQueueEmpty(apu, 1));
  assert(apu->portClock == 0 && apu->inPorts[0] == 0x12);
  assert(apu->cpuCyclesLeft == 7 && apu->cycles == 0);
  assert(apu->dsp->sampleWrite == 0);
  /* Already-due events consume no budget, including equal-time writes. */
  assert(apu_schedulePortWrite(apu, 0, 0x34, 100));
  assert(apu_schedulePortWrite(apu, 0, 0x56, 100));
  assert(apu_runUntilPortQueueEmpty(apu, 0));
  assert(apu->portClock == 0 && apu->inPorts[0] == 0x56);
  assert(apu_runUntilPortQueueEmpty(apu, 0));
  /* Reach a future boundary with exactly its time budget, without overshoot. */
  assert(apu_schedulePortWrite(apu, 1, 0x78, 105));
  assert(apu_runUntilPortQueueEmpty(apu, 5));
  assert(apu->portClock == 5 && apu->inPorts[1] == 0x78);
  /* Exhaustion leaves the next event pending, then one more cycle suffices. */
  assert(apu_schedulePortWrite(apu, 2, 0x9a, 110));
  assert(apu_schedulePortWrite(apu, 3, 0xbc, 112));
  assert(!apu_runUntilPortQueueEmpty(apu, 4));
  assert(apu->portClock == 9 && apu_portQueueDepth(apu) == 2);
  assert(apu->inPorts[2] == 0 && apu->inPorts[3] == 0);
  assert(!apu_runUntilPortQueueEmpty(apu, 0));
  assert(!apu_runUntilPortQueueEmpty(apu, 1));
  assert(apu->portClock == 10 && apu->inPorts[2] == 0x9a);
  assert(apu->inPorts[3] == 0 && apu_portQueueDepth(apu) == 1);
  assert(apu_runUntilPortQueueEmpty(apu, 2));
  assert(apu->portClock == 12 && apu->inPorts[3] == 0xbc);
  assert(apu_portQueueDepth(apu) == 0);
  /* A write becomes due after an external time advance, with no drain budget. */
  assert(apu_schedulePortWrite(apu, 0, 0xde, 115));
  apu_runCycles(apu, 3);
  assert(apu_portQueueDepth(apu) == 1);
  assert(apu_runUntilPortQueueEmpty(apu, 0));
  assert(apu->portClock == 15 && apu->inPorts[0] == 0xde);
  /* Reaching a DSP edge must not execute the sample scheduled at that edge. */
  assert(apu_schedulePortWrite(apu, 3, 0xf0, 132));
  assert(apu_runUntilPortQueueEmpty(apu, 17));
  assert(apu->portClock == 32 && apu->inPorts[3] == 0xf0);
  assert(apu->cycles == 32 && apu->dsp->sampleWrite == 1);
  assert(apu_schedulePortWrite(apu, 3, 0xf1, 132));
  assert(apu_runUntilPortQueueEmpty(apu, 0));
  assert(apu->portClock == 32 && apu->inPorts[3] == 0xf1);
  assert(apu->dsp->sampleWrite == 1);
  puts("upload queue boundary: passed");
  apu_free(apu);
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "--upload") == 0) {
    check_upload_boundary();
    return 0;
  }
  if (argc != 1) return 2;
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
