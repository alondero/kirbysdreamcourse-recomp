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
typedef struct OrbitAnchor {
    double x, y, z;
    int16_t origin_x, origin_y;
    uint32_t source_pc;
    bool world, unresolved, ground;
} OrbitAnchor;
typedef struct Piece {
    int16_t world_x, world_y;
    uint16_t attributes;
    uint8_t large, bucket;
    bool recovered;
    OrbitAnchor anchor;
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
    OrbitAnchor anchor;
} Request;
static Request requests[kMaxRequests];
static unsigned request_count;
static PpuExtraObject objects[kMaxPieces];
static uint16_t pixels[kMaxPieces][64];
static unsigned left_pixels, right_pixels;
static unsigned recovered_left, recovered_right;
static FILE *diagnostic;
static FILE *orbit_diagnostic;
static bool reported_invalid;
static OrbitAnchor projections[1024], task_anchors[64], pending_anchor;
static unsigned projection_count, projection_next;
static uint16_t anchor_map;
static bool pending_anchor_valid;
static int16_t pending_x, pending_y;
static uint32_t source_pc;
static void SetPendingAnchor(OrbitAnchor anchor, int x, int y);
static OrbitAnchor ResolveAnchor(const CpuState *cpu, int x, int y);
static OrbitAnchor EnemyBaseAnchor(const CpuState *cpu, unsigned slot);

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

static uint16_t AbsoluteWord(const CpuState *cpu, uint16_t address)
{
    uint16_t value = 0;
    /* Absolute operands use DB; DP operands always use bank zero. Reject
     * unsupported mappings rather than guessing WRAM or touching MMIO. */
    if (!ReadWord(cpu, cpu->DB, address, &value)) building.invalid = true;
    return value;
}

static double Fixed(const uint8_t *ram, unsigned low, unsigned high)
{
    return (int16_t)KdcReadWord(ram, high) + KdcReadWord(ram, low) / 65536.0;
}

static double Elevation(double physical)
{
    /* One terrain elevation step is $5A82 / 65536 physical units. */
    return physical * (65536.0 / 23170.0);
}

static void CheckAnchorMap(const uint8_t *ram)
{
    uint16_t map = KdcReadWord(ram, 0xabb2);
    if (map == anchor_map) return;
    anchor_map = map;
    projection_count = projection_next = 0;
    memset(task_anchors, 0, sizeof(task_anchors));
    pending_anchor_valid = false;
}

