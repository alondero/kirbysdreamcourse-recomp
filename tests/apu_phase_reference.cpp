/* Link the unmodified pinned Snes9x SMP and DSP, without a libretro host.
 * Access its public machine state to set up synthetic cases; no probe DLL. */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "apu/bapu/snes/snes.hpp"

SSettings Settings = {};
namespace SNES { CPU cpu; }
#include "apu/bapu/smp/smp.cpp"
#include "apu/bapu/dsp/sdsp.cpp"
using namespace SNES;

static void start(const uint8_t *program, size_t size) {
  smp.reset();
  smp.status.iplrom_enable = false;
  smp.regs.pc = 0x200;
  smp.clock = 0;
  cpu.reset();
  memcpy(smp.apuram + 0x200, program, size);
  dsp.power();
}

static void advance(unsigned clocks) { smp.clock -= static_cast<int>(clocks); smp.enter(); }

static void timer_cases(void) {
  for (unsigned timer = 0; timer < 3; ++timer)
    for (unsigned phase = 0; phase < 8; ++phase)
      for (unsigned initial = 0; initial < 3; ++initial)
        for (unsigned op = 0; op < 2; ++op) {
          unsigned opcode = op ? 0xeb : 0xe4;
          const unsigned counters[] = {0, 3, 15};
          const uint8_t program[] = {(uint8_t)opcode, (uint8_t)(0xfd + timer),
                                     (uint8_t)opcode, (uint8_t)(0xfd + timer)};
          start(program, sizeof(program));
          if (timer == 0) {
            smp.timer0.enable = true; smp.timer0.target = 1;
            smp.timer0.stage1_ticks = 127 - phase;
            smp.timer0.stage3_ticks = counters[initial];
          } else if (timer == 1) {
            smp.timer1.enable = true; smp.timer1.target = 1;
            smp.timer1.stage1_ticks = 127 - phase;
            smp.timer1.stage3_ticks = counters[initial];
          } else {
            smp.timer2.enable = true; smp.timer2.target = 1;
            smp.timer2.stage1_ticks = 15 - phase;
            smp.timer2.stage3_ticks = counters[initial];
          }
          printf("timer %u %u %u %u", timer, phase, counters[initial], opcode);
          for (unsigned read = 0; read < 2; ++read) {
            advance(3);
            unsigned counter = timer == 0 ? smp.timer0.stage3_ticks :
                timer == 1 ? smp.timer1.stage3_ticks : smp.timer2.stage3_ticks;
            printf(" %u %u", op ? smp.regs.B.y : smp.regs.B.a, counter);
          }
          puts("");
        }
}

static void port_cases(void) {
  const uint8_t program[] = {0xf4, 0xf4, 0x74, 0xf4, 0xd0, 0xfa,
                             0xd4, 0x00, 0x2f, 0xfe};
  for (unsigned phase = 0; phase <= 12; ++phase) {
    start(program, sizeof(program));
    smp.apuram[0] = 0x55;
    advance(phase);
    if (phase >= 5 && phase <= 7)
      printf("pause %u %u %u %d\n", phase, smp.regs.pc, smp.opcode_cycle, smp.clock);
    cpu.port_write(0, 0xff);
    advance(60 - phase);
    printf("race %u %u\n", phase, smp.apuram[0]);
  }
  const uint8_t store[] = {0xd4, 0xf4};
  start(store, sizeof(store));
  smp.regs.B.a = 0xcc;
  smp.port_write(0, 0x11);
  for (unsigned phase = 1; phase <= 5; ++phase) {
    advance(1);
    printf("store %u %u\n", phase, smp.port_read(0));
  }
}

static void test_dsp_start(uint8_t *ram) {
  uint8_t regs[128] = {0};
  regs[0x6c] = 0x20;
  dsp.spc_dsp.init(ram);
  dsp.spc_dsp.load(regs);
}
static void test_dsp_write(unsigned address, unsigned value) { dsp.spc_dsp.write(address, value); }
static unsigned test_dsp_read(unsigned address) { return dsp.spc_dsp.read(address); }
static void test_dsp_step(int16_t *output) {
  dsp.spc_dsp.set_output(output, 2);
  dsp.spc_dsp.run(32);
  assert(dsp.spc_dsp.sample_count() == 2);
}
static void test_dsp_finish(void) {}
#include "apu_dsp_cases.h"

int main(void) {
  Settings.InterpolationMethod = 2;
  for (unsigned i = 0; i < 8; ++i) Settings.ChannelsVolumePercent[i] = 100;
  timer_cases();
  port_cases();
  dsp_cases();
  return 0;
}
