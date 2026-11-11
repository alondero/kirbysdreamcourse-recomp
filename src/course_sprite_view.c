#include "course_sprite_view.h"
#include "course_sprites.h"
#include <math.h>
#include <string.h>

/* Verified USA-ROM table addresses; see docs/orbit-camera.md. */
enum {
    kObjVramBase = 0x6000,
    kBallBank = 0x87,
    kParasolBank = 0x85,
    kBallScriptsP1 = 0xab5e,
    kBallScriptsP2 = 0xc9cb,
    kBallMirrorsP1 = 0xab4e,
    kBallMirrorsP2 = 0xc9bb,
    kParasolMirrors = 0xb539,
    kParasolUpScriptsP1 = 0xb3bc,
    kParasolUpScriptsP2 = 0xb3cc,
    kParasolUpOffsets = 0xb3ff,
    kParasolTiltScriptsP1 = 0xb54d,
    kParasolTiltScriptsP2 = 0xb56d,
    kParasolTiltOffsets = 0xb5d9,
    kParasolSideScriptsP1 = 0xb6a1,
    kParasolSideScriptsP2 = 0xb6b1,
    kParasolSideOffsets = 0xb6dc,
    kPhaseStride = 9,  /* one queued script per animation phase */
    kBallPhases = 3,
};

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

/* OAM attribute bit 14: horizontal flip. */
static unsigned ObjMirror(unsigned attributes) { return (attributes >> 14) & 1; }

/* The animation phase whose graphics are resident in VRAM, or -1. The
 * matching transfer list is returned in resident when requested. */
static int ResidentPhase(const uint8_t *rom, const uint16_t *vram, unsigned bank,
                         unsigned script, unsigned phase_count, Transfers *resident)
{
    for (unsigned phase = 0; phase < phase_count; ++phase) {
        Transfers list;
        if (ReadTransfers(rom, bank, script + phase * kPhaseStride, &list) &&
            Matches(rom, vram, &list)) {
            if (resident) *resident = list;
            return (int)phase;
        }
    }
    return -1;
}

/* For a stage whose RAM direction can be stale: the direction (entry i of
 * count, i * step) whose graphics are resident and whose mirror flag
 * matches the OBJ. Directions sharing both are indistinguishable; preferred
 * wins among them, otherwise the lowest. False when none matches. */
static bool RecoverDirection(const uint8_t *rom, const uint16_t *vram, unsigned bank,
                             unsigned table, unsigned mirrors, unsigned count,
                             unsigned step, unsigned phase_count, unsigned mirror,
                             int preferred, uint8_t *direction)
{
    int first = -1;
    for (unsigned i = 0; i < count; ++i) {
        unsigned d = i * step;
        if (rom[Offset(bank, mirrors + d)] != mirror ||
            ResidentPhase(rom, vram, bank, Word(rom, bank, table + 2 * i), phase_count, NULL) < 0)
            continue;
        if ((int)d == preferred) { first = (int)d; break; }
        if (first < 0) first = (int)d;
    }
    if (first < 0) return false;
    *direction = (uint8_t)first;
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
        if (!RecoverDirection(rom, vram, kParasolBank,
                view.player ? kParasolSideScriptsP2 : kParasolSideScriptsP1, kParasolMirrors,
                8, 2, 1, ObjMirror(sprite->attributes), -1, &view.direction))
            return false;
    }
    if (view.family == KDC_SPRITE_VIEW_BALL) {
        /* $87:AB24 uploads the $D75E image and stores the $D75A mirror flag.
         * While aiming, the shot code copies $D9DE into both ($82:C98C) and
         * later clears them ($82:CAAD, $82:CB3E), so the captured pair can
         * be stale. Five images are shared by direction pairs and the mirror
         * flag separates the eight views: keep the capture only if VRAM and
         * the OBJ show it, otherwise recover the view they do show. */
        unsigned table = view.player ? kBallScriptsP2 : kBallScriptsP1;
        unsigned mirrors = view.player ? kBallMirrorsP2 : kBallMirrorsP1;
        unsigned mirror = ObjMirror(sprite->attributes);
        if (rom[Offset(kBallBank, mirrors + view.facing)] != mirror ||
            ResidentPhase(rom, vram, kBallBank, Word(rom, kBallBank, table + view.direction * 2),
                          kBallPhases, NULL) < 0) {
            if (!RecoverDirection(rom, vram, kBallBank, table, mirrors, 16, 1, kBallPhases,
                                  mirror, view.aimed ? view.aim : -1, &view.direction))
                return false;
            view.facing = view.direction;
        }
    }
    unsigned direction = (view.direction - turn) & 15;
    unsigned facing = (view.facing - turn) & 15;
    unsigned bank, table, mirror_table, offset_table = 0, phase_count = 1;
    unsigned source_index = view.direction, target_index = direction;
    unsigned source_facing = view.facing, target_facing = facing;
    switch (view.family) {
    case KDC_SPRITE_VIEW_BALL:
        bank = kBallBank; table = view.player ? kBallScriptsP2 : kBallScriptsP1;
        mirror_table = view.player ? kBallMirrorsP2 : kBallMirrorsP1;
        phase_count = kBallPhases;
        break;
    case KDC_SPRITE_VIEW_PARASOL_UP:
        bank = kParasolBank; table = view.player ? kParasolUpScriptsP2 : kParasolUpScriptsP1;
        mirror_table = kParasolMirrors; offset_table = kParasolUpOffsets;
        source_index >>= 1; target_index >>= 1;
        source_facing >>= 1; target_facing >>= 1;
        break;
    case KDC_SPRITE_VIEW_PARASOL_TILT:
        bank = kParasolBank; table = view.player ? kParasolTiltScriptsP2 : kParasolTiltScriptsP1;
        mirror_table = kParasolMirrors; offset_table = kParasolTiltOffsets;
        break;
    case KDC_SPRITE_VIEW_PARASOL_SIDE:
        bank = kParasolBank; table = view.player ? kParasolSideScriptsP2 : kParasolSideScriptsP1;
        mirror_table = kParasolMirrors; offset_table = kParasolSideOffsets;
        source_index >>= 1; target_index >>= 1;
        source_facing >>= 1; target_facing >>= 1;
        break;
    default: return false;
    }
    unsigned source_script = Word(rom, bank, table + source_index * 2);
    unsigned target_script = Word(rom, bank, table + target_index * 2);
    Transfers source, target;
    int phase = ResidentPhase(rom, vram, bank, source_script, phase_count, &source);
    if (phase < 0 || !ReadTransfers(rom, bank, target_script + phase * kPhaseStride, &target))
        return false;

    KdcOrbitSprite result = *sprite;
    memset(result.view_tiles, 0, sizeof(result.view_tiles));
    unsigned tile = sprite->attributes & 0x1ff;
    /* These verified producers use the OBJ page at VRAM $6000. Checking
     * membership in both DMA lists rejects unrelated sprites/changed art. */
    for (unsigned y = 0; y < size / 8; ++y) {
        for (unsigned x = 0; x < size / 8; ++x) {
            unsigned piece_tile = tile + y * 16 + x;
            unsigned address = kObjVramBase + piece_tile * 16;
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