void KdcObserveProjection(const CpuState *cpu, uint32_t pc)
{
    unsigned site = pc & 0x7fffff;
    if (cpu->m_flag || cpu->x_flag || cpu->emulation) return;
    CheckAnchorMap(cpu->ram);
    if (site == 0x05977a) {
        unsigned slot = DirectWord(cpu, 0xd2);
        if (slot >= 128 || (slot & 1)) return;
        OrbitAnchor anchor = EnemyBaseAnchor(cpu, slot);
        if (!anchor.world)
            anchor = ResolveAnchor(cpu, (int16_t)AbsoluteWord(cpu, 0x5f1c + slot),
                                   (int16_t)AbsoluteWord(cpu, 0x5f9c + slot));
        if (!anchor.world && task_anchors[slot / 2].world) anchor = task_anchors[slot / 2];
        anchor.source_pc = pc;
        if (anchor.world) {
            anchor.origin_x = cpu->X; anchor.origin_y = cpu->Y;
            anchor.z = ((int16_t)AbsoluteWord(cpu, 0x8b9e) -
                        16 * (anchor.y - anchor.x) - (int16_t)cpu->Y) / 16.0;
            anchor.ground = true; anchor.source_pc = pc;
        }
        SetPendingAnchor(anchor, cpu->X, cpu->Y);
        return;
    }
    if (site == 0x01aefc || site == 0x01af2d || site == 0x01af58 ||
        site == 0x01ae70 || site == 0x01aea1) {
        bool landing = site == 0x01ae70 || site == 0x01aea1;
        unsigned i = 2u * DirectWord(cpu, landing ? 0 : 2);
        if (i >= (landing ? 6u : 1024u)) return;
        int x = (int16_t)(DirectWord(cpu, 0xc2) + AbsoluteWord(cpu, 0x86));
        int y = (int16_t)(cpu->Y + AbsoluteWord(cpu, 0x88));
        OrbitAnchor anchor = {
            Fixed(cpu->ram, (landing ? 0x6b0c : 0xb746) + i,
                           (landing ? 0x6b12 : 0xbb46) + i),
            Fixed(cpu->ram, (landing ? 0x6b18 : 0xbf46) + i,
                           (landing ? 0x6b1e : 0xc346) + i),
            0, x, y, pc, true, false, false
        };
        if (landing) {
            /* The marker renderer retains U/V and its projected origin;
             * some creation paths do not retain Z. With U/V known, height
             * is determined up to the original one-pixel raster rounding. */
            int projected_y = (int16_t)(AbsoluteWord(cpu, 0x8b9a) -
                                        AbsoluteWord(cpu, 0x6b00 + i));
            anchor.z = ((int16_t)AbsoluteWord(cpu, 0x8b9e) -
                        16 * (anchor.y - anchor.x) - projected_y) / 16.0;
        } else {
            bool ground = site == 0x01af58;
            anchor.ground = ground;
            anchor.z = Elevation(Fixed(cpu->ram, (ground ? 0xcf46 : 0xc746) + i,
                                                 (ground ? 0xd346 : 0xcb46) + i));
        }
        SetPendingAnchor(anchor, x, y);
        return;
    }
    if (site != 0x01a5cc) return;
    unsigned d = cpu->D;
    if (d > 0xffb4) return;
    OrbitAnchor anchor = {
        Fixed(cpu->ram, d + 0x40, d + 0x42),
        Fixed(cpu->ram, d + 0x44, d + 0x46),
        Elevation(Fixed(cpu->ram, d + 0x48, d + 0x4a)),
        (int16_t)AbsoluteWord(cpu, 0x6a82),
        (int16_t)(AbsoluteWord(cpu, 0x8b9a) - AbsoluteWord(cpu, 0x6a86)),
        pc, true, false, false
    };
    projections[projection_next] = anchor;
    projection_next = (projection_next + 1) % 1024;
    if (projection_count < 1024) ++projection_count;
}

static OrbitAnchor ResolveAnchor(const CpuState *cpu, int x, int y)
{
    const uint8_t *ram = cpu->ram;
    CheckAnchorMap(ram);
    OrbitAnchor result = {.origin_x = x, .origin_y = y, .source_pc = source_pc,
                          .unresolved = true};
    if (cpu->DB == 0x7f) return result;
    /* Ball and its shadow are resolved by their separate live projected
     * origins. Prefer actual ball height when the two happen to coincide. */
    for (unsigned player = 0; player < 4; player += 2) {
        if ((int16_t)KdcReadWord(ram, 0x87e + player) < 0) continue;
        int bx = (int16_t)KdcReadWord(ram, 0x91e + player);
        int by = (int16_t)KdcReadWord(ram, 0x93e + player);
        if (x == bx && y == by) {
            result.x = Fixed(ram, 0xd8aa + player, 0xd8ae + player);
            result.y = Fixed(ram, 0xd8b2 + player, 0xd8b6 + player);
            result.z = Elevation(Fixed(ram, 0xd8ba + player, 0xd8be + player));
            result.world = true; result.unresolved = false;
            return result;
        }
    }
    /* Initial map objects can keep drawing without rerunning projection.
     * Only use this source while its current task origin still agrees. */
    for (unsigned i = 0; i < 64; i += 2) {
        if (KdcReadWord(ram, 0xb4b8 + i) == 0xffff) break;
        unsigned slot = KdcReadWord(ram, 0xb4f8 + i);
        if (slot >= 128 || (slot & 1) ||
            (int16_t)KdcReadWord(ram, 0x4e1c + slot) < 0 ||
            x != (int16_t)KdcReadWord(ram, 0xb638 + i) ||
            y != (int16_t)KdcReadWord(ram, 0xb678 + i)) continue;
        double u = KdcReadWord(ram, 0xb538 + i), v = KdcReadWord(ram, 0xb578 + i);
        double z = Fixed(ram, 0xb5b8 + i, 0xb5f8 + i);
        double projected_x = 32 * (u + v) + (int16_t)KdcReadWord(ram, 0x8b9c);
        double projected_y = (int16_t)KdcReadWord(ram, 0x8b9e) - 16 * (v - u) - z * (11586.0 / 256);
        if (projected_x - x < -1 || projected_x - x > 1 ||
            projected_y - y < -1 || projected_y - y > 1) continue;
        /* Map entities are stored at the cell corner; their layouts and
         * ground-shadow helper place the visual center 32 pixels to the
         * right ($85:9772). Rotate that half-cell ground displacement. */
        result.x = u + 0.5; result.y = v + 0.5; result.z = Elevation(z);
        result.origin_x = (int16_t)(x + 32);
        result.world = true; result.unresolved = false;
        return result;
    }
    for (unsigned n = 0; n < projection_count; ++n) {
        unsigned i = (projection_next + 1023 - n) % 1024;
        if (projections[i].origin_x == x && projections[i].origin_y == y) {
            result = projections[i]; result.source_pc = source_pc;
            return result;
        }
    }
    return result;
}

