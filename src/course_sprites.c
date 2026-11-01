#include "course_sprites.h"
#include "course_view.h"
#include "cpu_state.h"
#include "snes/cart.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host bounds, independent of the 128 native OAM slots. Exceeding either
 * bound invalidates the whole list and reports it instead of truncating it. */
enum { kMaxPieces = 4096, kMaxRequests = 1024, kMaxLayoutSteps = 1024 };
typedef struct Piece {
    int16_t world_x, world_y;
    uint16_t attributes;
    uint8_t large, bucket;
    bool recovered;
} Piece;
typedef struct DrawList {
    Piece pieces[kMaxPieces];
    unsigned count;
    bool invalid;
} DrawList;

extern Snes *g_snes;
static DrawList building, displayed;
typedef struct Request {
    int16_t world_x, world_y;
    uint16_t layout;
    uint8_t bank, bucket;
    bool direct, recovered;
} Request;
static Request requests[kMaxRequests];
static unsigned request_count;
static PpuExtraObject objects[kMaxPieces];
static uint16_t pixels[kMaxPieces][64];
static unsigned left_pixels, right_pixels;
static unsigned recovered_left, recovered_right;
static FILE *diagnostic;
static bool reported_invalid;

/* Side-effect-free reads for this verified LoROM. Low WRAM mirrors in
 * bank $89 hold the game's generated layouts; never read those from ROM. */
static bool ReadByte(const CpuState *cpu, unsigned bank, uint16_t address,
                     uint8_t *value)
{
    if (bank == 0x7e || bank == 0x7f) {
        *value = cpu->ram[((bank - 0x7e) << 16) | address];
    } else if ((bank & 0x7f) < 0x40 && address < 0x2000) {
        *value = cpu->ram[address];
    } else if (address >= 0x8000 && g_snes && g_snes->cart &&
               g_snes->cart->romImageSize == 0x100000) {
        unsigned offset = ((bank & 0x7f) << 15) | (address & 0x7fff);
        if (offset >= g_snes->cart->romImageSize) return false;
        *value = g_snes->cart->rom[offset];
    } else {
        return false;
    }
    return true;
}

static bool ReadWord(const CpuState *cpu, unsigned bank, uint16_t address,
                     uint16_t *value)
{
    uint8_t lo, hi;
    if (!ReadByte(cpu, bank, address, &lo) ||
        !ReadByte(cpu, bank, (uint16_t)(address + 1), &hi)) return false;
    *value = (uint16_t)(lo | (unsigned)hi << 8);
    return true;
}

static uint16_t DirectWord(const CpuState *cpu, unsigned offset)
{
    return KdcReadWord(cpu->ram, (uint16_t)(cpu->D + offset));
}

static void Expand(const CpuState *cpu, const Request *request)
{
    if (building.invalid) return;
    unsigned bank = request->bank;
    uint16_t layout = request->layout;
    int priority = -1;
    for (unsigned step = 0; step < kMaxLayoutSteps; ++step) {
        uint8_t dy, dx, flags;
        uint16_t word;
        if (!ReadByte(cpu, bank, layout, &dy) ||
            !ReadWord(cpu, bank, (uint16_t)(layout + 1), &word)) break;
        if (dy == 0x80) { layout = word; continue; }
        if (!request->direct && dy == 0x7e) {
            priority = (KdcReadWord(cpu->ram, word) & 3) << 12;
            layout = (uint16_t)(layout + 3);
            continue;
        }
        if (request->direct && dy == 0x7f) {
            /* $80:8A45: an index in WRAM selects a linked layout. */
            uint16_t index = KdcReadWord(cpu->ram, word);
            uint16_t target;
            if (!ReadWord(cpu, bank, (uint16_t)(layout + 3 + 2 * index), &target)) break;
            layout = target;
            continue;
        }
        if (!ReadByte(cpu, bank, (uint16_t)(layout + 3), &dx) ||
            !ReadByte(cpu, bank, (uint16_t)(layout + 4), &flags)) break;
        if (building.count == kMaxPieces) break;
        Piece *piece = &building.pieces[building.count++];
        *piece = (Piece){
            (int16_t)(request->world_x + (int8_t)dx),
            (int16_t)(request->world_y + (int8_t)dy),
            (uint16_t)(priority < 0 ? word : (word & 0xcfff) | priority),
            flags & 1, request->bucket, request->recovered
        };
        if (flags & 0x80) return;
        layout = (uint16_t)(layout + 5);
    }
    building.invalid = true; /* invalid pointer, cycle, or host capacity */
}

