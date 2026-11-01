/* Regression cases from the independent sprite review; synthetic data only. */
#include "course_sprites.c"
#include "snes/ppu.c"
#include <assert.h>

Snes *g_snes;
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;
static uint8_t ram[0x20000], rom[0x100000];
static InterpPreOpcodeHook hooks[65536];

bool interp_bridge_add_pre_opcode_hook(uint32_t pc, InterpPreOpcodeHook hook)
{
    hooks[pc & 0xffff] = hook;
    return true;
}

static void Word(unsigned address, uint16_t value)
{
    ram[address] = value;
    ram[address + 1] = value >> 8;
}

static void Queue(CpuState *cpu)
{
    Word(0xc0, 0x1000);
    Word(0xc2, 300);
    cpu->A = 2;
    cpu->Y = 16;
    KdcObserveSprites(cpu, 0x808b09);
}

static void QueueClear(CpuState *cpu)
{
    /* Already-expanded direct objects must survive either queue-only clear. */
    Capture(cpu, 0x89, 0x1000, -16, 16, 0, true, false);
    Queue(cpu);
    KdcObserveSprites(cpu, 0x808841);
    KdcObserveSprites(cpu, 0x808c12);
    KdcObserveSprites(cpu, 0x8082ca);
    assert(displayed.count == 1 && displayed.pieces[0].bucket == 0);
    assert(hooks[0x8841]);
    KdcObserveSprites(cpu, 0x808823);
    Queue(cpu);
    KdcObserveSprites(cpu, 0x808c12);
    unsigned count = building.count;
    KdcObserveSprites(cpu, 0x808c12);
    assert(building.count == count && request_count == 0);
}

static void DataBank(CpuState *cpu)
{
    /* Deliberately contradictory banks: DB=$7F has a visible animation and
     * different camera; $7E marks the same task hidden. DP remains bank zero. */
    Word(0xd2, 6);
    Word(0x5da2, 0xffff);
    Word(0x1009e, 0x89);
    Word(0x10086, 100);
    Word(0x10088, 20);
    Word(0x15da2, 0);
    Word(0x15e22, 0x1100);
    Word(0x1100, 0x1000);
    Word(0x15ea2, 2);
    Word(0x15f22, 500);
    Word(0x15fa2, 40);
    cpu->DB = 0x7f;
    CpuState before = *cpu;
    uint8_t before_ram[sizeof(ram)];
    memcpy(before_ram, ram, sizeof(ram));
    KdcObserveSprites(cpu, 0x80e55e);
    KdcObserveSprites(cpu, 0x808c12);
    assert(!building.invalid && building.count == 1);
    assert(building.pieces[0].world_x == 500 && building.pieces[0].world_y == 40);
    assert(!memcmp(&before, cpu, sizeof(before)));
    assert(!memcmp(before_ram, ram, sizeof(ram)));
    KdcObserveSprites(cpu, 0x808823);
    cpu->DB = 0; /* $00:5F9C is not WRAM, and must never be guessed as such. */
    KdcObserveSprites(cpu, 0x80e55e);
    assert(building.invalid && request_count == 0);
}

static void NativeMargins(Ppu *ppu)
{
    PpuSetExtraSpace(ppu, 272);
    for (unsigned i = 0; i < 128; ++i) ppu->oam[2 * i] = 0xe000;
    ppu->oam[0] = (16 << 8) | 252; /* four opaque pixels in the right margin */
    ppu->oam[2] = (16 << 8) | 250; /* unobserved native large-sprite emitter */
    ppu->highOam[0] = 8;
    ppu->vram[0] = 0x00ff;
    ppu->vram[16] = 0x00ff;
    memset(&ppu->objBuffer, 0, sizeof(ppu->objBuffer));
    assert(ppu_evaluateSprites(ppu, 16));
    PpuPixelPrioBufs native = ppu->objBuffer;
    unsigned start = kPpuExtraLeftRight + 256;
    assert(native.data[start] && native.data[start + 9]);
    PpuSetExtraObjectMargins(ppu, true);
    /* An unrepresented native emitter must still draw with an empty list. */
    memset(&ppu->objBuffer, 0, sizeof(ppu->objBuffer));
    assert(ppu_evaluateSprites(ppu, 16));
    assert(!memcmp(&native, &ppu->objBuffer, sizeof(native)));
    /* Partial coverage replaces only opaque host pixels, including black.
     * Transparent holes retain every uncovered native OBJ pixel. */
    uint16_t colors[] = {0x8000, 0x801f, 0, 0};
    PpuExtraObject host = {256, 16, 4, 1, 4, 2, 0, 0, 1, colors};
    PpuSetExtraObjects(ppu, &host, 1);
    memset(&ppu->objBuffer, 0, sizeof(ppu->objBuffer));
    assert(ppu_evaluateSprites(ppu, 16));
    assert(ppu->objectColors[start] == 0x8000);
    assert(ppu->objectColors[start + 1] == 0x801f);
    for (unsigned i = 2; i < kPpuBufWidth - start; ++i) {
        assert(ppu->objBuffer.data[start + i] == native.data[start + i]);
        assert(ppu->objectColors[start + i] == 0);
    }
    assert(!memcmp(native.data + kPpuExtraLeftRight,
                   ppu->objBuffer.data + kPpuExtraLeftRight,
                   256 * sizeof(PpuZbufType)));
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    Ppu *ppu = ppu_init();
    Cart cart = {.rom = rom, .romImageSize = sizeof(rom)};
    Snes snes = {.ppu = ppu, .cart = &cart};
    CpuState cpu = {.ram = ram, .DB = 0x7e};
    g_snes = &snes;
    Word(0x9e, 0x89);
    ram[0x1004] = 0x80; /* one small terminal piece, tile zero */
    KdcInitializeSprites();
    if (!strcmp(argv[1], "queue")) QueueClear(&cpu);
    else if (!strcmp(argv[1], "bank")) DataBank(&cpu);
    else if (!strcmp(argv[1], "margins")) NativeMargins(ppu);
    else assert(false);
    ppu_free(ppu);
    return 0;
}