static void SetPendingAnchor(OrbitAnchor anchor, int x, int y)
{
    pending_anchor = anchor;
    pending_x = x; pending_y = y;
    pending_anchor_valid = true;
}

static OrbitAnchor EnemyBaseAnchor(const CpuState *cpu, unsigned slot)
{
    OrbitAnchor anchor = {.source_pc = source_pc, .unresolved = true};
    if (slot >= 128 || (slot & 1) || AbsoluteWord(cpu, 0x4e1c + slot) != 0x49) return anchor;
    /* The $85:91xx enemy animator retains its ground origin in $5A1C/$5A9C
     * while bobbing/raising the $5F1C/$5F9C draw origin. Match that saved
     * base to this task's registry entry; a projected XY cache alone cannot
     * identify an airborne enemy's ground position. */
    int x = (int16_t)AbsoluteWord(cpu, 0x5a1c + slot);
    int y = (int16_t)AbsoluteWord(cpu, 0x5a9c + slot);
    for (unsigned i = 0; i < 64; i += 2) {
        if (AbsoluteWord(cpu, 0xb4b8 + i) == 0xffff) break;
        if (AbsoluteWord(cpu, 0xb4f8 + i) != slot ||
            x != (int16_t)AbsoluteWord(cpu, 0xb638 + i) ||
            y != (int16_t)AbsoluteWord(cpu, 0xb678 + i)) continue;
        double u = AbsoluteWord(cpu, 0xb538 + i), v = AbsoluteWord(cpu, 0xb578 + i);
        double physical_z = Fixed(cpu->ram, 0xb5b8 + i, 0xb5f8 + i);
        double px = 32 * (u + v) + (int16_t)AbsoluteWord(cpu, 0x8b9c);
        double py = (int16_t)AbsoluteWord(cpu, 0x8b9e) - 16 * (v - u) - physical_z * (11586.0 / 256);
        if (px - x < -1 || px - x > 1 || py - y < -1 || py - y > 1) continue;
        anchor.x = u + 0.5; anchor.y = v + 0.5; anchor.z = Elevation(physical_z);
        anchor.origin_x = (int16_t)(x + 32); anchor.origin_y = y;
        anchor.world = true; anchor.unresolved = false;
        break;
    }
    return anchor;
}

static OrbitAnchor BallAnchor(const CpuState *cpu, unsigned player)
{
    OrbitAnchor anchor = {.source_pc = source_pc, .unresolved = true};
    if (player > 2 || (player & 1)) return anchor;
    anchor.x = Fixed(cpu->ram, 0xd8aa + player, 0xd8ae + player);
    anchor.y = Fixed(cpu->ram, 0xd8b2 + player, 0xd8b6 + player);
    anchor.z = Elevation(Fixed(cpu->ram, 0xd8ba + player, 0xd8be + player));
    anchor.origin_x = (int16_t)AbsoluteWord(cpu, 0x91e + player);
    anchor.origin_y = (int16_t)AbsoluteWord(cpu, 0x93e + player);
    anchor.world = true; anchor.unresolved = false;
    return anchor;
}

