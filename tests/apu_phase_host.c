/* Exercise the real framework APU with ROM-free programs. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes/apu.h"

#include "apu_fixture_host.h"

static int single_cycle;
static void advance(Apu *apu, unsigned clocks) {
  if (single_cycle) while (clocks--) apu_runCycles(apu, 1);
  else apu_runCycles(apu, clocks);
}

static Apu *start(const uint8_t *program, size_t size) {
  Apu *apu = apu_init();
  assert(apu);
  apu_reset(apu);
  apu->romReadable = false;
  apu->spc->pc = 0x200;
  apu->cpuCyclesLeft = 0;
  memcpy(apu->ram + 0x200, program, size);
  return apu;
}

static void timer_cases(void) {
  for (unsigned timer = 0; timer < 3; ++timer)
    for (unsigned phase = 0; phase < 8; ++phase)
      for (unsigned initial = 0; initial < 3; ++initial)
        for (unsigned op = 0; op < 2; ++op) {
          unsigned opcode = op ? 0xeb : 0xe4;
          const unsigned counters[] = {0, 3, 15};
          const uint8_t program[] = {opcode, 0xfd + timer, opcode, 0xfd + timer};
          Apu *apu = start(program, sizeof(program));
          apu->timer[timer].enabled = true;
          apu->timer[timer].target = 1;
          apu->timer[timer].counter = counters[initial];
          apu->timer[timer].cycles = phase;
          printf("timer %u %u %u %u", timer, phase, counters[initial], opcode);
          for (unsigned read = 0; read < 2; ++read) {
            advance(apu, 3);
            printf(" %u %u", op ? apu->spc->y : apu->spc->a, apu->timer[timer].counter);
          }
          puts("");
          apu_free(apu);
        }
}

static void port_cases(void) {
  const uint8_t program[] = {0xf4, 0xf4, 0x74, 0xf4, 0xd0, 0xfa,
                             0xd4, 0x00, 0x2f, 0xfe};
  for (unsigned phase = 0; phase <= 12; ++phase) {
    Apu *apu = start(program, sizeof(program));
    apu->ram[0] = 0x55;
    advance(apu, phase);
    apu_writePortNow(apu, 0, 0xff);
    advance(apu, 60 - phase);
    printf("race %u %u\n", phase, apu->ram[0]);
    apu_free(apu);
  }
  const uint8_t store[] = {0xd4, 0xf4};
  Apu *apu = start(store, sizeof(store));
  apu->spc->a = 0xcc;
  apu->outPorts[0] = 0x11;
  for (unsigned phase = 1; phase <= 5; ++phase) {
    advance(apu, 1);
    printf("store %u %u\n", phase, apu->outPorts[0]);
  }
  apu_free(apu);
}

static Dsp *test_dsp;
static void test_dsp_start(uint8_t *ram) { test_dsp = dsp_init(ram); assert(test_dsp); dsp_reset(test_dsp); }
static void test_dsp_write(unsigned address, unsigned value) { dsp_write(test_dsp, address, value); }
static unsigned test_dsp_read(unsigned address) { return dsp_read(test_dsp, address); }
static void test_dsp_step(int16_t *output) {
  dsp_cycle(test_dsp);
  assert(dsp_getSamples(test_dsp, output, 1) == 1);
}
static void test_dsp_finish(void) { dsp_free(test_dsp); }
#include "apu_dsp_cases.h"

int main(int argc, char **argv) {
  single_cycle = argc > 1 && atoi(argv[1]);
  timer_cases();
  port_cases();
  dsp_cases();
  return 0;
}
