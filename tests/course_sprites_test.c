/* Synthetic layouts only: exercise recovery and the real OBJ compositor. */
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

static void Layout(unsigned address, int dy, unsigned attr, int dx, unsigned flags)
{
    ram[address] = dy;
    Word(address + 1, attr);
    ram[address + 3] = dx;
    ram[address + 4] = flags;
}

static void Queue(CpuState *cpu, int x, int y, unsigned layout, unsigned bucket)
{
    Word(0xc0, layout);
    Word(0xc2, x);
    cpu->Y = y;
    cpu->A = bucket;
    KdcObserveSprites(cpu, 0x808b09);
}

static void Finish(CpuState *cpu)
{
    KdcObserveSprites(cpu, 0x808c12);
    KdcObserveSprites(cpu, 0x8082ca);
}

int main(void)
{
    Ppu *ppu = ppu_init();
    Cart cart = {.rom = rom, .romImageSize = sizeof(rom)};
    Snes snes = {.ppu = ppu, .cart = &cart};
    CpuState cpu = {.ram = ram};
    g_snes = &snes;
    assert(ppu);
    KdcInitializeSprites();
    assert(hooks[0xe55e] && hooks[0x8c12] && hooks[0x82ca]);
    ppu->bgmode = 1;
    ppu->bgXsc[0] = 0x51;
    ppu->bgXsc[1] = 0x59;
    PpuSetExtraSpace(ppu, 272);
    Word(0x9e, 0x89);

    /* A queued RAM layout is resolved after the game's layout updater. */
    Layout(0x1000, 0, 0x0800, 0, 0x80);
    Queue(&cpu, -16, 16, 0x1000, 2);
    Word(0x1001, 0x3800);
    KdcObserveSprites(&cpu, 0x808c12);
    assert(building.count == 1 && building.pieces[0].attributes == 0x3800);
    assert(displayed.count == 0); /* no early presentation */
    KdcObserveSprites(&cpu, 0x8082ca);
    assert(displayed.count == 1);
    KdcObserveSprites(&cpu, 0x808823);
    assert(building.count == 0 && displayed.count == 1);
    /* Queue-only clearing cannot delete pieces already written to OAM. */
    Capture(&cpu, 0x89, 0x1000, -16, 16, 0, true, false);
    Queue(&cpu, 300, 16, 0x1000, 2);
    KdcObserveSprites(&cpu, 0x808ae1);
    Finish(&cpu);
    assert(displayed.count == 1 && displayed.pieces[0].bucket == 0);
    KdcObserveSprites(&cpu, 0x808823);

    /* Culled task callback: observe after its position updater, including
     * hidden animation state and side-effect-free bank $89 WRAM mirrors. */
    Word(0xd2, 6);
    Word(0x5d9c + 6, 2);
    Word(0x5e1c + 6, 0x1100);
    Word(0x1104, 0x1000);
    Word(0x5e9c + 6, 3);
    Word(0x5f1c + 6, -100);
    Word(0x5f9c + 6, 40);
    uint8_t before[sizeof(ram)];
    memcpy(before, ram, sizeof(ram));
    CpuState original = cpu;
    hooks[0xe55e](&cpu, 0x00e55e);
    assert(!memcmp(before, ram, sizeof(ram)) && !memcmp(&original, &cpu, sizeof(cpu)));
    Finish(&cpu);
    assert(displayed.count == 1 && displayed.pieces[0].world_x == -100);
    KdcObserveSprites(&cpu, 0x808823);
    Word(0x5d9c + 6, 0xffff);
    hooks[0xe55e](&cpu, 0x80e55e);
    Finish(&cpu);
    assert(displayed.count == 0);

    /* Linked layouts, changed dynamic priority, signed offsets, and no
     * native Y/OAM allocation gates (including >128 pieces). */
    KdcObserveSprites(&cpu, 0x808823);
    ram[0x1200] = 0x7e; Word(0x1201, 0x1400); Word(0x1400, 2);
    ram[0x1203] = 0x80; Word(0x1204, 0x1300);
    Layout(0x1300, -80, 0x7800, -8, 0x81);
    for (unsigned i = 0; i < 129; ++i) Queue(&cpu, 300, 10, 0x1200, 2);
    Finish(&cpu);
    assert(!displayed.invalid && displayed.count == 129);
    assert(displayed.pieces[0].world_y == -70);
    assert(displayed.pieces[0].world_x == 292);
    assert(displayed.pieces[0].attributes == 0x6800);

    /* Bound malformed cycles instead of following forever. Invalid lists
     * are rejected as a whole and cannot display a partial stale frame. */
    KdcObserveSprites(&cpu, 0x808823);
    ram[0x1500] = 0x80; Word(0x1501, 0x1500);
    Queue(&cpu, -16, 10, 0x1500, 1);
    Finish(&cpu);
    assert(displayed.invalid);
    KdcPrepareSprites(ppu, 272);
    PrepareLine(ppu, 10, NULL);
    assert(ppu->extraObjectCount == 0);

    /* Raster-time palette/VRAM, opaque black, flips, original-center mask,
     * priority/math classification, and camera alignment on moving lines. */
    KdcObserveSprites(&cpu, 0x808823);
    Word(0x86, 400); Word(0x88, 200);
    Layout(0x1000, 0, 0x2800, 0, 0x80);
    Queue(&cpu, -4, 16, 0x1000, 2);
    Queue(&cpu, 260, 16, 0x1000, 3);
    Finish(&cpu);
    ppu->hScroll[0] = 400; ppu->vScroll[0] = 200;
    ppu->vram[0] = 0x00ff; /* 4bpp color 1 across row zero */
    ppu->cgram[0xc1] = 0; /* opaque black, OBJ palette 4 */
    KdcPrepareSprites(ppu, 272);
    ppu->extraObjectProvider(ppu, 16, NULL);
    assert(ppu->extraObjectCount == 2);
    assert(objects[0].x == -4 && objects[0].math == 1 && objects[0].priority == 2);
    assert(pixels[0][0] == 0x8000 && pixels[0][4] == 0);
    assert(left_pixels == 4 && right_pixels == 8);
    ppu->cgram[0xc1] = 0x1f;
    ppu->hScroll[0] = 402;
    PrepareLine(ppu, 16, NULL);
    assert(objects[0].x == -6 && pixels[0][0] == 0x801f);
    ppu->vram[0] = 0;
    PrepareLine(ppu, 16, NULL);
    assert(pixels[0][0] == 0);
    KdcPrepareSprites(ppu, 0);
    assert(!ppu->extraObjectProvider && !ppu->extraObjectCount);
    KdcResetSprites(ppu);
    assert(!displayed.count && !building.count && !request_count);

    /* Exercise actual native/extra composition ordering and opacity. */
    uint16_t color[] = {0x8000, 0x801f};
    PpuExtraObject fixture[] = {
        {-1, 5, 2, 1, 2, 1, 1, 0, 2, color},
        {-1, 5, 2, 1, 2, 3, 0, 0, 1, color}
    };
    uint16_t depth[4] = {0}, colors[4] = {0};
    uint64_t order[4] = {UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX};
    assert(PpuComposeExtraObjects(fixture, 2, 5, -2, 4, 0, depth, colors, order));
    assert(colors[1] == 0x8000 && colors[2] == 0x801f && order[1] == 1);
    assert((depth[1] >> 8) == SPRITE_PRIO_TO_PRIO(3, 1));

    /* A full host margin list must win independently of native slot zero,
     * while native center pixels and hardware overflow flags are retained. */
    for (unsigned i = 0; i < 128; ++i) {
        ppu->oam[2 * i] = 0xe000;
        ppu->oam[2 * i + 1] = 0;
    }
    ppu->hScroll[0] = ppu->vScroll[0] = 0;
    ppu->oam[0] = (16 << 8) | 252;
    ppu->vram[0] = 0x00ff;
    memset(&ppu->objBuffer, 0, sizeof(ppu->objBuffer));
    assert(ppu_evaluateSprites(ppu, 16));
    uint16_t native_center[256];
    memcpy(native_center, ppu->objBuffer.data + kPpuExtraLeftRight, sizeof(native_center));
    bool native_range = ppu->rangeOver, native_time = ppu->timeOver;
    uint16_t margin_color[] = {0x801f,0x801f,0x801f,0x801f};
    PpuExtraObject margin = {256,16,4,1,4,2,0,0,1,margin_color};
    PpuSetExtraObjects(ppu, &margin, 1);
    PpuSetExtraObjectMargins(ppu, true);
    memset(&ppu->objBuffer, 0, sizeof(ppu->objBuffer));
    assert(ppu_evaluateSprites(ppu, 16));
    assert(!memcmp(native_center, ppu->objBuffer.data + kPpuExtraLeftRight, sizeof(native_center)));
    assert(ppu->objectColors[kPpuExtraLeftRight + 256] == 0x801f);
    assert(ppu->rangeOver == native_range && ppu->timeOver == native_time);
    PpuSetExtraObjectProvider(ppu, PrepareLine, NULL);
    ppu_reset(ppu);
    assert(!ppu->extraObjectProvider && !ppu->extraObjectCount);
    ppu_free(ppu);
    return 0;
}
