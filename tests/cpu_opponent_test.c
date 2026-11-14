/* ROM-free check of the CPU opponent's turn lifecycle. A stand-in for the
 * guest preview draws a straight, slowing roll; a stand-in for rollback
 * snapshots copies work RAM. */
#include "cpu_opponent.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK %d: %s\n", __LINE__, #x); exit(1); } } while (0)

#define DECEL 0.0016
static uint8_t ram[0x20000], before[0x20000], saved[0x20000];
static uint32_t sites[16];
static InterpPreOpcodeHook hooks[16];
static unsigned count, previews, max_loft, horizon_seen;
static bool saved_valid;
CpuState g_cpu;

bool interp_bridge_add_pre_opcode_hook(uint32_t pc, InterpPreOpcodeHook hook)
{ CHECK(count < 16); sites[count] = pc; hooks[count++] = hook; return true; }
static unsigned Word(unsigned a) { return ram[a] | (unsigned)ram[a + 1] << 8; }
static void Put(unsigned a, unsigned v) { ram[a] = (uint8_t)v; ram[a + 1] = (uint8_t)(v >> 8); }
static void Fixed(unsigned lo, unsigned hi, double v)
{ double w = floor(v); Put(hi, (unsigned)(int)w & 0xffff); Put(lo, (unsigned)((v - w) * 65536)); }
static void At(CpuState *cpu, uint32_t pc)
{
    for (unsigned i = 0; i < count; ++i)
        if (sites[i] == pc) { hooks[i](cpu, pc); return; }
    CHECK(0);
}

size_t RtlRollbackSnapshotBound(void) { return sizeof(saved); }
size_t RtlRollbackSaveToMemory(void *data, size_t capacity)
{
    CHECK(KdcCpuPlanning() && capacity >= sizeof(saved));
    memcpy(data, ram, sizeof(saved));
    memcpy(saved, ram, sizeof(saved));
    saved_valid = true;
    return sizeof(saved);
}
bool RtlRollbackLoadFromMemory(const void *data, size_t size)
{
    CHECK(KdcCpuPlanning() && saved_valid && size == sizeof(saved));
    CHECK(!memcmp(data, saved, sizeof(saved)));
    memcpy(ram, data, size);
    saved_valid = false;
    return true;
}

bool KdcCpuRunPreview(CpuState *cpu, uint32_t entry_pc, unsigned horizon)
{
    (void)horizon;
    return interp_bridge_run(cpu, entry_pc) == 1;
}

/* Where a shot from (2.5, 2.5) comes to rest along its angle. */
static double Roll(unsigned power) { double v = power * 0.0025; return v * v / (2 * DECEL); }

int interp_bridge_run(CpuState *cpu, uint32_t pc)
{
    CHECK(pc == 0x81a9b8 && KdcCpuPlanning() && saved_valid && cpu->ram == ram);
    ++previews;
    if (Word(0xd9b6) > max_loft) max_loft = Word(0xd9b6);
    Put(0x6ad2, 40);
    At(cpu, 0x81aa0e); /* The guide's tick limit, as the guest reaches it. */
    unsigned ticks = Word(0x6ad2);
    if (!Word(0x6ad6)) horizon_seen = ticks;
    double a = Word(0xd9b4) * 3.14159265358979323846 / 180, v = Word(0x6b3c) * 0.0025;
    double x = 2.5, y = 2.5;
    unsigned n = 0;
    for (; n < ticks / 3 && n < 512 && v > 0; ++n, v -= DECEL) {
        Fixed(0xb746 + n * 2, 0xbb46 + n * 2, x);
        Fixed(0xbf46 + n * 2, 0xc346 + n * 2, y);
        Put(0xc746 + n * 2, 0); Put(0xcb46 + n * 2, 0);
        x += cos(a) * v; y += sin(a) * v;
    }
    Put(0x6aee, n);
    Put(0x1234, 0xdead); /* Preview scratch the rollback must remove. */
    return 1;
}

static void Course(unsigned d)
{
    memset(ram, 0, sizeof(ram));
    Put(0x17ff, 1);
    Put(0x8b92, 16); Put(0x8b94, 16);
    Put(0xb4b8, 8); Put(0xb4ba, 0xffff); Put(0xb4f8, 0); Put(0x4e1c, 0x49);
    Put(0xb538, 6); Put(0xb578, 5); /* The cup, 5 cells away at about 37 degrees. */
    Fixed(0xd8ac, 0xd8b0, 2.5); Fixed(0xd8b4, 0xd8b8, 2.5);
    unsigned level = KdcCpuHandicap(d);
    static const unsigned caps[] = {63, 58, 50};
    Put(0x6b3c, caps[level]);
    Put(0x6acc, 3 - level);
    Put(0x6b5e, 3 - level);
    Put(0xd9b4, 300); /* The previous aim, well away from the cup. */
}

