/* ROM-free unit test for KdcCpuRunPreview's LoROM/WRAM bus, 32 KiB mirror
 * boundary word reads ($xx:7FFF/$xx:8000), I/O fallback, and horizon hook. */
#include "cpu_opponent.h"
#include "common_cpu_infra.h"
#include "cpu_state.h"
#include "snes/cart.h"
#include "snes/interp_bridge.h"
#include "snes/snes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static uint8_t rom[0x100000];
static uint8_t ram[0x20000];
static Cart cart;
static Snes snes;
static unsigned io_reads, io_writes;
static uint8_t io_last_bank;
static uint16_t io_last_addr;
static uint8_t io_last_val;

const uint8_t *g_rom = rom;
Snes *g_snes = &snes;
CpuState g_cpu;

uint8_t cpu_read8(CpuState *cpu, uint8_t bank, uint16_t addr)
{
    (void)cpu;
    ++io_reads;
    io_last_bank = bank;
    io_last_addr = addr;
    return 0xCDu;
}

void cpu_write8(CpuState *cpu, uint8_t bank, uint16_t addr, uint8_t val)
{
    (void)cpu;
    ++io_writes;
    io_last_bank = bank;
    io_last_addr = addr;
    io_last_val = val;
}

int interp_bridge_run(CpuState *cpu, uint32_t pc)
{
    (void)cpu;
    (void)pc;
    CHECK(0 && "unexpected fallback to interp_bridge_run");
    return 0;
}

static uint16_t Word(unsigned a)
{
    return (uint16_t)ram[a] | ((uint16_t)ram[a + 1u] << 8);
}

static void RunRomCode(uint32_t entry_pc, const uint8_t *code, size_t len, unsigned horizon)
{
    uint32_t bank = (entry_pc >> 16) & 0x7Fu;
    uint16_t addr = (uint16_t)(entry_pc & 0xFFFFu);
    CHECK(addr >= 0x8000u);
    uint32_t off = (bank << 15) | (addr & 0x7FFFu);
    memcpy(rom + off, code, len);

    CpuState cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.ram = ram;
    cpu.S = 0x1FF0u;
    cpu.m_flag = false; /* 16-bit A */
    cpu.x_flag = false; /* 16-bit X/Y */
    CHECK(KdcCpuRunPreview(&cpu, entry_pc, horizon));
}

int main(void)
{
    cart.type = CART_LOROM;
    cart.romSize = sizeof(rom);
    snes.cart = &cart;
    memset(rom, 0xEA, sizeof(rom));
    memset(ram, 0, sizeof(ram));

    /* 1. $5F:7FFF -> $5F:8000 (last byte of 1 MiB ROM wraps to start of $5F window). */
    rom[0xFFFFF] = 0xA5u; /* $5F:7FFF */
    rom[0xF8000] = 0x5Au; /* $5F:8000 */
    static const uint8_t test_5f[] = {
        0xAF, 0xFF, 0x7F, 0x5F, /* LDA $5F7FFF */
        0x8D, 0x00, 0x00,       /* STA $0000 */
        0x6B                    /* RTL */
    };
    RunRomCode(0x818000u, test_5f, sizeof(test_5f), 0u);
    CHECK(Word(0x0000u) == 0x5AA5u);

    /* 2. $40:7FFF -> $40:8000 wraps within bank $40, not into bank $41 ($008000). */
    rom[0x007FFFu] = 0x34u; /* $40:7FFF */
    rom[0x000000u] = 0x12u; /* $40:8000 */
    rom[0x008000u] = 0x99u; /* $41:0000 (wrong bank if off+1 used) */
    static const uint8_t test_40[] = {
        0xAF, 0xFF, 0x7F, 0x40, /* LDA $407FFF */
        0x8D, 0x02, 0x00,       /* STA $0002 */
        0x6B                    /* RTL */
    };
    RunRomCode(0x818020u, test_40, sizeof(test_40), 0u);
    CHECK(Word(0x0002u) == 0x1234u);

    /* 3. $7E:FFFF -> $7F:0000 WRAM bank boundary. */
    ram[0x0FFFFu] = 0x11u;
    ram[0x10000u] = 0x22u;
    static const uint8_t test_wram[] = {
        0xAF, 0xFF, 0xFF, 0x7E, /* LDA $7EFFFF */
        0x8D, 0x04, 0x00,       /* STA $0004 */
        0x6B                    /* RTL */
    };
    RunRomCode(0x818040u, test_wram, sizeof(test_wram), 0u);
    CHECK(Word(0x0004u) == 0x2211u);

    /* 4. Non-RAM/ROM I/O fallback ($4216) + $01:AA0E horizon hook. */
    ram[0x6AD6u] = 0u;
    ram[0x6AD7u] = 0u;
    ram[0x6AD2u] = 0u;
    ram[0x6AD3u] = 0u;
    static const uint8_t test_io_and_hook[] = {
        0xE2, 0x20,             /* SEP #$20 (8-bit A) */
        0xAD, 0x16, 0x42,       /* LDA $4216 (I/O read) */
        0x8D, 0x06, 0x00,       /* STA $0006 */
        0x8D, 0x02, 0x42,       /* STA $4202 (I/O write) */
        0x6B                    /* RTL */
    };
    /* Place at $81:AA0E to trigger the horizon hook on entry. */
    RunRomCode(0x81AA0Eu, test_io_and_hook, sizeof(test_io_and_hook), 40u);
    CHECK(io_reads == 1u);
    CHECK(io_writes == 1u && io_last_addr == 0x4202u && io_last_val == 0xCDu);
    CHECK(ram[0x0006u] == 0xCDu);
    CHECK(Word(0x6AD2u) == 40u);

    printf("cpu_preview_test: all checks passed\n");
    return 0;
}
