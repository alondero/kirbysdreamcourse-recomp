/* Synthetic geometry and planar graphics: no ROM assets or captured pixels. */
#include "course_orbit.h"
#include "course_sprites.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

enum { width = 256, height = 224, padding = 4, stride = width + padding };
static uint8_t rom[0x100000], saved_ram[0x20000];
uint8_t g_ram[0x20000];
static Ppu ppu, saved_ppu;
static Cart cart;
static Snes snes;
Snes *g_snes = &snes;
static uint32_t pixels[stride * height + 8], first[stride * height + 8];
static uint32_t wide_pixels[(342 + padding) * height + 8];
static uint32_t native_field[342 * height];
static bool course = true;
static bool draw_ground;
static double world_height = 3;

bool KdcIsCourse(const Ppu *value) { return value == &ppu && course; }
bool KdcOrbitEnabled(void) { return true; }

void KdcVisitOrbitSprites(const Ppu *value, const uint8_t *ram,
                         KdcOrbitSpriteVisitor visitor, void *context)
{
    assert(value == &ppu && ram == g_ram);
    KdcOrbitSprite world = {.x = 3, .y = 2, .z = world_height,
        .attributes = 1, .world = true};
    KdcOrbitSprite hud = {.offset_x = 20, .offset_y = 200,
        .attributes = 0x202, .world = false};
    if (draw_ground) {
        /* A ground decal projects beneath the body. Its image overlaps the
         * raised body's screen rectangle, but must remain behind it even
         * when its draw request follows the body's in reverse OBJ order. */
        KdcOrbitSprite ground = {.x = 3, .y = 2, .z = 1,
            .offset_y = -32, .attributes = 0x403, .world = true, .ground = true};
        visitor(&ground, context);
    }
    visitor(&world, context);
    visitor(&hud, context);
}

static void Word(unsigned address, unsigned value)
{
    g_ram[address] = (uint8_t)value;
    g_ram[address + 1] = (uint8_t)(value >> 8);
}

static void Fill(uint32_t *buffer, size_t count)
{
    for (size_t i = 0; i < count; ++i) buffer[i] = 0xdeadbeef;
}

static void CheckPadding(const uint32_t *buffer, unsigned w)
{
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = w; x < w + padding; ++x)
            assert(buffer[y * (w + padding) + x] == 0xdeadbeef);
    for (unsigned i = 0; i < 8; ++i)
        assert(buffer[(w + padding) * height + i] == 0xdeadbeef);
}

static unsigned CountColor(const uint32_t *buffer, uint32_t color,
                           double *center_x, double *center_y)
{
    unsigned count = 0;
    double sx = 0, sy = 0;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            if (buffer[y * stride + x] != color) continue;
            ++count; sx += x; sy += y;
        }
    }
    *center_x = count ? sx / count : 0;
    *center_y = count ? sy / count : 0;
    return count;
}

static void Draw(void)
{
    Fill(pixels, sizeof(pixels) / sizeof(*pixels));
    assert(KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 1));
    CheckPadding(pixels, width);
    assert(!memcmp(g_ram, saved_ram, sizeof(g_ram)));
    assert(!memcmp(&ppu, &saved_ppu, sizeof(ppu)));
}

static void NativeAngle(void)
{
    KdcOrbitReset();
    /* Moving and returning activates the new renderer at its original
     * angle; a never-moved camera must retain the native PPU path instead. */
    KdcOrbitInput(32767, 0, false);
    KdcOrbitInput(-32767, 0, false);
}

static void BallElevation(unsigned elevation)
{
    unsigned fixed = elevation * 23170;
    Word(0xd8ba, fixed & 0xffff);
    Word(0xd8be, fixed >> 16);
}

static void TerrainElevation(unsigned elevation)
{
    unsigned fixed = elevation * 23170;
    unsigned low = 0x40000 + elevation * 2;
    unsigned high = 0x400c8 + elevation * 2;
    rom[low] = (uint8_t)fixed;
    rom[low + 1] = (uint8_t)(fixed >> 8);
    rom[high] = (uint8_t)(fixed >> 16);
    rom[high + 1] = (uint8_t)(fixed >> 24);
    memset(g_ram + 0x7390, elevation, 16);
}

static void QuarterTurn(void)
{
    for (int i = 0; i < 60; ++i) KdcOrbitInput(32767, 0, false);
}