int main(void)
{
    CpuState cpu = {0};
    cpu.ram = g_cpu.ram = ram;
    KdcCpuInitialize();
    CHECK(!strcmp(KdcCpuName(KDC_CPU_EASY), "EASY") && !strcmp(KdcCpuName(KDC_CPU_MEDIUM), "MEDIUM") &&
          !strcmp(KdcCpuName(KDC_CPU_HARD), "HARD"));
    CHECK(KdcCpuHandicap(KDC_CPU_EASY) == 2 && KdcCpuHandicap(KDC_CPU_MEDIUM) == 1 &&
          KdcCpuHandicap(KDC_CPU_HARD) == 0);
    uint32_t input = 0xffffff;
    KdcCpuFrame(&input);
    CHECK(input == 0xffffff);
    for (unsigned d = 1; d <= 3; ++d) {
        KdcCpuSelect(d);
        input = 0xffffff;
        KdcCpuFrame(&input);
        CHECK(input == 0xfff);
        Course(d);
        /* A human turn changes nothing except the CPU's handicap column. */
        memcpy(before, ram, sizeof(ram));
        for (unsigned i = 0; i < count; ++i) At(&cpu, sites[i]);
        unsigned level = KdcCpuHandicap(d);
        CHECK(Word(0xecfd) == level && Word(0xed01) == level && Word(0xed05) == level);
        Put(0xecfd, 0); Put(0xed01, 0); Put(0xed05, 0);
        CHECK(!memcmp(before, ram, sizeof(ram)));

        Put(0x1803, 2);
        At(&cpu, 0x81a619);
        previews = max_loft = horizon_seen = 0;
        unsigned frames = 0;
        for (;; ++frames) {
            CHECK(frames < 200); /* Hard's search fits in about two seconds. */
            unsigned start = previews;
            memcpy(before, ram, sizeof(ram));
            input = 0;
            KdcCpuFrame(&input);
            /* Planning is invisible: live RAM is exactly as it was. */
            CHECK(!memcmp(before, ram, sizeof(ram)) && !KdcCpuPlanning());
            CHECK(previews - start <= 1);
            At(&cpu, 0x81a680);
            if (Word(0x67)) break; /* Decided: the cursor starts to move. */
            CHECK(Word(0xd9b4) == 300 && !Word(0x6b));
        }
        CHECK(previews >= 15);
        CHECK(horizon_seen == (d == 3 ? 240u : d == 2 ? 120u : 40u));
        CHECK(max_loft == (d == 1 ? 0u : d == 2 ? 30u : 60u));

        /* The aim moves only through Left/Right, a degree at a time as the
         * game's rotation does: one on the press, then each frame once held. */
        unsigned moving = 0, held = 0;
        for (unsigned f = 0; f < 400 && !Word(0x6b); ++f) {
            unsigned pad = Word(0x67), angle = Word(0xd9b4);
            held = pad ? held + 1 : 0;
            unsigned step = held > 15 || held == 1 ? 1 : 0;
            if (pad == 0x200) angle = (angle + step) % 360;
            if (pad == 0x100) angle = (angle + 360 - step) % 360;
            if (angle != Word(0xd9b4)) ++moving;
            Put(0xd9b4, angle);
            At(&cpu, 0x81a680);
        }
        CHECK(Word(0x6b) == 0x80 && moving > 5);
        unsigned angle = Word(0xd9b4);
        CHECK(angle > 25 && angle < 50);
        if (d == 1) CHECK(!Word(0xd9b6) && Word(0xd9b8) == 6);

        /* Spin: the side marker follows Left/Right, the other marker sweeps. */
        unsigned x = 0, confirmed = 0;
        for (unsigned f = 0; f < 200 && !confirmed; ++f) {
            Put(0x6b50, x); Put(0x6b52, (f / 3) % 13);
            At(&cpu, 0x81bfa9);
            if (Word(0x67) == 0x100) ++x;
            if (Word(0x67) == 0x200) --x;
            Put(0x6b50, x); Put(0x6b, 0);
            At(&cpu, 0x81bfb5);
            confirmed = Word(0x6b) == 0x80;
        }
        CHECK(confirmed && x == Word(0xd9b8));
        CHECK(abs((int)Word(0x6b52) - (int)Word(0xd9ba)) <= 1);

        unsigned cap = Word(0x6b3c), power = 0;
        for (; power <= cap; ++power) {
            Put(0x6b3a, power);
            At(&cpu, 0x81c005);
            if (Word(0x6b) == 0x80) break;
        }
        CHECK(power >= 1 && power <= cap);
        double a = angle * 3.14159265358979323846 / 180, r = Roll(power);
        double dx = 2.5 + cos(a) * r - 6.5, dy = 2.5 + sin(a) * r - 5.5;
        CHECK(dx * dx + dy * dy < 0.5); /* Even Easy's guessed roll finds the cup. */
        At(&cpu, 0x81c04c);

        /* The scorecard waits two seconds before accepting a CPU win. */
        Put(0x1803, 0); Put(0x6b84, 1); Put(0x51, 0);
        At(&cpu, 0x80f48c);
        for (unsigned i = 0; i < 119; ++i) At(&cpu, 0x83d495);
        CHECK(!Word(0x51));
        At(&cpu, 0x83d495);
        CHECK(Word(0x51) == 0x80);
        Put(0x6b84, 0); Put(0x51, 0);
        At(&cpu, 0x83d495);
        CHECK(!Word(0x51));

        KdcCpuSelect(0);
        memcpy(before, ram, sizeof(ram));
        Put(0x1803, 2);
        for (unsigned i = 0; i < count; ++i) At(&cpu, sites[i]);
        Put(0x1803, 0);
        CHECK(!memcmp(before, ram, sizeof(ram)));
    }
    return 0;
}
