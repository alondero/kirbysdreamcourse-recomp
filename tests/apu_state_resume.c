/* Save and resume mid-instruction with pending CPU writes and DSP output. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes/apu.h"

uint64_t g_apu_timer0_total_ticks;
int snes_frame_counter;
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}

typedef struct Buffer {
  SaveLoadInfo base;
  uint8_t bytes[1048576];
  size_t pos;
  int load;
} Buffer;

static void transfer(SaveLoadInfo *base, void *data, size_t size) {
  Buffer *buffer = (Buffer *)base;
  assert(size <= sizeof(buffer->bytes) - buffer->pos);
  if (buffer->load) memcpy(data, buffer->bytes + buffer->pos, size);
  else memcpy(buffer->bytes + buffer->pos, data, size);
  buffer->pos += size;
}

int main(void) {
  Buffer *saved = calloc(1, sizeof(Buffer)), *final = calloc(1, sizeof(Buffer));
  assert(saved && final);
  saved->base.func = final->base.func = transfer;
  /* MOV Y,timer; MOV A,inport; MOV outport+X,A; BRA to start. */
  const uint8_t program[] = {0xeb, 0xfd, 0xe4, 0xf4, 0xd4, 0xf5, 0x2f, 0xf8};
  for (unsigned cut = 0; cut <= 16; ++cut) {
    Apu *original = apu_init(), *restored = apu_init();
    assert(original && restored);
    apu_reset(original);
    apu_reset(restored);
    original->romReadable = false;
    original->spc->pc = 0x200;
    original->cpuCyclesLeft = 0;
    memcpy(original->ram + 0x200, program, sizeof(program));
    original->timer[0].enabled = true;
    original->timer[0].target = 1;
    original->timer[0].cycles = 1;
    assert(apu_schedulePortWrite(original, 0, 0x57, 0));
    assert(apu_schedulePortWrite(original, 0, 0xa6, 9));
    assert(apu_schedulePortWrite(original, 0, 0xbc, 20));
    apu_runCycles(original, cut);
    saved->pos = 0; saved->load = 0;
    ApuPortSched scheduler = {0};
    apu_saveload(original, &saved->base);
    apu_port_sched_save(original, &scheduler);
    saved->pos = 0; saved->load = 1;
    apu_saveload(restored, &saved->base);
    apu_port_sched_restore(restored, &scheduler);
    apu_runCycles(original, 64 - cut);
    for (unsigned i = cut; i < 64; ++i) apu_runCycles(restored, 1);
    saved->pos = final->pos = 0; saved->load = 0;
    apu_saveload(original, &saved->base);
    apu_saveload(restored, &final->base);
    assert(saved->pos == final->pos);
    assert(memcmp(saved->bytes, final->bytes, saved->pos) == 0);
    ApuPortSched replayed = {0};
    apu_port_sched_save(original, &scheduler);
    apu_port_sched_save(restored, &replayed);
    assert(memcmp(&scheduler, &replayed, sizeof(scheduler)) == 0);
    assert(original->portClock == 64 && original->dsp->sampleWrite == 2);
    apu_free(original);
    apu_free(restored);
  }
  puts("17 instruction-phase snapshot replays: passed");
  free(saved);
  free(final);
  return 0;
}