static OrbitAnchor RecoverEffectAnchor(const CpuState *cpu, unsigned slot)
{
    OrbitAnchor anchor = {.source_pc = source_pc, .unresolved = true};
    if (slot >= 128 || (slot & 1)) return anchor;
    if (task_anchors[slot / 2].world) return task_anchors[slot / 2];
    unsigned type = AbsoluteWord(cpu, 0x4e1c + slot);
    if (type == 0x5d && AbsoluteWord(cpu, 0x519c + slot) == 0xf04b &&
        (AbsoluteWord(cpu, 0x521c + slot) & 0x7f) == 4) {
        anchor = EnemyBaseAnchor(cpu, AbsoluteWord(cpu, 0x531c + slot));
    } else if (type == 0xbe && AbsoluteWord(cpu, 0xd798) == 0) {
        unsigned root = AbsoluteWord(cpu, 0x589c + slot) == 0 ? slot :
                        AbsoluteWord(cpu, 0x581c + slot);
        /* The two BE creators suspend gameplay until the root increments
         * D798 at $85:E1EF. Ball zero's physical/projected coordinates stay
         * frozen through this animation, so a loaded state needs no host
         * spawn history. Validate the live root before recovering it. */
        if (root < 128 && !(root & 1) &&
            AbsoluteWord(cpu, 0x4e1c + root) == 0xbe &&
            AbsoluteWord(cpu, 0x589c + root) == 0 &&
            (AbsoluteWord(cpu, 0x501c + root) & 0x7f) == 5 &&
            AbsoluteWord(cpu, 0x4f9c + root) >= 0xe0b7 &&
            AbsoluteWord(cpu, 0x4f9c + root) <= 0xe1ef)
            anchor = BallAnchor(cpu, 0);
    }
    if (anchor.world) task_anchors[slot / 2] = anchor;
    return anchor;
}

static bool ShotHudCaller(uint32_t pc)
{
    /* $84:9079 draws the shot meter/crosshair relative to the fixed HUD
     * origin $6B40/$6B42. $912E draws its remaining shot indicators. */
    switch (pc & 0x7fffff) {
    case 0x04902a: case 0x049049: case 0x049068: case 0x0490b6:
    case 0x0490e8: case 0x0490ff: case 0x04910b: case 0x049129:
    case 0x049192: return true;
    default: return false;
    }
}