static void Capture(const CpuState *cpu, unsigned bank, uint16_t layout,
                    int x, int y, unsigned bucket, bool direct, bool recovered)
{
    if (building.invalid) return;
    if (bucket >= 10 || request_count == kMaxRequests) {
        building.invalid = true;
        return;
    }
    Request *request = &requests[request_count++];
    *request = (Request){
        (int16_t)(x + KdcReadWord(cpu->ram, 0x86)),
        (int16_t)(y + KdcReadWord(cpu->ram, 0x88)),
        layout, (uint8_t)bank, (uint8_t)bucket, direct, recovered
    };
    /* Resolve queued layouts at $8C12, after the dynamic-layout updater.
     * A direct expander invoked later observes its memory at that call. */
    if (direct) Expand(cpu, request);
}

static bool Rejected(int x, int y, int low_x, int low_y)
{
    return x < low_x || x >= 296 || y < low_y || y >= 280;
}

void KdcObserveSprites(const CpuState *cpu, uint32_t pc)
{
    unsigned address = pc & 0xffff;
    if (address == 0x8823) {
        building.count = 0;
        building.invalid = false;
        request_count = 0;
        return;
    }
    if (address == 0x8ae1) {
        /* Unlike $8823, this clears queues without resetting the OAM
         * allocation pointer. Keep pieces already expanded into that OAM. */
        request_count = 0;
        return;
    }
    if (address == 0x8c12) {
        for (unsigned i = 0; i < request_count; ++i)
            if (!requests[i].direct) Expand(cpu, &requests[i]);
        return;
    }
    if (address == 0x82ca) {
        /* Immediately before the fixed $0600 -> OAM DMA, not at the end of
         * a host frame, when the next simulation camera can be staged. */
        displayed = building;
        if (displayed.invalid && !reported_invalid) {
            fprintf(stderr, "Course sprite presentation list rejected: invalid layout or host capacity exceeded\n");
            reported_invalid = true;
        }
        return;
    }
    if (cpu->m_flag || cpu->x_flag || cpu->emulation) return;
    unsigned bank = KdcReadWord(cpu->ram, 0x9e) & 0xff;
    uint16_t layout;
    int x, y;
    unsigned bucket;
    bool recovered = true;
    switch (address) {
    case 0xe55e: case 0xe5a6: {
        unsigned slot = DirectWord(cpu, 0xd2);
        if (slot >= 0x80 || (slot & 1)) return;
        uint16_t animation = KdcReadWord(cpu->ram, 0x5d9c + slot);
        if (animation & 0x8000) return; /* intentionally hidden/deleted */
        x = (int16_t)KdcReadWord(cpu->ram, 0x5f1c + slot);
        y = (int16_t)KdcReadWord(cpu->ram, 0x5f9c + slot);
        if (address == 0xe55e) {
            x = (int16_t)(x - KdcReadWord(cpu->ram, 0x86));
            y = (int16_t)(y - KdcReadWord(cpu->ram, 0x88));
        }
        if (!Rejected(x, y, -48, -32)) return; /* queue observer owns it */
        uint16_t table = KdcReadWord(cpu->ram, 0x5e1c + slot);
        if (!ReadWord(cpu, bank, (uint16_t)(table + 2 * animation), &layout)) {
            building.invalid = true;
            return;
        }
        bucket = KdcReadWord(cpu->ram, 0x5e9c + slot);
        break;
    }
    case 0xe60b: case 0xe635:
        x = (int16_t)cpu->X;
        y = (int16_t)cpu->Y;
        if (address == 0xe60b) {
            x = (int16_t)(x - KdcReadWord(cpu->ram, 0x86));
            y = (int16_t)(y - KdcReadWord(cpu->ram, 0x88));
        }
        if (!Rejected(x, y, -48, -32)) return;
        layout = DirectWord(cpu, 0xc0);
        bucket = cpu->A;
        break;
    case 0x9e9e: case 0x9eec:
        if (cpu->X >= 0x20 || (cpu->X & 1)) return;
        x = (int16_t)KdcReadWord(cpu->ram, 0x8de + cpu->X);
        y = (int16_t)cpu->Y;
        if (!Rejected(x, y, address == 0x9eec ? -24 : -48,
                      address == 0x9eec ? -24 : -32)) return;
        layout = DirectWord(cpu, 0xe6);
        bucket = KdcReadWord(cpu->ram, 0x2880);
        break;
    case 0x8b09:
        recovered = false;
        x = (int16_t)DirectWord(cpu, 0xc2);
        y = (int16_t)cpu->Y;
        layout = DirectWord(cpu, 0xc0);
        bucket = cpu->A;
        break;
    case 0x8a3d:
        Capture(cpu, cpu->DB, cpu->Y, (int16_t)DirectWord(cpu, 0x36),
                (int16_t)DirectWord(cpu, 0x38), 0, true, false);
        return;
    default: return;
    }
    Capture(cpu, bank, layout, x, y, bucket, false, recovered);
}

static void InterpretedObserver(CpuState *cpu, uint32_t pc)
{
    KdcObserveSprites(cpu, pc);
}

