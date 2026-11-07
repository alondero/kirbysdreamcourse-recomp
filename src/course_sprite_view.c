#include "course_sprite_view.h"
#include "course_sprites.h"
#include <math.h>
#include <string.h>

typedef struct Transfer { unsigned source, destination, length; } Transfer;
typedef struct Transfers { Transfer items[4]; unsigned count; } Transfers;

static unsigned Offset(unsigned bank, unsigned address)
{ return ((bank & 0x7f) << 15) | (address & 0x7fff); }

static unsigned Word(const uint8_t *rom, unsigned bank, unsigned address)
{
    unsigned offset = Offset(bank, address);
    return rom[offset] | (unsigned)rom[offset + 1] << 8;
}

/* $80:8696 queues these eight-byte DMA records until FF. This reader
 * deliberately accepts only the mode-0 ROM-to-VRAM copies used below. */
static bool ReadTransfers(const uint8_t *rom, unsigned bank, unsigned address,
                           Transfers *result)
{
    *result = (Transfers){0};
    for (unsigned i = 0; i <= 4; ++i, address += 8) {
        if (address < 0x8000 || address > 0xfff7) return false;
        const uint8_t *p = rom + Offset(bank, address);
        if (p[0] == 0xff) return result->count != 0;
        if (i == 4 || p[0] != 0 || p[5] < 0x80 || p[5] > 0x9f) return false;
        unsigned length = p[1] | (unsigned)p[2] << 8;
        unsigned source = p[3] | (unsigned)p[4] << 8;
        unsigned destination = p[6] | (unsigned)p[7] << 8;
        if (!length || (length & 1) || source < 0x8000 ||
            length > 0x10000 - source || destination >= 0x8000 ||
            length / 2 > 0x8000 - destination) return false;
        result->items[result->count++] = (Transfer){Offset(p[5], source),
                                                   destination, length};
    }
    return false;
}

static bool Matches(const uint8_t *rom, const uint16_t *vram, const Transfers *list)
{
    for (unsigned i = 0; i < list->count; ++i) {
        const Transfer *t = &list->items[i];
        for (unsigned j = 0; j < t->length; j += 2) {
            unsigned expected = rom[t->source + j] | (unsigned)rom[t->source + j + 1] << 8;
            if (vram[t->destination + j / 2] != expected) return false;
        }
    }
    return true;
}

static unsigned TileSource(const Transfers *list, unsigned address)
{
    /* Later transfers win, matching DMA. Require the complete tile. */
    for (unsigned i = list->count; i > 0; --i) {
        const Transfer *t = &list->items[i - 1];
        if (address >= t->destination && (address - t->destination) * 2 + 32 <= t->length)
            return t->source + (address - t->destination) * 2;
    }
    return 0;
}