static bool ScorecardTask(const CpuState *cpu, unsigned task)
{
    if (task >= 128 || (task & 1) ||
        (AbsoluteWord(cpu, 0x511c + task) & 0x7f) != 3) return false;
    unsigned type = AbsoluteWord(cpu, 0x4e1c + task);
    unsigned callback = AbsoluteWord(cpu, 0x509c + task);
    /* $83:D081 takes fixed scorecard coordinates from ROM tables and
     * creates type8C digits. Its type8D children animate those same digits
     * at $D296; neither path subtracts the course camera. */
    return (type == 0x8c && callback == 0xd177) ||
           (type == 0x8d && callback == 0xd296);
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
            flags & 1, request->bucket, request->recovered, request->anchor
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
        .world_x = (int16_t)(x + AbsoluteWord(cpu, 0x86)),
        .world_y = (int16_t)(y + AbsoluteWord(cpu, 0x88)),
        .layout = layout, .bank = (uint8_t)bank, .bucket = (uint8_t)bucket,
        .direct = direct, .recovered = recovered
    };
    request->anchor = ResolveAnchor(cpu, request->world_x, request->world_y);
    unsigned task = DirectWord(cpu, 0xd2);
    bool used_pending = false;
    if (direct) {
        request->anchor.world = request->anchor.unresolved = false;
    } else if (pending_anchor_valid && pending_x == request->world_x &&
               pending_y == request->world_y) {
        request->anchor = pending_anchor;
        used_pending = true;
    }
    pending_anchor_valid = false;
    /* A translated dispatch can reach the same queue observer again through
     * fallback without another producer hook. Keep the producer's anchor
     * for an immediately repeated identical request; retain both draws. */
    if (!used_pending && !direct && request_count > 1) {
        const Request *previous = &requests[request_count - 2];
        if (previous->world_x == request->world_x && previous->world_y == request->world_y &&
            previous->layout == layout && previous->bank == bank && previous->bucket == bucket &&
            previous->anchor.source_pc == request->anchor.source_pc)
            request->anchor = previous->anchor;
    }
    if (task < 128 && !(task & 1) && task == AbsoluteWord(cpu, 0x6b4c) &&
        AbsoluteWord(cpu, 0x4e1c + task) == 0x12) {
        /* $81:BF18 seeds the floating shot-number label from the active
         * ball's screen position (the stock creation path is single-player).
         * Its bob/rise remains a billboard. */
        request->anchor = BallAnchor(cpu, AbsoluteWord(cpu, 0x1803));
    } else if (ShotHudCaller(source_pc) || ScorecardTask(cpu, task) ||
               (task < 128 && !(task & 1) && AbsoluteWord(cpu, 0x519c + task) == 0x99fa &&
                (AbsoluteWord(cpu, 0x521c + task) & 0x7f) == 4)) {
        request->anchor.world = request->anchor.unresolved = false;
    }
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
    source_pc = pc;
    unsigned address = pc & 0xffff;
    if ((pc & 0x7fffff) == 0x04efc1 && cpu->Y < 128 && !(cpu->Y & 1)) {
        /* Enemy reward marker: $84:EFD0 copies its parent task's origin,
         * adds (+33,-10), then animates upward entirely in screen pixels. */
        unsigned parent = AbsoluteWord(cpu, 0x531c + cpu->Y);
        OrbitAnchor anchor = EnemyBaseAnchor(cpu, parent);
        if (!anchor.world && parent < 128 && !(parent & 1))
            anchor = task_anchors[parent / 2];
        task_anchors[cpu->Y / 2] = anchor;
        return;
    }
    if ((pc & 0x7fffff) == 0x00e525 && cpu->Y < 128 && !(cpu->Y & 1)) {
        int x = (int16_t)AbsoluteWord(cpu, 0x5f1c + cpu->Y);
        int y = (int16_t)AbsoluteWord(cpu, 0x5f9c + cpu->Y);
        OrbitAnchor anchor = ResolveAnchor(cpu, x, y);
        /* Both stock type-BE creators ($82:E230 and $87:E8E7) explicitly
         * copy ball zero's origin. Save it before the radial animation and
         * its E776 clones move, including when the ball actor is hidden. */
        if (AbsoluteWord(cpu, 0x4e1c + cpu->Y) == 0xbe &&
            x == (int16_t)AbsoluteWord(cpu, 0x91e) &&
            y == (int16_t)AbsoluteWord(cpu, 0x93e))
            anchor = BallAnchor(cpu, 0);
        task_anchors[cpu->Y / 2] = anchor;
        return;
    }
    if (address == 0xdfc0 && cpu->Y < 128 && !(cpu->Y & 1)) {
        task_anchors[cpu->Y / 2] = (OrbitAnchor){0};
        return;
    }
    if (address == 0xe776 && cpu->X < 128 && cpu->Y < 128 &&
        !(cpu->X & 1) && !(cpu->Y & 1)) {
        task_anchors[cpu->Y / 2] = task_anchors[cpu->X / 2];
        return;
    }
    if (address == 0x8823) {
        building.count = 0;
        building.invalid = false;
        request_count = 0;
        pending_anchor_valid = false;
        return;
    }
    if (address == 0x8841 || address == 0x8ae1) {
        /* Unlike $8823, this clears queues without resetting the OAM
         * allocation pointer. Keep pieces already expanded into that OAM. */
        request_count = 0;
        return;
    }
    if (address == 0x8c12) {
        for (unsigned i = 0; i < request_count; ++i)
            if (!requests[i].direct) Expand(cpu, &requests[i]);
        /* Each request needs only one presentation copy, even if the game
         * consumes the same native queue again before clearing its headers. */
        request_count = 0;
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
    unsigned bank = AbsoluteWord(cpu, 0x9e) & 0xff;
    uint16_t layout;
    int x, y;
    unsigned bucket;
    bool recovered = true;
    switch (address) {
    case 0xe55e: case 0xe5a6: {
        unsigned slot = DirectWord(cpu, 0xd2);
        if (slot >= 0x80 || (slot & 1)) return;
        uint16_t animation = AbsoluteWord(cpu, 0x5d9c + slot);
        if (animation & 0x8000) return; /* intentionally hidden/deleted */
        x = (int16_t)AbsoluteWord(cpu, 0x5f1c + slot);
        y = (int16_t)AbsoluteWord(cpu, 0x5f9c + slot);
        if (address == 0xe55e) {
            OrbitAnchor anchor = EnemyBaseAnchor(cpu, slot);
            if (!anchor.world && (AbsoluteWord(cpu, 0x4e1c + slot) == 0xbe ||
                                  AbsoluteWord(cpu, 0x4e1c + slot) == 0x5d))
                anchor = RecoverEffectAnchor(cpu, slot);
            if (!anchor.world) anchor = ResolveAnchor(cpu, x, y);
            if (slot == AbsoluteWord(cpu, 0x6b4e) && AbsoluteWord(cpu, 0x4e1c + slot) == 0x1a)
                anchor = BallAnchor(cpu, AbsoluteWord(cpu, 0x1803));
            if (AbsoluteWord(cpu, 0x4e1c + slot) == 0x6c &&
                x == (int16_t)AbsoluteWord(cpu, 0x6924) &&
                y == (int16_t)AbsoluteWord(cpu, 0x6926)) {
                /* Fixed tee marker: do not follow Kirby after the shot.
                 * $80:C3CB seeds the tee U/V with half-cell fractions;
                 * $80:C3BF stores the marker one pixel above its ground. */
                anchor = (OrbitAnchor){
                    AbsoluteWord(cpu, 0x6b64) + 0.5, AbsoluteWord(cpu, 0x6b66) + 0.5,
                    AbsoluteWord(cpu, 0x6b68), x, (int16_t)(y + 1), pc,
                    true, false, true
                };
            }
            if (anchor.world) task_anchors[slot / 2] = anchor;
            else if (task_anchors[slot / 2].world) anchor = task_anchors[slot / 2];
            SetPendingAnchor(anchor, x, y);
        } else {
            SetPendingAnchor((OrbitAnchor){.source_pc = pc},
                x + AbsoluteWord(cpu, 0x86), y + AbsoluteWord(cpu, 0x88));
        }
        if (address == 0xe55e) {
            x = (int16_t)(x - AbsoluteWord(cpu, 0x86));
            y = (int16_t)(y - AbsoluteWord(cpu, 0x88));
        }
        if (!Rejected(x, y, -48, -32)) return; /* queue observer owns it */
        uint16_t table = AbsoluteWord(cpu, 0x5e1c + slot);
        if (!ReadWord(cpu, bank, (uint16_t)(table + 2 * animation), &layout)) {
            building.invalid = true;
            return;
        }
        bucket = AbsoluteWord(cpu, 0x5e9c + slot);
        break;
    }
    case 0xe60b: case 0xe635:
        x = (int16_t)cpu->X;
        y = (int16_t)cpu->Y;
        if (address == 0xe60b) {
            /* A specialized producer (e.g. an enemy ground shadow) may
             * already have supplied exact provenance before this helper. */
            if (!pending_anchor_valid || pending_x != x || pending_y != y) {
                OrbitAnchor anchor = ResolveAnchor(cpu, x, y);
                unsigned slot = DirectWord(cpu, 0xd2);
                OrbitAnchor effect = RecoverEffectAnchor(cpu, slot);
                if (slot < 128 && !(slot & 1) && effect.world &&
                    x == (int16_t)AbsoluteWord(cpu, 0x5f1c + slot) &&
                    y == (int16_t)AbsoluteWord(cpu, 0x5f9c + slot))
                    anchor = effect;
                SetPendingAnchor(anchor, x, y);
            }
        }
        else SetPendingAnchor((OrbitAnchor){.source_pc = pc},
            x + AbsoluteWord(cpu, 0x86), y + AbsoluteWord(cpu, 0x88));
        if (address == 0xe60b) {
            x = (int16_t)(x - AbsoluteWord(cpu, 0x86));
            y = (int16_t)(y - AbsoluteWord(cpu, 0x88));
        }
        if (!Rejected(x, y, -48, -32)) return;
        layout = DirectWord(cpu, 0xc0);
        bucket = cpu->A;
        break;
    case 0x9e9e: case 0x9eec:
        if (cpu->X >= 0x20 || (cpu->X & 1)) return;
        x = (int16_t)AbsoluteWord(cpu, 0x8de + cpu->X);
        y = (int16_t)cpu->Y;
        {
            int wx = (int16_t)(x + AbsoluteWord(cpu, 0x86));
            int wy = (int16_t)(y + AbsoluteWord(cpu, 0x88));
            OrbitAnchor anchor = ResolveAnchor(cpu, wx, wy);
            if (cpu->X == 4 || cpu->X == 6) {
                unsigned player = cpu->X - 4;
                anchor.x = Fixed(cpu->ram, 0xd8aa + player, 0xd8ae + player);
                anchor.y = Fixed(cpu->ram, 0xd8b2 + player, 0xd8b6 + player);
                anchor.z = Elevation(Fixed(cpu->ram, 0xd8ba + player, 0xd8be + player)) +
                    ((int16_t)AbsoluteWord(cpu, 0x93e + player) - wy) / 16.0;
                anchor.world = true; anchor.unresolved = false;
                anchor.ground = true;
            }
            SetPendingAnchor(anchor, wx, wy);
        }
        if (!Rejected(x, y, address == 0x9eec ? -24 : -48,
                      address == 0x9eec ? -24 : -32)) return;
        layout = DirectWord(cpu, 0xe6);
        bucket = AbsoluteWord(cpu, 0x2880);
        break;
    case 0x8b09:
        {
            /* A direct JSL caller identifies otherwise unclassified draws
             * in diagnostics. Do not interpret arbitrary stack bytes as PC. */
            unsigned stack = (uint16_t)(cpu->S + 1);
            uint16_t ret = cpu->ram[stack] | (unsigned)cpu->ram[(uint16_t)(stack + 1)] << 8;
            unsigned caller_bank = cpu->ram[(uint16_t)(cpu->S + 3)];
            uint8_t opcode;
            if (ret >= 0x8003 && ReadByte(cpu, caller_bank, ret - 3, &opcode) && opcode == 0x22)
                source_pc = (caller_bank << 16) | (ret - 3);
        }
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

static void ProjectionObserver(CpuState *cpu, uint32_t pc)
{
    KdcObserveProjection(cpu, pc);
}

void KdcInitializeSprites(void)
{
    static const uint16_t sites[] = {
        0x8823, 0x8841, 0x8ae1, 0x82ca, 0x8c12, 0xe55e, 0xe5a6, 0xe60b, 0xe635,
        0x9e9e, 0x9eec, 0x8b09, 0x8a3d, 0xdfc0, 0xe776, 0xe525
    };
    for (unsigned i = 0; i < sizeof(sites) / sizeof(sites[0]); ++i) {
        if (!interp_bridge_add_pre_opcode_hook(0x800000 | sites[i], InterpretedObserver)) {
            fprintf(stderr, "Unable to register course sprite observer\n");
            abort();
        }
    }
    if (!interp_bridge_add_pre_opcode_hook(0x84efc1, InterpretedObserver)) {
        fprintf(stderr, "Unable to register course reward observer\n");
        abort();
    }
    static const uint16_t projection_sites[] = {0xa5cc, 0xaefc, 0xaf2d, 0xaf58, 0xae70, 0xaea1};
    for (unsigned i = 0; i < sizeof(projection_sites) / sizeof(projection_sites[0]); ++i) {
        if (!interp_bridge_add_pre_opcode_hook(0x810000 | projection_sites[i], ProjectionObserver)) {
            fprintf(stderr, "Unable to register course projection observer\n");
            abort();
        }
    }
    if (!interp_bridge_add_pre_opcode_hook(0x85977a, ProjectionObserver)) {
        fprintf(stderr, "Unable to register course shadow observer\n");
        abort();
    }
    const char *path = getenv("KDC_SPRITE_LOG");
    if (!diagnostic && path && *path) {
        diagnostic = fopen(path, "w");
        if (!diagnostic) fprintf(stderr, "Unable to open sprite log: %s\n", path);
    }
    path = getenv("KDC_ORBIT_SPRITE_LOG");
    if (!orbit_diagnostic && path && *path) {
        orbit_diagnostic = fopen(path, "w");
        if (!orbit_diagnostic) fprintf(stderr, "Unable to open orbit sprite log: %s\n", path);
    }
}

static void PrepareLine(Ppu *ppu, int line, void *context)
{
    const uint8_t *ram = context;
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
    int camera_x = KdcCourseScroll(ppu->hScroll[0], KdcReadWord(ram, 0x86));
    int camera_y = KdcCourseScroll(ppu->vScroll[0], KdcReadWord(ram, 0x88));
    for (unsigned bucket = 0; bucket < 10; ++bucket) {
        for (unsigned i = 0; i < displayed.count; ++i) {
            const Piece *piece = &displayed.pieces[i];
            if (piece->bucket != bucket) continue;
            ++order;
            int size = sizes[PPU_objSize(ppu)][piece->large];
            int x = (int16_t)(piece->world_x - camera_x);
            int y = (int16_t)(piece->world_y - camera_y);
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

void KdcPrepareSprites(Ppu *ppu, const uint8_t *ram, int extra)
{
    left_pixels = right_pixels = 0;
    recovered_left = recovered_right = 0;
    PpuSetExtraObjects(ppu, NULL, 0);
    const char *enabled = getenv("KDC_SPRITE_VIEW");
    if (enabled && !strcmp(enabled, "0")) extra = 0;
    PpuSetExtraObjectProvider(ppu, extra ? PrepareLine : NULL, (void *)ram);
}

void KdcResetSprites(Ppu *ppu)
{
    memset(&building, 0, sizeof(building));
    memset(&displayed, 0, sizeof(displayed));
    request_count = 0;
    projection_count = projection_next = 0;
    memset(task_anchors, 0, sizeof(task_anchors));
    pending_anchor_valid = false;
    if (ppu) KdcPrepareSprites(ppu, NULL, 0);
}

void KdcVisitOrbitSprites(const Ppu *ppu, const uint8_t *ram,
                         KdcOrbitSpriteVisitor visitor, void *context)
{
    if (!ppu || !ram || !visitor || displayed.invalid || !KdcIsCourse(ppu)) return;
    int camera_x = KdcCourseScroll(ppu->hScroll[0], KdcReadWord(ram, 0x86));
    int camera_y = KdcCourseScroll(ppu->vScroll[0], KdcReadWord(ram, 0x88));
    for (unsigned bucket = 0; bucket < 10; ++bucket) {
        for (unsigned i = 0; i < displayed.count; ++i) {
            const Piece *piece = &displayed.pieces[i];
            if (piece->bucket != bucket) continue;
            const OrbitAnchor *anchor = &piece->anchor;
            KdcOrbitSprite sprite = {
                anchor->x, anchor->y, anchor->z,
                (int16_t)(piece->world_x - (anchor->world ? anchor->origin_x : camera_x)),
                (int16_t)(piece->world_y - (anchor->world ? anchor->origin_y : camera_y)),
                piece->attributes, piece->large, piece->bucket,
                anchor->world, anchor->unresolved, anchor->ground, anchor->source_pc
            };
            visitor(&sprite, context);
        }
    }
}

void KdcReportSprites(int frame)
{
    if (diagnostic)
        fprintf(diagnostic, "%d %u %u %u %u %u %u %u\n", frame, building.count,
                displayed.count, displayed.invalid, left_pixels, right_pixels,
                recovered_left, recovered_right);
    if (orbit_diagnostic) {
        for (unsigned i = 0; i < displayed.count; ++i) {
            const Piece *piece = &displayed.pieces[i];
            const OrbitAnchor *a = &piece->anchor;
            fprintf(orbit_diagnostic, "%d %u %06x %u %u %d %d %.6f %.6f %.6f %u %04x %d %d\n",
                    frame, i, a->source_pc, a->world, a->unresolved,
                    piece->world_x, piece->world_y, a->x, a->y, a->z,
                    a->ground, piece->attributes, a->origin_x, a->origin_y);
        }
    }
}