void KdcInitializeSprites(void)
{
    static const uint16_t sites[] = {
        0x8823, 0x8ae1, 0x82ca, 0x8c12, 0xe55e, 0xe5a6, 0xe60b, 0xe635,
        0x9e9e, 0x9eec, 0x8b09, 0x8a3d
    };
    for (unsigned i = 0; i < sizeof(sites) / sizeof(sites[0]); ++i) {
        if (!interp_bridge_add_pre_opcode_hook(0x800000 | sites[i], InterpretedObserver)) {
            fprintf(stderr, "Unable to register course sprite observer\n");
            abort();
        }
    }
    const char *path = getenv("KDC_SPRITE_LOG");
    if (!diagnostic && path && *path) {
        diagnostic = fopen(path, "w");
        if (!diagnostic) fprintf(stderr, "Unable to open sprite log: %s\n", path);
    }
}

static void PrepareLine(Ppu *ppu, int line, void *context)
{
    (void)context;
    static const uint8_t sizes[8][2] = {
        {8,16}, {8,32}, {8,64}, {16,32}, {16,64}, {32,64}, {16,32}, {16,32}
    };
    PpuSetExtraObjects(ppu, NULL, 0);
    PpuSetExtraObjectMargins(ppu, false);
    if (displayed.invalid || !KdcIsCourse(ppu) || PPU_objInterlace(ppu)) return;
    /* The complete recovered list supplies margin order independently of
     * native OAM slots. Native evaluation and center pixels stay intact. */
    PpuSetExtraObjectMargins(ppu, true);
    unsigned count = 0, order = 0;
    const uint16_t *vram = PpuRenderVram(ppu);
    for (unsigned bucket = 0; bucket < 10; ++bucket) {
        for (unsigned i = 0; i < displayed.count; ++i) {
            const Piece *piece = &displayed.pieces[i];
            if (piece->bucket != bucket) continue;
            ++order;
            int size = sizes[PPU_objSize(ppu)][piece->large];
            int x = (int16_t)(piece->world_x - (int16_t)ppu->hScroll[0]);
            int y = (int16_t)(piece->world_y - (int16_t)ppu->vScroll[0]);
            int row = line - y;
            if (row < 0 || row >= size || x >= 256 + ppu->extraRightCur ||
                x + size <= -ppu->extraLeftCur || (x >= 0 && x + size <= 256)) continue;
            uint16_t attr = piece->attributes;
            if (attr & 0x8000) row = size - 1 - row;
            unsigned base = attr & 0x100 ? PPU_objTileAdr2(ppu) : PPU_objTileAdr1(ppu);
            unsigned palette = 0x80 + 16 * ((attr >> 9) & 7);
            for (int column = 0; column < size; ++column) {
                pixels[count][column] = 0;
                int screen_x = x + column;
                /* The original center is always supplied by native OAM. */
                if (screen_x >= 0 && screen_x < 256) continue;
                int source_x = attr & 0x4000 ? size - 1 - column : column;
                unsigned tile = ((((attr & 0xff) >> 4) + (row >> 3)) << 4) |
                                (((attr & 15) + (source_x >> 3)) & 15);
                unsigned address = (base + tile * 16 + (row & 7)) & 0x7fff;
                uint32_t plane = vram[address] | (uint32_t)vram[(address + 8) & 0x7fff] << 16;
                unsigned shift = 7 - (source_x & 7);
                plane >>= shift;
                unsigned color = (plane & 1) | ((plane >> 7) & 2) |
                                 ((plane >> 14) & 4) | ((plane >> 21) & 8);
                if (!color) continue;
                pixels[count][column] = 0x8000 | ppu->cgram[palette + color];
                if (screen_x < 0) ++left_pixels; else ++right_pixels;
                if (piece->recovered) {
                    if (screen_x < 0) ++recovered_left; else ++recovered_right;
                }
            }
            objects[count] = (PpuExtraObject){x, line, (uint16_t)size, 1, 64,
                (attr >> 12) & 3, (attr & 0x800) != 0, 0, order, pixels[count]};
            ++count;
        }
    }
    PpuSetExtraObjects(ppu, objects, count);
}

void KdcPrepareSprites(Ppu *ppu, int extra)
{
    left_pixels = right_pixels = 0;
    recovered_left = recovered_right = 0;
    PpuSetExtraObjects(ppu, NULL, 0);
    const char *enabled = getenv("KDC_SPRITE_VIEW");
    if (enabled && !strcmp(enabled, "0")) extra = 0;
    PpuSetExtraObjectProvider(ppu, extra ? PrepareLine : NULL, NULL);
}

void KdcResetSprites(Ppu *ppu)
{
    memset(&building, 0, sizeof(building));
    memset(&displayed, 0, sizeof(displayed));
    request_count = 0;
    if (ppu) KdcPrepareSprites(ppu, 0);
}

void KdcReportSprites(int frame)
{
    if (diagnostic)
        fprintf(diagnostic, "%d %u %u %u %u %u %u %u\n", frame, building.count,
                displayed.count, displayed.invalid, left_pixels, right_pixels,
                recovered_left, recovered_right);
}
