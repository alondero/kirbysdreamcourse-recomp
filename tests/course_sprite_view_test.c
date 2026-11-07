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
    KdcSpriteView view = {KDC_SPRITE_VIEW_BALL, 0, 0, 0, false};
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
    view = (KdcSpriteView){KDC_SPRITE_VIEW_PARASOL_TILT, 0, 0, 0, true};
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
            view = (KdcSpriteView){family, 0, 0, player, true};
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