bool KdcApplySpriteView(const uint8_t *rom, size_t rom_size,
                        const uint16_t *vram, KdcSpriteView view, double yaw,
                        unsigned size, KdcOrbitSprite *sprite)
{
    if (!rom || rom_size != 0x100000 || !vram || !sprite ||
        !isfinite(yaw) || (size != 8 && size != 16) ||
        view.direction >= 16 || view.facing >= 16 || (view.player != 0 && view.player != 2))
        return false;
    const double pi = 3.14159265358979323846;
    int turn = (int)lround(remainder(yaw, 2 * pi) * (8 / pi));
    if (!(turn & 15)) return false;
    if (view.family == KDC_SPRITE_VIEW_PARASOL_SIDE) {
        /* $85:B62B uploads this stage once. Later motion can change D75E;
         * recover the frozen view from the latched tiles and uniform OBJ
         * mirror in body layouts 9603/9624 and attachment A144/A165. */
        unsigned table = view.player ? 0xb6b1 : 0xb6a1;
        bool found = false;
        for (unsigned d = 0; d < 16; d += 2) {
            Transfers candidate;
            if (rom[Offset(0x85, 0xb539 + d)] != ((sprite->attributes >> 14) & 1)) continue;
            if (!ReadTransfers(rom, 0x85, Word(rom, 0x85, table + d), &candidate) ||
                !Matches(rom, vram, &candidate)) continue;
            view.direction = (uint8_t)d;
            found = true;
            break;
        }
        if (!found) return false;
    }
    unsigned direction = (view.direction - turn) & 15;
    unsigned facing = (view.facing - turn) & 15;
    unsigned bank, table, mirror_table, offset_table = 0, phase_count = 1;
    unsigned source_index = view.direction, target_index = direction;
    unsigned source_facing = view.facing, target_facing = facing;
    switch (view.family) {
    case KDC_SPRITE_VIEW_BALL:
        bank = 0x87; table = view.player ? 0xc9cb : 0xab5e;
        mirror_table = view.player ? 0xc9bb : 0xab4e;
        phase_count = 3;
        break;
    case KDC_SPRITE_VIEW_PARASOL_UP:
        bank = 0x85; table = view.player ? 0xb3cc : 0xb3bc;
        mirror_table = 0xb539; offset_table = 0xb3ff;
        source_index >>= 1; target_index >>= 1;
        source_facing >>= 1; target_facing >>= 1;
        break;
    case KDC_SPRITE_VIEW_PARASOL_TILT:
        bank = 0x85; table = view.player ? 0xb56d : 0xb54d;
        mirror_table = 0xb539; offset_table = 0xb5d9;
        break;
    case KDC_SPRITE_VIEW_PARASOL_SIDE:
        bank = 0x85; table = view.player ? 0xb6b1 : 0xb6a1;
        mirror_table = 0xb539; offset_table = 0xb6dc;
        source_index >>= 1; target_index >>= 1;
        source_facing >>= 1; target_facing >>= 1;
        break;
    default: return false;
    }
    unsigned source_script = Word(rom, bank, table + source_index * 2);
    unsigned target_script = Word(rom, bank, table + target_index * 2);
    Transfers source, target;
    unsigned phase;
    for (phase = 0; phase < phase_count; ++phase)
        if (ReadTransfers(rom, bank, source_script + phase * 9, &source) &&
            Matches(rom, vram, &source)) break;
    if (phase == phase_count || !ReadTransfers(rom, bank, target_script + phase * 9, &target))
        return false;

    KdcOrbitSprite result = *sprite;
    memset(result.view_tiles, 0, sizeof(result.view_tiles));
    unsigned tile = sprite->attributes & 0x1ff;
    /* These verified producers use the OBJ page at VRAM $6000. Checking
     * membership in both DMA lists rejects unrelated sprites/changed art. */
    for (unsigned y = 0; y < size / 8; ++y) {
        for (unsigned x = 0; x < size / 8; ++x) {
            unsigned piece_tile = tile + y * 16 + x;
            unsigned address = 0x6000 + piece_tile * 16;
            unsigned offset = TileSource(&target, address);
            unsigned player_tile = piece_tile - (view.player ? 0x20 : 0);
            bool static_canopy = view.attachment &&
                (view.family == KDC_SPRITE_VIEW_PARASOL_UP || view.family == KDC_SPRITE_VIEW_PARASOL_SIDE) &&
                ((player_tile >= 0x1c9 && player_tile <= 0x1ce) ||
                 (player_tile >= 0x1d9 && player_tile <= 0x1de));
            /* UP/SIDE scripts only upload Kirby; their A144/A165 canopy
             * uses resident tiles. It still changes mirror and placement. */
            if (!offset || !TileSource(&source, address)) {
                if (!static_canopy) return false;
                offset = 0;
            }
            result.view_tiles[y * (size / 8) + x] = offset;
        }
    }
    unsigned old_mirror = view.family == KDC_SPRITE_VIEW_BALL ? view.facing : view.direction;
    unsigned new_mirror = view.family == KDC_SPRITE_VIEW_BALL ? facing : direction;
    if (view.family == KDC_SPRITE_VIEW_PARASOL_UP || view.family == KDC_SPRITE_VIEW_PARASOL_SIDE) {
        old_mirror &= 14; new_mirror &= 14;
    }
    if (rom[Offset(bank, mirror_table + old_mirror)] != rom[Offset(bank, mirror_table + new_mirror)]) {
        result.offset_x = 2 * result.motion_x - result.offset_x - size;
        result.attributes ^= 0x4000;
    }
    if (view.attachment && offset_table) {
        int dx = (int16_t)Word(rom, bank, offset_table + 4 * target_facing) -
                 (int16_t)Word(rom, bank, offset_table + 4 * source_facing);
        int dy = (int16_t)Word(rom, bank, offset_table + 4 * target_facing + 2) -
                 (int16_t)Word(rom, bank, offset_table + 4 * source_facing + 2);
        result.offset_x += dx; result.offset_y += dy;
        result.motion_x += dx; result.motion_y += dy;
        /* $85:B58D moves a tilted canopy across Kirby as its facing
         * crosses directions 3 and 10. The other two stages stay in front. */
        result.view_order = view.family == KDC_SPRITE_VIEW_PARASOL_TILT &&
            !(target_facing >= 3 && target_facing < 10) ? -1 : 1;
    }
    *sprite = result;
    return true;
}
