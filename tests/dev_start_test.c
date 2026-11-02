#include "dev_start.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static uint32_t redirect;
static InterpPreOpcodeHook hooks[3];
static unsigned sites;

bool interp_bridge_add_pre_opcode_hook(uint32_t pc, InterpPreOpcodeHook hook)
{
    CHECK(sites < 3);
    CHECK(pc == (sites == 0 ? 0x80f0ffu : sites == 1 ? 0x80f1dfu : 0x80c168u));
    hooks[sites++] = hook;
    return true;
}
void interp_bridge_pre_opcode_redirect(uint32_t pc) { redirect = pc; }
static unsigned Word(const uint8_t *ram, unsigned address)
{
    return ram[address] | ((unsigned)ram[address + 1] << 8);
}

int main(int argc, char **argv)
{
    CHECK(argc >= 2);
    if (!strcmp(argv[1], "parse")) {
        --argc; ++argv;
        int result = KdcDevParseArgs(&argc, argv);
        if (result < 0) return 2;
        if (result > 0) return 0;
        KdcDevInitialize();
        /* Ordinary host arguments survive, including their order. */
        for (int i = 1; i < argc; ++i) puts(argv[i]);
        return 0;
    }
    CHECK(argc == 5);
    unsigned map = (unsigned)strtoul(argv[2], NULL, 10);
    unsigned course = (unsigned)strtoul(argv[3], NULL, 10);
    int multiplayer = atoi(argv[4]);
    char *args[] = {"game", "--rom", "input.sfc", "--dev-hole", argv[1],
                    "--no-launcher", "--dev-two-player", NULL};
    int count = multiplayer && !strncmp(argv[1], "map:", 4) ? 7 : 6;
    CHECK(KdcDevParseArgs(&count, args) == 0);
    CHECK(count == 4 && !strcmp(args[1], "--rom") &&
          !strcmp(args[2], "input.sfc") && !strcmp(args[3], "--no-launcher") && !args[4]);
    KdcDevInitialize();
    CHECK(sites == 3);
    uint8_t ram[0x20000] = {0};
    CpuState cpu = {0};
    cpu.ram = ram;
    hooks[0](&cpu, 0x80f0ff);
    CHECK(redirect == 0x80f1db && Word(ram, 0x17ff) == (unsigned)multiplayer);
    hooks[1](&cpu, 0x80f1df);
    CHECK(redirect == 0x80f256);
    CHECK(Word(ram, 0x6a5a) == course && Word(ram, 0x6a5c) == course * 2);
    unsigned hole = !strncmp(argv[1], "map:", 4) ? 0 : map % 8;
    CHECK(Word(ram, 0x6a5e) == hole && Word(ram, 0x6a60) == hole * 2);
    /* Model the game's B0A4 indices before the real loader's entry. */
    if (strncmp(argv[1], "map:", 4)) {
        ram[0xabb2] = (uint8_t)map;
        ram[0xabb4] = (uint8_t)(map * 2); ram[0xabb5] = (uint8_t)(map * 2 >> 8);
        ram[0xabb6] = (uint8_t)(map * 3); ram[0xabb7] = (uint8_t)(map * 3 >> 8);
    }
    hooks[2](&cpu, 0x80c168);
    CHECK(Word(ram, 0xabb2) == map && Word(ram, 0xabb4) == map * 2 && Word(ram, 0xabb6) == map * 3);
    redirect = 0;
    hooks[0](&cpu, 0x80f0ff); CHECK(!redirect);
    KdcDevReset();
    hooks[0](&cpu, 0x80f0ff); CHECK(redirect == 0x80f1db);
    KdcDevReset(); KdcDevStateLoaded(); redirect = 0;
    hooks[0](&cpu, 0x80f0ff); CHECK(!redirect);
    return 0;
}
