/* Synthetic ROM tables and graphics; no extracted game data. */
#include "course_sprite_view.h"
#include "course_sprites.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint8_t rom[0x100000];
static uint16_t vram[0x8000];
static unsigned Offset(unsigned bank, unsigned address)
{ return ((bank & 0x7f) << 15) | (address & 0x7fff); }
static void Word(unsigned bank, unsigned address, unsigned value)
{
    unsigned o = Offset(bank, address);
    rom[o] = value; rom[o + 1] = value >> 8;
}
static void Script(unsigned bank, unsigned address, unsigned source,
                   unsigned destination, unsigned length)
{
    unsigned o = Offset(bank, address);
    rom[o] = 0;
    Word(bank, address + 1, length);
    Word(bank, address + 3, source);
    rom[o + 5] = 0x8d;
    Word(bank, address + 6, destination);
    rom[o + 8] = 0xff;
    for (unsigned i = 0; i < length; ++i)
        rom[Offset(0x8d, source) + i] = (uint8_t)(source / 32 + i);
}
static void Load(unsigned source, unsigned destination, unsigned length)
{
    unsigned o = Offset(0x8d, source);
    for (unsigned i = 0; i < length; i += 2)
        vram[destination + i / 2] = rom[o + i] | (unsigned)rom[o + i + 1] << 8;
}
static int failures;
#define EXPECT(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
int main(void)
{
    const double pi = 3.14159265358979323846;
    for (unsigned d = 0; d < 16; ++d) {
        Word(0x87, 0xab5e + 2 * d, 0x8000 + 32 * d);
        rom[Offset(0x87, 0xab4e + d)] = d >= 8;
        for (unsigned phase = 0; phase < 3; ++phase)
            Script(0x87, 0x8000 + 32 * d + 9 * phase,
                   0x8000 + 0x400 * d + 0x120 * phase, 0x7c00, 0x120);
    }
    Load(0x8240, 0x7c00, 0x120); /* direction zero, phase two */
    KdcOrbitSprite original = {.attributes = 0x31c0, .offset_x = -12,
        .offset_y = -19, .world = true};
    KdcOrbitSprite s = original;
    KdcSpriteView view = {.family = KDC_SPRITE_VIEW_BALL};
    EXPECT(KdcApplySpriteView(rom, sizeof(rom), vram, view, pi / 2, 8, &s));
    EXPECT(s.view_tiles[0] == Offset(0x8d, 0xb240)); /* direction12, same phase */
    EXPECT(s.attributes == (original.attributes ^ 0x4000));
    EXPECT(s.offset_x == 4 && s.offset_y == -19);
    s = original;
    EXPECT(!KdcApplySpriteView(rom, sizeof(rom), vram, view, 0, 8, &s));
    EXPECT(!memcmp(&s, &original, sizeof(s)));
    vram[0x7c00] ^= 1;
    EXPECT(!KdcApplySpriteView(rom, sizeof(rom), vram, view, pi / 2, 8, &s));
    EXPECT(!memcmp(&s, &original, sizeof(s)));
    vram[0x7c00] ^= 1;

    /* Stale directions (see KdcApplySpriteView). This table shape, five
     * images shared by direction pairs and told apart by the mirror flag,
     * follows the documented layout; the contents are synthetic. */
    static const uint8_t image[16] = {0,0,1,1,2,2,3,3,2,2,1,1,0,0,4,4};
    static const uint8_t mirror[16] = {0,0,0,0,1,1,0,0,0,0,1,1,1,1,0,0};
    for (unsigned k = 0; k < 5; ++k)
        for (unsigned phase = 0; phase < 3; ++phase)
            Script(0x87, 0x8000 + 32 * k + 9 * phase,
                   0x8000 + 0x400 * k + 0x120 * phase, 0x7c00, 0x120);
    for (unsigned player = 0; player <= 2; player += 2) {
        for (unsigned d = 0; d < 16; ++d) {
            Word(0x87, (player ? 0xc9cb : 0xab5e) + 2 * d, 0x8000 + 32 * image[d]);
            rom[Offset(0x87, (player ? 0xc9bb : 0xab4e) + d)] = mirror[d];
        }
        const KdcSpriteView stale = {.family = KDC_SPRITE_VIEW_BALL,
            .player = (uint8_t)player};
        original = (KdcOrbitSprite){.attributes = 0x31c0, .offset_x = -12,
            .offset_y = -19, .world = true};
        /* Aimed at 2 (image 1, unmirrored), phase one: a quarter turn shows
         * 14, the only image-4 view. */
        Load(0x8520, 0x7c00, 0x120);
        s = original;
        EXPECT(KdcApplySpriteView(rom, sizeof(rom), vram, stale, pi / 2, 8, &s));
        EXPECT(s.view_tiles[0] == Offset(0x8d, 0x9120));
        EXPECT(s.attributes == original.attributes);
        /* Aimed at 10, the same image mirrored: 6 (image 3) is unmirrored. */
        KdcOrbitSprite mirrored = original;
        mirrored.attributes ^= 0x4000;
        s = mirrored;
        EXPECT(KdcApplySpriteView(rom, sizeof(rom), vram, stale, pi / 2, 8, &s));
        EXPECT(s.view_tiles[0] == Offset(0x8d, 0x8d20));
        EXPECT(s.attributes == original.attributes);
        EXPECT(s.offset_x == 4);
        /* A captured direction that matches image and mirror is kept: 3
         * turned by a sixteenth is 2 (image 1); its partner 2 would give 1. */
        s = original;
        EXPECT(KdcApplySpriteView(rom, sizeof(rom), vram,
            (KdcSpriteView){.family = KDC_SPRITE_VIEW_BALL, .direction = 3,
                            .facing = 3, .player = (uint8_t)player}, pi / 8, 8, &s));
        EXPECT(s.view_tiles[0] == Offset(0x8d, 0x8520));
        /* Otherwise the aim direction chooses within the resident pair; an
         * aim that is not resident is ignored and the lower one is used. */
        KdcSpriteView aimed = stale;
        aimed.aimed = true; aimed.aim = 3;
        s = original;
        EXPECT(KdcApplySpriteView(rom, sizeof(rom), vram, aimed, pi / 8, 8, &s));
        EXPECT(s.view_tiles[0] == Offset(0x8d, 0x8520));
        aimed.aim = 5;
        s = original;
        EXPECT(KdcApplySpriteView(rom, sizeof(rom), vram, aimed, pi / 8, 8, &s));
        EXPECT(s.view_tiles[0] == Offset(0x8d, 0x8120));
        /* Aimed at 12: image 0 like the stale 0, but mirrored. A quarter
         * turn shows 8 (image 2, unmirrored), not 12. */
        Load(0x8120, 0x7c00, 0x120);
        s = mirrored;
        EXPECT(KdcApplySpriteView(rom, sizeof(rom), vram, stale, pi / 2, 8, &s));
        EXPECT(s.view_tiles[0] == Offset(0x8d, 0x8920));
        EXPECT(s.attributes == original.attributes);
        /* Every aim, with the cleared 0/0 capture: both members of each
         * shared pair (including 1, whose partner is the stale 0) must turn
         * from the aim, not from the capture. */
        for (unsigned aim = 0; aim < 16; ++aim) {
            Load(0x8000 + 0x400 * image[aim] + 0x120, 0x7c00, 0x120);
            KdcOrbitSprite shown = original;
            if (mirror[aim]) shown.attributes ^= 0x4000;
            KdcSpriteView cleared = stale;
            cleared.aimed = true; cleared.aim = (uint8_t)aim;
            for (unsigned turn = 1; turn <= 4; turn += 3) {
                unsigned target = (aim - turn) & 15;
                s = shown;
                EXPECT(KdcApplySpriteView(rom, sizeof(rom), vram, cleared, turn * pi / 8, 8, &s));
                if (s.view_tiles[0] != Offset(0x8d, 0x8000 + 0x400 * image[target] + 0x120) ||
                    ((s.attributes >> 14) & 1) != mirror[target])
                    fprintf(stderr, "player %u aim %u turn %u\n", player, aim, turn);
                EXPECT(s.view_tiles[0] == Offset(0x8d, 0x8000 + 0x400 * image[target] + 0x120));
                EXPECT(((s.attributes >> 14) & 1) == mirror[target]);
            }
        }
        /* Graphics that match no direction keep the original artwork. */
        Load(0x8120, 0x7c00, 0x120);
        vram[0x7c00] ^= 1;
        s = original;
        EXPECT(!KdcApplySpriteView(rom, sizeof(rom), vram, stale, pi / 2, 8, &s));
        EXPECT(!memcmp(&s, &original, sizeof(s)));
        vram[0x7c00] ^= 1;
    }

    /* Tilted parasol uses separate body/umbrella DMA strips and signed
     * direction-dependent attachment offsets. The whole request moves. */
    for (unsigned d = 0; d < 16; ++d) {
        Word(0x85, 0xb54d + 2 * d, 0xc000 + 32 * d);
        Script(0x85, 0xc000 + 32 * d, 0xc000 + 0x200 * d, 0x7c90, 0xc0);
        Script(0x85, 0xc008 + 32 * d, 0xc0c0 + 0x200 * d, 0x7d90, 0xc0);
        rom[Offset(0x85, 0xb539 + d)] = d >= 8;
        Word(0x85, 0xb5d9 + 4 * d, (unsigned)(int)(d - 8));
        Word(0x85, 0xb5db + 4 * d, (unsigned)(int)(-20 + d));
    }
    Load(0xc000, 0x7c90, 0xc0); Load(0xc0c0, 0x7d90, 0xc0);
    original = (KdcOrbitSprite){.attributes = 0x31c9, .offset_x = -16,
        .offset_y = -28, .motion_x = -8, .motion_y = -20, .world = true};
    s = original;
    view = (KdcSpriteView){.family = KDC_SPRITE_VIEW_PARASOL_TILT, .attachment = true};
    EXPECT(KdcApplySpriteView(rom, sizeof(rom), vram, view, pi / 2, 16, &s));
    EXPECT(s.motion_x == 4 && s.motion_y == -8);
    EXPECT(s.view_order == -1);
    EXPECT(s.offset_x == -4 && s.offset_y == -16);
    EXPECT(s.view_tiles[0] == Offset(0x8d, 0xd800));
    EXPECT(s.view_tiles[1] == Offset(0x8d, 0xd820));
    EXPECT(s.view_tiles[2] == Offset(0x8d, 0xd8c0));
    EXPECT(s.view_tiles[3] == Offset(0x8d, 0xd8e0));
    s = original;
    EXPECT(!KdcApplySpriteView(rom, sizeof(rom) - 1, vram, view, pi / 2, 16, &s));
    EXPECT(!KdcApplySpriteView(rom, sizeof(rom), vram, view, NAN, 16, &s));
    EXPECT(!KdcApplySpriteView(rom, sizeof(rom), vram, view, pi / 2, 32, &s));
    EXPECT(!memcmp(&s, &original, sizeof(s)));

    /* Both player pages and all parasol stages select complete 16px pieces,
     * with opposite turns choosing opposite view directions. */
    for (unsigned player = 0; player <= 2; player += 2) {
        for (unsigned family = KDC_SPRITE_VIEW_PARASOL_UP;
             family <= KDC_SPRITE_VIEW_PARASOL_SIDE; ++family) {
            bool full = family == KDC_SPRITE_VIEW_PARASOL_TILT;
            unsigned table = family == KDC_SPRITE_VIEW_PARASOL_UP ? 0xb3bc :
                             full ? 0xb54d : 0xb6a1;
            unsigned offsets = family == KDC_SPRITE_VIEW_PARASOL_UP ? 0xb3ff :
                               full ? 0xb5d9 : 0xb6dc;
            if (player) table += full ? 32 : 16;
            unsigned destination = player ? 0x7e00 : 0x7c00;
            for (unsigned d = 0; d < (full ? 16u : 8u); ++d) {
                Word(0x85, table + 2 * d, 0xd000 + 32 * d);
                Script(0x85, 0xd000 + 32 * d, 0xe000 + 0x200 * d, destination, 64);
                Script(0x85, 0xd008 + 32 * d, 0xe040 + 0x200 * d, destination + 0x100, 64);
                Word(0x85, offsets + 4 * d, d);
                Word(0x85, offsets + 4 * d + 2, (unsigned)(int)(-20 + d));
            }
            Load(0xe000, destination, 64); Load(0xe040, destination + 0x100, 64);
            original = (KdcOrbitSprite){.attributes = player ? 0x31e0 : 0x31c0,
                .offset_x = -8, .offset_y = -28, .motion_y = -20};
            if (!full) original.attributes += 9; /* resident canopy */
            view = (KdcSpriteView){.family = (uint8_t)family, .player = (uint8_t)player,
                                   .attachment = true};
            /* SIDE retains its original tiles after the live direction
             * changes: infer its source view, never use this stale index. */
            if (family == KDC_SPRITE_VIEW_PARASOL_SIDE) view.direction = 4;
            for (int turn = -1; turn <= 1; turn += 2) {
                s = original;
                unsigned target = turn > 0 ? 12 : 4;
                if (!full) target /= 2;
                EXPECT(KdcApplySpriteView(rom, sizeof(rom), vram, view, turn * pi / 2, 16, &s));
                EXPECT(s.view_tiles[0] == (full ? Offset(0x8d, 0xe000 + target * 0x200) : 0));
                EXPECT(s.view_tiles[3] == (full ? Offset(0x8d, 0xe060 + target * 0x200) : 0));
                EXPECT(s.motion_x == target && s.motion_y == -20 + (double)target);
                EXPECT(s.view_order == (full && turn > 0 ? -1 : 1));
            }
            s = original;
            EXPECT(!KdcApplySpriteView(rom, sizeof(rom), vram, view, 2 * pi, 16, &s));
            EXPECT(!memcmp(&s, &original, sizeof(s)));
        }
    }
    return failures ? 1 : 0;
}
