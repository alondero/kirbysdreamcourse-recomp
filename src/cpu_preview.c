/* Fast isolated 65816 runner for the CPU planner's off-screen trajectory
 * preview ($81:A9B8). Runs the exact same Interp816 core as interp_bridge_run
 * over a direct WRAM/LoROM bus without diagnostic rings, quiescent detection,
 * or per-opcode bridge overhead, falling back to cpu_read8/cpu_write8 for
 * hardware multiply/divide or any other non-RAM/ROM access. */
#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/cart.h"
#include "snes/interp816.h"
#include "snes/interp_bridge.h"
#include "snes/snes.h"
#include <stdlib.h>
#include <string.h>

#define PREVIEW_STEP_CAP 2000000u

typedef struct {
    CpuState *cpu;
    uint8_t *ram;
    const uint8_t *rom;
    uint32_t rom_mask;
} PreviewBus;

static inline bool PreviewIsLoRom(uint8_t bank, uint16_t addr)
{
    uint8_t b = bank & 0x7Fu;
    return (b < 0x40u && addr >= 0x8000u) || (b >= 0x40u && b < 0x70u);
}

static inline uint32_t PreviewLoRomOffset(uint8_t bank, uint16_t addr, uint32_t mask)
{
    return ((((uint32_t)(bank & 0x7Fu) << 15) | (addr & 0x7FFFu)) & mask);
}

static uint8_t PreviewRead(void *mem, uint32_t adr)
{
    PreviewBus *b = (PreviewBus *)mem;
    uint8_t bank = (uint8_t)(adr >> 16);
    uint16_t addr = (uint16_t)adr;
    if ((bank & 0xFEu) == 0x7Eu)
        return b->ram[((uint32_t)(bank & 1u) << 16) | addr];
    if ((bank & 0x7Fu) < 0x40u && addr < 0x2000u)
        return b->ram[addr];
    if (b->rom && PreviewIsLoRom(bank, addr))
        return b->rom[PreviewLoRomOffset(bank, addr, b->rom_mask)];
    return cpu_read8(b->cpu, bank, addr);
}

static void PreviewWrite(void *mem, uint32_t adr, uint8_t val)
{
    PreviewBus *b = (PreviewBus *)mem;
    uint8_t bank = (uint8_t)(adr >> 16);
    uint16_t addr = (uint16_t)adr;
    if ((bank & 0xFEu) == 0x7Eu) {
        b->ram[((uint32_t)(bank & 1u) << 16) | addr] = val;
        return;
    }
    if ((bank & 0x7Fu) < 0x40u && addr < 0x2000u) {
        b->ram[addr] = val;
        return;
    }
    cpu_write8(b->cpu, bank, addr, val);
}

static bool PreviewReadWord(void *mem, uint32_t adrl, uint32_t adrh, uint16_t *out)
{
    if (adrh != adrl + 1u) return false;
    PreviewBus *b = (PreviewBus *)mem;
    uint8_t bank = (uint8_t)(adrl >> 16);
    uint16_t addr = (uint16_t)adrl;
    if ((bank & 0xFEu) == 0x7Eu && addr < 0xFFFFu) {
        uint32_t off = ((uint32_t)(bank & 1u) << 16) | addr;
        *out = (uint16_t)b->ram[off] | ((uint16_t)b->ram[off + 1u] << 8);
        return true;
    }
    if ((bank & 0x7Fu) < 0x40u && addr < 0x1FFFu) {
        *out = (uint16_t)b->ram[addr] | ((uint16_t)b->ram[addr + 1u] << 8);
        return true;
    }
    if (b->rom && PreviewIsLoRom(bank, addr) && addr < 0xFFFFu) {
        uint32_t off = PreviewLoRomOffset(bank, addr, b->rom_mask);
        *out = (uint16_t)b->rom[off] | ((uint16_t)b->rom[off + 1u] << 8);
        return true;
    }
    return false;
}

static bool PreviewWriteWord(void *mem, uint32_t adrl, uint32_t adrh, uint16_t val, bool reversed)
{
    (void)reversed;
    if (adrh != adrl + 1u) return false;
    PreviewBus *b = (PreviewBus *)mem;
    uint8_t bank = (uint8_t)(adrl >> 16);
    uint16_t addr = (uint16_t)adrl;
    if ((bank & 0xFEu) == 0x7Eu && addr < 0xFFFFu) {
        uint32_t off = ((uint32_t)(bank & 1u) << 16) | addr;
        b->ram[off] = (uint8_t)val;
        b->ram[off + 1u] = (uint8_t)(val >> 8);
        return true;
    }
    if ((bank & 0x7Fu) < 0x40u && addr < 0x1FFFu) {
        b->ram[addr] = (uint8_t)val;
        b->ram[addr + 1u] = (uint8_t)(val >> 8);
        return true;
    }
    return false;
}

bool KdcCpuRunPreview(CpuState *cpu, uint32_t entry_pc, unsigned horizon)
{
    static int force_bridge = -1;
    if (force_bridge < 0) {
        const char *env = getenv("KDC_CPU_PREVIEW_BRIDGE");
        force_bridge = (env && env[0] == '1' && env[1] == '\0') ? 1 : 0;
    }
    if (force_bridge || !g_rom || !g_snes || !g_snes->cart ||
        g_snes->cart->type != CART_LOROM || g_snes->cart->romSize == 0 ||
        (g_snes->cart->romSize & (g_snes->cart->romSize - 1u)) != 0) {
        return interp_bridge_run(cpu, entry_pc) == 1;
    }

    PreviewBus bus = {
        .cpu = cpu,
        .ram = cpu->ram,
        .rom = g_rom,
        .rom_mask = g_snes->cart->romSize - 1u,
    };
    Interp816 in;
    memset(&in, 0, sizeof(in));
    in.mem = &bus;
    in.read = PreviewRead;
    in.write = PreviewWrite;
    in.read_word = PreviewReadWord;
    in.write_word = PreviewWriteWord;
    in.brkHookEnabled = false;

    in.a = cpu->A;
    in.x = cpu->X;
    in.y = cpu->Y;
    in.sp = cpu->S;
    in.dp = cpu->D;
    in.db = cpu->DB;
    in.c = cpu->_flag_C;
    in.z = cpu->_flag_Z;
    in.v = cpu->_flag_V;
    in.n = cpu->_flag_N;
    in.i = cpu->_flag_I;
    in.d = cpu->_flag_D;
    in.mf = cpu->m_flag;
    in.xf = cpu->x_flag;
    in.e = cpu->emulation;
    in.k = (uint8_t)((entry_pc >> 16) & 0xFFu);
    in.pc = (uint16_t)(entry_pc & 0xFFFFu);

    uint16_t s_enter = cpu->S;
    uint8_t *r = cpu->ram;
    for (unsigned steps = 0; steps < PREVIEW_STEP_CAP; ++steps) {
        uint32_t pc24 = (((uint32_t)in.k << 16) | in.pc) & 0x7FFFFFu;
        if (pc24 == 0x01AA0Eu && (r[0x6AD6] | ((unsigned)r[0x6AD7] << 8)) == 0u) {
            r[0x6AD2] = (uint8_t)horizon;
            r[0x6AD3] = (uint8_t)(horizon >> 8);
        }
        int cyc = interp816_runOpcode(&in);
        cpu->cycles += (uint64_t)cyc;
        if (in.sp > s_enter)
            return true;
        if (in.waiting || in.stopped)
            return false;
    }
    return false;
}
