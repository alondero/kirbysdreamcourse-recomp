#include "transitions.h"
#include "practice.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint32_t sites[4];
static InterpPreOpcodeHook hooks[4];
static unsigned count;
static bool enabled, dev;
static KdcPracticeView view;
static uint8_t ram[0x20000], before[0x20000];
static CpuState cpu;

bool KdcFasterTransitionsEnabled(void) { return enabled; }
int KdcDevEnabled(void) { return dev; }
const KdcPracticeView *KdcPracticeCurrentView(void) { return &view; }
bool interp_bridge_add_pre_opcode_hook(uint32_t pc, InterpPreOpcodeHook hook)
{
    CHECK(count < 4);
    sites[count] = pc; hooks[count++] = hook;
    return true;
}
static void At(uint32_t pc)
{
    CpuState original = cpu;
    memcpy(before, ram, sizeof(ram));
    for (unsigned i = 0; i < count; ++i) {
        if (sites[i] != pc) continue;
        hooks[i](&cpu, pc);
        CHECK(!memcmp(before, ram, sizeof(ram)));
        CHECK(!memcmp(&original, &cpu, sizeof(cpu)));
        return;
    }
    CHECK(0);
}
static void NewGame(void)
{
    KdcTransitionsReset();
    memset(ram, 0, sizeof(ram));
    view = (KdcPracticeView){0}; dev = false;
    At(0x80f1df);
}
int main(int argc, char **argv)
{
    (void)argv;
    cpu.ram = ram;
    KdcTransitionsInitialize();
    if (argc > 1) {
        enabled = true; NewGame(); At(0x80f48c);
        CHECK(KdcTransitionsSpeed() == 1); /* unqualified audio clock */
        return 0;
    }
    At(0x80f48c); CHECK(KdcTransitionsSpeed() == 1); /* unknown scene */
    NewGame(); At(0x80f48c); CHECK(KdcTransitionsSpeed() == 1); /* default */
    enabled = true;
    At(0x80f48c); CHECK(KdcTransitionsSpeed() == 3);
    At(0x80f256); CHECK(KdcTransitionsSpeed() == 1);
    At(0x80f48c); At(0x80f120); CHECK(KdcTransitionsSpeed() == 1);
    At(0x80f48c); CHECK(KdcTransitionsSpeed() == 1);
    for (unsigned a = 0; a < 4; ++a) {
        NewGame();
        static const unsigned addresses[] = {0x17ff, 0xeab9, 0x6a5a, 0x6a5e};
        ram[addresses[a]] = a < 2 ? 1 : a == 2 ? 8 : 7;
        At(0x80f48c); CHECK(KdcTransitionsSpeed() == 1);
    }
    for (unsigned course = 0; course < 8; ++course) {
        for (unsigned hole = 0; hole < 7; ++hole) {
            NewGame(); ram[0x6a5a] = (uint8_t)course; ram[0x6a5e] = (uint8_t)hole;
            At(0x80f48c); CHECK(KdcTransitionsSpeed() == 3);
        }
    }
    for (int mode = 0; mode < 4; ++mode) {
        NewGame();
        if (mode == 0) view.active = true;
        if (mode == 1) view.cpu = 1;
        if (mode == 2) view.screen = 1;
        if (mode == 3) dev = true;
        At(0x80f48c); CHECK(KdcTransitionsSpeed() == 1);
    }
    NewGame(); At(0x80f48c);
    for (unsigned i = 0; i < 1799; ++i) KdcTransitionsFrame();
    CHECK(KdcTransitionsSpeed() == 3);
    KdcTransitionsFrame(); CHECK(KdcTransitionsSpeed() == 1);
    NewGame(); At(0x80f48c); enabled = false;
    CHECK(KdcTransitionsSpeed() == 1);
    KdcTransitionsFrame(); enabled = true;
    CHECK(KdcTransitionsSpeed() == 1);
    At(0x80f48c); KdcTransitionsReset(); /* reset/load fails closed */
    CHECK(KdcTransitionsSpeed() == 1);
    At(0x80f48c); CHECK(KdcTransitionsSpeed() == 1);
    return 0;
}