int main(void)
{
    cart.rom = rom;
    cart.romImageSize = sizeof(rom);
    snes.cart = &cart;
    snes.ppu = &ppu;
    Word(0x8b92, 4);
    Word(0x8b94, 4);
    Word(0x8b9e, 112);
    memset(g_ram + 0x6b90, 1, 16); /* Flat cells, synthetic selector table zero. */
    /* A green terrain plane, made from one uniformly opaque 4bpp tile. */
    for (unsigned row = 0; row < 64; ++row) {
        Word(0x93b0 + row * 2, 0);
        Word(0x9bb0 + row * 2, 63);
        Word(0x8bb0 + row * 2, row * 64);
        for (unsigned column = 0; column < 64; ++column)
            Word(0x13000 + (row * 64 + column) * 2, 1);
    }
    ppu.cgram[0] = 0x7c00; /* Blue background. */
    ppu.cgram[1] = 0x03e0; /* Green terrain. */
    ppu.cgram[9] = 0x7c1f; /* Magenta BG3 HUD marker. */
    ppu.cgram[129] = 0x001f; /* Red original OBJ bitmap. */
    ppu.cgram[145] = 0x03ff; /* Yellow screen-space OBJ. */
    ppu.cgram[161] = 0x7fe0; /* Cyan ground decal. */
    ppu.inidisp = 15;
    ppu.obsel = 3; /* Separate OBJ graphics at VRAM word $6000. */
    ppu.bgTileAdr = 0x400;
    ppu.bgXsc[2] = 0x3c;
    ppu.vram[0x3c00 + 24 * 32 + 2] = 0x0802;
    ppu.vram[0x3c00 + 24 * 32 + 4] = 0x0802;
    for (unsigned y = 0; y < 8; ++y) {
        ppu.vram[16 + y] = 0x00ff;
        ppu.vram[0x6000 + 16 + y] = 0x00ff;
        ppu.vram[0x6000 + 32 + y] = 0x00ff;
        ppu.vram[0x6000 + 48 + y] = 0x00ff;
        ppu.vram[0x4000 + 16 + y] = 0x00ff;
    }
    memcpy(saved_ram, g_ram, sizeof(g_ram));
    saved_ppu = ppu;
    Fill(pixels, sizeof(pixels) / sizeof(*pixels));
    memcpy(first, pixels, sizeof(first));
    KdcOrbitReset();
    KdcOrbitInput(0, 0, false);
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 1));
    assert(!memcmp(pixels, first, sizeof(pixels))); /* Native path untouched. */
    KdcOrbitInput(100, -100, false);
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 1));

    NativeAngle();
    Draw();
    double red_x, red_y, other_x, other_y;
    unsigned red_count = CountColor(pixels, 0xffff0000, &red_x, &red_y);
    assert(red_count == 64 && red_x == 163.5 && red_y == 82.5);
    assert(pixels[200 * stride + 20] == 0xffffff00);
    assert(pixels[191 * stride + 16] == 0xffff00ff);
    memcpy(first, pixels, sizeof(first));
    assert(KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 0.2));
    assert(!memcmp(first, pixels, sizeof(first))); /* Drawing never advances controls. */

    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            native_field[y * width + x] = 0xff010000u | (y << 8) | x;
    assert(KdcOrbitDraw((uint8_t *)pixels, stride * 4, (uint8_t *)native_field,
                        width, height, 1));
    for (unsigned x = 16; x <= 39; ++x)
        assert(pixels[191 * stride + x] == native_field[191 * width + x]);
    /* Native color math survives inside the transparent gap between HUD
     * tiles, while pixels outside the occupied span remain the new scene. */
    assert(pixels[191 * stride + 15] != native_field[191 * width + 15]);
    assert(pixels[191 * stride + 40] != native_field[191 * width + 40]);
    CheckPadding(pixels, width);

    for (int i = 0; i < 30; ++i) KdcOrbitInput(32767, 0, false);
    Draw();
    assert(CountColor(pixels, 0xffff0000, &other_x, &other_y) > 40);
    assert(other_x < red_x - 20); /* Actual world orbit, not a menu setting. */
    assert(memcmp(first, pixels, sizeof(first)));
    assert(pixels[200 * stride + 20] == 0xffffff00);
    assert(pixels[191 * stride + 16] == 0xffff00ff);

    NativeAngle();
    for (int i = 0; i < 60; ++i) KdcOrbitInput(0, -32768, false);
    Draw();
    assert(CountColor(pixels, 0xffff0000, &other_x, &other_y) > red_count * 3);
    assert(pixels[200 * stride + 20] == 0xffffff00);
    assert(pixels[191 * stride + 16] == 0xffff00ff);

    NativeAngle();
    world_height = 0; /* Opaque terrain must occlude a buried original sprite. */
    Draw();
    assert(!CountColor(pixels, 0xffff0000, &other_x, &other_y));
    world_height = 3;
    draw_ground = true;
    Draw();
    assert(CountColor(pixels, 0xffff0000, &other_x, &other_y) == red_count);
    draw_ground = false;
    Fill(wide_pixels, sizeof(wide_pixels) / sizeof(*wide_pixels));
    assert(KdcOrbitDraw((uint8_t *)wide_pixels, (342 + padding) * 4, NULL, 342, height, 1));
    assert(wide_pixels[200 * (342 + padding) + 20 + 43] == 0xffffff00);
    assert(wide_pixels[191 * (342 + padding) + 16 + 43] == 0xffff00ff);
    CheckPadding(wide_pixels, 342);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < 342; ++x)
            native_field[y * 342 + x] = 0xff010000u | (y << 9) | x;
    assert(KdcOrbitDraw((uint8_t *)wide_pixels, (342 + padding) * 4,
                        (uint8_t *)native_field, 342, height, 1));
    for (unsigned x = 16 + 43; x <= 39 + 43; ++x)
        assert(wide_pixels[191 * (342 + padding) + x] == native_field[191 * 342 + x]);
    CheckPadding(wide_pixels, 342);

    Fill(pixels, sizeof(pixels) / sizeof(*pixels));
    memcpy(first, pixels, sizeof(first));
    assert(!KdcOrbitDraw((uint8_t *)pixels, width * 4 - 1, NULL, width, height, 1));
    assert(!memcmp(first, pixels, sizeof(first)));
    ppu.inidisp = 14;
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 1));
    ppu.inidisp = 0x8f;
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 1));
    ppu.inidisp = 15;
    ppu.mosaic = 0x10;
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 1));
    assert(!memcmp(first, pixels, sizeof(first)));
    ppu.mosaic = 0;
    Draw(); /* Transitions defer to the PPU without discarding the camera. */
    Fill(pixels, sizeof(pixels) / sizeof(*pixels));
    KdcOrbitInput(0, 0, true);
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 1));
    NativeAngle();
    course = false;
    KdcOrbitInput(32767, -32768, false);
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 1));
    course = true;
    KdcOrbitInput(0, 0, false);
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 1));
    assert(!memcmp(first, pixels, sizeof(first)));
    NativeAngle();
    Word(0xabb2, 1); /* A newly loaded hole starts with its original view. */
    KdcOrbitInput(0, 0, false);
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 1));
    assert(!memcmp(first, pixels, sizeof(first)));

    /* Equal scenes at different elevations must orbit around the same
     * visible subject. Capture the low scene after a quarter turn first. */
    memcpy(saved_ram, g_ram, sizeof(g_ram));
    NativeAngle();
    QuarterTurn();
    Draw();
    assert(CountColor(pixels, 0xffff0000, &red_x, &red_y) == 64);

    TerrainElevation(9);
    world_height = 12;
    Word(0x88, (uint16_t)-144);
    ppu.vScroll[0] = ((unsigned)-144) & 1023;
    Word(0x87e, 0xffff); /* Course registers exist before the ball is ready. */
    Word(0x6b4e, 0xffff);
    BallElevation(0); /* Loader's temporary zero must not become the pivot. */
    NativeAngle();
    Fill(pixels, sizeof(pixels) / sizeof(*pixels));
    memcpy(first, pixels, sizeof(first));
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 1));
    assert(!memcmp(first, pixels, sizeof(first)));

    BallElevation(9);
    Word(0x87e, 0); /* The initialized actor now supplies the elevated focus. */
    KdcOrbitInput(0, 0, false);
    QuarterTurn();
    memcpy(saved_ram, g_ram, sizeof(g_ram));
    saved_ppu = ppu;
    Draw();
    assert(CountColor(pixels, 0xffff0000, &other_x, &other_y) == 64);
    assert(other_x == red_x && other_y == red_y);

    /* During a shot the validated proxy can supply readiness even while
     * the main actor is inactive. Invalid or odd proxy slots cannot. */
    Word(0x87e, 0xffff);
    Word(0x6b4e, 3);
    Word(0x4e1c + 3, 0x1a);
    NativeAngle();
    assert(!KdcOrbitDraw((uint8_t *)pixels, stride * 4, NULL, width, height, 1));
    Word(0x6b4e, 2);
    Word(0x4e1c + 2, 0x1a);
    KdcOrbitInput(0, 0, false);
    QuarterTurn();
    memcpy(saved_ram, g_ram, sizeof(g_ram));
    Draw();
    assert(CountColor(pixels, 0xffff0000, &other_x, &other_y) == 64);
    assert(other_x == red_x && other_y == red_y);

    /* Reset must capture a fresh lower elevation, not retain the old one. */
    TerrainElevation(0);
    world_height = 3;
    Word(0x88, 0);
    ppu.vScroll[0] = 0;
    Word(0x87e, 0);
    Word(0x6b4e, 0xffff);
    BallElevation(0);
    NativeAngle();
    QuarterTurn();
    memcpy(saved_ram, g_ram, sizeof(g_ram));
    saved_ppu = ppu;
    Draw();
    assert(CountColor(pixels, 0xffff0000, &other_x, &other_y) == 64);
    assert(other_x == red_x && other_y == red_y);
    return 0;
}
