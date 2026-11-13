/* Synthetic geometry and planar graphics: no ROM assets or captured pixels. */
#include "course_orbit.h"
#include "course_hud.h"
#include "course_sprites.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { width = 256, height = 224, padding = 4, stride = width + padding };
static uint8_t rom[0x100000], saved_ram[0x20000];
uint8_t g_ram[0x20000];
static Ppu ppu, saved_ppu;
static Cart cart;
static Snes snes;
Snes *g_snes = &snes;
int snes_frame_counter;
static uint32_t pixels[stride * height + 8], first[stride * height + 8];
static uint32_t wide_pixels[(342 + padding) * height + 8];
static uint32_t native_field[342 * height];
static bool course = true;
static bool draw_ground;
static double world_height = 3;
/* Scenario sprites replace the default scene when enabled. */
static KdcOrbitSprite custom[4];
static unsigned custom_count;
static bool use_custom;

bool KdcIsCourse(const Ppu *value) { return value == &ppu && course; }
void PpuSetBackgroundSample(Ppu *p, bool (*sample)(const Ppu *, unsigned, int, int, uint16_t *, int *, int *, void *), void *ctx) {
    p->backgroundSample = sample; p->backgroundSampleContext = ctx;
}
static bool hd_enabled;
bool KdcHdEnabled(void) { return hd_enabled; }
bool KdcOrbitEnabled(void) { return true; }

void KdcVisitOrbitSprites(const Ppu *value, const uint8_t *ram,
                         KdcOrbitSpriteVisitor visitor, void *context)
{
    assert(value == &ppu && ram == g_ram);
    if (use_custom) {
        for (unsigned i = 0; i < custom_count; ++i) visitor(&custom[i], context);
        return;
    }
    KdcOrbitSprite world = {.x = 3, .y = 2, .z = world_height,
        .attributes = 0x3001, .world = true};
    KdcOrbitSprite hud = {.offset_x = 20, .offset_y = 200,
        .attributes = 0x202, .world = false};
    if (draw_ground) {
        /* A ground decal projects beneath the body. Its image overlaps the
         * raised body's screen rectangle, but must remain behind it even
         * when its draw request follows the body's in reverse OBJ order. */
        KdcOrbitSprite ground = {.x = 3, .y = 2, .z = 1,
            .offset_y = -32, .attributes = 0x3403, .world = true, .ground = true};
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

static void Ball(double x, double y, unsigned elevation)
{
    /* Player one's U/V are signed integer cells with 16-bit fractions. */
    Word(0xd8ae, (unsigned)(int)floor(x));
    Word(0xd8aa, (unsigned)((x - floor(x)) * 65536));
    Word(0xd8b6, (unsigned)(int)floor(y));
    Word(0xd8b2, (unsigned)((y - floor(y)) * 65536));
    BallElevation(elevation);
}

static void Scroll(int x, int y)
{
    Word(0x86, (uint16_t)x);
    Word(0x88, (uint16_t)y);
    ppu.hScroll[0] = (uint16_t)(x & 1023);
    ppu.vScroll[0] = (uint16_t)(y & 1023);
}

static void Snapshot(void)
{
    memcpy(saved_ram, g_ram, sizeof(g_ram));
    saved_ppu = ppu;
}

static unsigned Bounds(const uint32_t *buffer, unsigned w, uint32_t color,
                       int *left, int *top, int *right, int *bottom)
{
    unsigned count = 0;
    *left = *top = 1 << 30;
    *right = *bottom = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < (int)w; ++x) {
            if (buffer[y * (w + padding) + x] != color) continue;
            ++count;
            if (x < *left) *left = x;
            if (x > *right) *right = x;
            if (y < *top) *top = y;
            if (y > *bottom) *bottom = y;
        }
    }
    return count;
}

static void Zoom(int vertical, int frames)
{
    for (int i = 0; i < frames; ++i) KdcOrbitInput(0, vertical, false);
}

static int failures;
#define EXPECT(c) do { if (!(c)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static const uint32_t red = 0xffff0000, cyan = 0xff00ffff, blue = 0xff0000ff,
                      green = 0xff00ff00, white = 0xffffffff;

static KdcOrbitSprite Body(double x, double y, double z, int offset_y)
{
    return (KdcOrbitSprite){.x = x, .y = y, .z = z, .offset_x = -4,
        .offset_y = offset_y, .attributes = 0x3001, .world = true};
}

/* Scenarios share one fixture and run in order; a scenario that changes the
 * map, scroll or camera state restores or resets what the next one needs. */
static int Scenarios(void)
{
    int left, top, right, bottom;
    double cx, cy, first_x, first_y;
    use_custom = true;

    /* Zooming in keeps the subject in view. The original camera frames the
     * ball 80 pixels left of center; that look-ahead must not be magnified
     * 4x with the course, pushing the player off screen. */
    Ball(3, 2, 1);
    custom[0] = Body(3, 2, 1, -8);
    custom_count = 1;
    Scroll(112, 40);
    Snapshot();
    NativeAngle();
    Zoom(-32768, 150);
    Draw();
    unsigned count = CountColor(pixels, red, &cx, &cy);
    EXPECT(count == 64 * 16);
    EXPECT(fabs(cx - 128) < 100 && fabs(cy - 112) < 70);
    first_x = cx;
    first_y = cy;
    /* In flight the pivot stays on the ground beneath the ball, so bounces
     * do not shake the course. Zoomed in, a high ball would then leave the
     * top of the screen: the view rises just enough to keep it in frame. */
    Ball(3, 2, 7);
    custom[0] = Body(3, 2, 7, -8);
    Snapshot();
    Draw();
    EXPECT(CountColor(pixels, red, &cx, &cy) == 64 * 16);
    EXPECT(cx == first_x && cy - 16 >= 0);
    /* Back on the ground, the view returns to the ground pivot. */
    Ball(3, 2, 1);
    custom[0] = Body(3, 2, 1, -8);
    Snapshot();
    Draw();
    EXPECT(CountColor(pixels, red, &cx, &cy) == 64 * 16);
    EXPECT(cx == first_x && cy == first_y);
    /* Zoomed out, the look-ahead shrinks with the course, so the course
     * stays centered around the original view instead of drifting away. */
    KdcOrbitInput(0, 0, true);
    NativeAngle();
    Zoom(32767, 150);
    Draw();
    EXPECT(CountColor(pixels, red, &cx, &cy) > 0);
    EXPECT(fabs(cx - 128) < 25 && fabs(cy - 112) < 15);
    /* Turned away from the original angle, its look-ahead points at an
     * unrelated part of the screen; it fades out and centers the player.
     * The body's center is four pixels above its ground anchor. */
    KdcOrbitInput(0, 0, true);
    NativeAngle();
    QuarterTurn();
    Draw();
    EXPECT(CountColor(pixels, red, &cx, &cy) == 64);
    EXPECT(fabs(cx - 127.5) < 1 && fabs(cy - 107.5) < 1);

    /* After the ball reaches another elevation, the orbit pivot follows the
     * ground beneath it rather than the tee's height. */
    Ball(3, 2, 1);
    custom[0] = Body(3, 2, 1, -8);
    Scroll(32, -1);
    Snapshot();
    NativeAngle();
    QuarterTurn();
    Draw();
    EXPECT(CountColor(pixels, red, &first_x, &first_y) == 64);
    TerrainElevation(4);
    Ball(3, 2, 5);
    custom[0] = Body(3, 2, 5, -8);
    Scroll(32, -65);
    Zoom(0, 60);
    Snapshot();
    Draw();
    EXPECT(CountColor(pixels, red, &cx, &cy) == 64);
    EXPECT(fabs(cx - first_x) <= 1 && fabs(cy - first_y) <= 1);
    TerrainElevation(0);
    Ball(3, 2, 1);
    Scroll(32, -1);

    /* An original sprite extends below its ground anchor (e.g. the hole,
     * shadows and feet). Those rows rest on the ground instead of being
     * buried by it, including when the guest height is quantized slightly
     * below the reconstructed surface. */
    for (unsigned below = 0; below < 2; ++below) {
        custom[0] = Body(2, 2, below ? 0.97 : 1, -4);
        Snapshot();
        NativeAngle();
        QuarterTurn();
        Draw();
        EXPECT(CountColor(pixels, red, &cx, &cy) == 64);
    }
    /* A sprite a full step below the surface remains hidden. */
    custom[0] = Body(2, 2, 0, -4);
    Draw();
    EXPECT(CountColor(pixels, red, &cx, &cy) == 0);

    /* A cup rim is a ground surface, including its back half. A ball
     * contacting that half remains above it at every quarter turn. The
     * same image drawn as a body incorrectly hides the ball's bottom rows.
     * Compare against the ball alone so raster coverage is immaterial. */
    static const double approach[4][2] = {
        {-0.0625, 0.0625}, {-0.0625, -0.0625},
        {0.0625, -0.0625}, {0.0625, 0.0625}
    };
    for (unsigned angle = 0; angle < 4; ++angle) {
        custom[0] = Body(2 + approach[angle][0], 2 + approach[angle][1], 1, -8);
        custom[1] = (KdcOrbitSprite){.x = 2, .y = 2, .z = 1,
            .offset_x = -4, .offset_y = -4, .attributes = 0x3403,
            .world = true, .ground = true};
        NativeAngle();
        for (unsigned turn = 0; turn < angle; ++turn) QuarterTurn();
        custom_count = 1;
        Draw();
        unsigned body_pixels = CountColor(pixels, red, &cx, &cy);
        custom_count = 2;
        Draw();
        EXPECT(body_pixels > 0 && CountColor(pixels, red, &cx, &cy) == body_pixels);
    }
    custom_count = 1;

    /* Ground decals keep the original image at the native angle, then turn
     * with the course: a quarter turn exchanges its screen-space width and
     * height relationship, as for the terrain artwork beneath it. */
    custom[0] = (KdcOrbitSprite){.x = 2, .y = 2, .z = 1, .offset_x = -4,
        .offset_y = -4, .attributes = 0x3403, .world = true, .ground = true};
    NativeAngle();
    Draw();
    EXPECT(Bounds(pixels, width, cyan, &left, &top, &right, &bottom) == 64);
    EXPECT(right - left == 7 && bottom - top == 7);
    QuarterTurn();
    Draw();
    EXPECT(Bounds(pixels, width, cyan, &left, &top, &right, &bottom) > 48);
    EXPECT(right - left >= 14 && bottom - top <= 6);
    /* A raised body always stands in front of its decal. */
    custom[1] = Body(2, 2, 1, -4);
    custom_count = 2;
    Draw();
    EXPECT(CountColor(pixels, red, &cx, &cy) == 64);
    /* Zoomed out to half size the decal is filtered, not dropped: its
     * 64 source pixels cover about a quarter as many output pixels. */
    custom_count = 1;
    NativeAngle();
    Zoom(32767, 60);
    Draw();
    unsigned decal = Bounds(pixels, width, cyan, &left, &top, &right, &bottom);
    EXPECT(decal >= 12 && decal <= 20);
    EXPECT(right - left <= 4 && bottom - top <= 4);

    /* Outer cliffs end at the base elevation, as in the original artwork:
     * the 4x4 course spans 128 rows down its center plus one 16-row wall. */
    custom_count = 0;
    Scroll(0, 0);
    Snapshot();
    NativeAngle();
    Draw();
    unsigned column = 0;
    for (unsigned y = 0; y < height; ++y)
        column += pixels[y * stride + 128] != blue;
    EXPECT(column >= 142 && column <= 146);

    /* Wide scenery margins use the PR #23 reflection of the clean map, not
     * a 256-pixel wrap. Row 12 has an opaque tile only in column 31. */
    Word(0xdadc + 2 * (12 * 32 + 31), 0x0c02);
    ppu.cgram[13] = 0x7fff;
    Snapshot();
    Fill(wide_pixels, sizeof(wide_pixels) / sizeof(*wide_pixels));
    assert(KdcOrbitDraw((uint8_t *)wide_pixels, (342 + padding) * 4, NULL, 342, height, 1));
    EXPECT(wide_pixels[100 * (342 + padding) + 43 + 260] == white);
    EXPECT(wide_pixels[100 * (342 + padding) + 43 - 5] == blue);
    Word(0xdadc + 2 * (12 * 32 + 31), 0);
    ppu.cgram[13] = 0;

    /* OBJ priority masks follow the original artwork at every angle: e.g.
     * the cup's priority-0 interior shows only through the transparent
     * hole in the floor tiles. (2,2,1) projects natively to (128,96), so
     * this 8x8 piece covers terrain tiles 15-16 in rows 11-12. */
    static const unsigned masks[][3] = {
        /* tile word, OBJ priority, visible */
        {0x0001, 0, 0}, {0x0000, 0, 1}, {0x0001, 1, 0}, {0x0001, 2, 1},
        {0x2001, 2, 0}, {0x2001, 3, 1},
    };
    for (unsigned m = 0; m < sizeof(masks) / sizeof(masks[0]); ++m) {
        for (unsigned row = 11; row <= 12; ++row)
            for (unsigned c = 15; c <= 16; ++c)
                Word(0x13000 + (row * 64 + c) * 2, masks[m][0]);
        custom[0] = Body(2, 2, 1, -4);
        custom[0].attributes = (uint16_t)(masks[m][1] << 12 | 1);
        custom_count = 1;
        Snapshot();
        NativeAngle();
        Draw();
        EXPECT(CountColor(pixels, red, &cx, &cy) == 64 * masks[m][2]);
        QuarterTurn();
        Draw();
        EXPECT(CountColor(pixels, red, &cx, &cy) == 64 * masks[m][2]);
    }
    for (unsigned row = 11; row <= 12; ++row)
        for (unsigned c = 15; c <= 16; ++c)
            Word(0x13000 + (row * 64 + c) * 2, 1);
    custom_count = 0;

    /* Zoomed out, each output pixel covers several texels. Average them
     * rather than point-sampling one, which shimmers on fine patterns. */
    ppu.cgram[3] = 0x7fff;
    for (unsigned y = 0; y < 8; ++y) ppu.vram[16 + y] = 0xaaff;
    Snapshot();
    NativeAngle();
    Zoom(32767, 150);
    Draw();
    unsigned mixed = 0, terrain = 0;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            uint32_t p = pixels[y * stride + x];
            if (p == blue || p == 0xffffff00 || p == 0xffff00ff) continue;
            ++terrain;
            mixed += p != green && p != white;
        }
    }
    EXPECT(terrain > 500 && mixed > terrain / 2);
    /* A continuous checker across 16 characters, including their borders.
     * Magnification changes subpixels without changing guest state. */
    for (unsigned ty = 0; ty < 4; ++ty)
        for (unsigned tx = 0; tx < 4; ++tx) {
            unsigned tile = 1 + ty * 4 + tx;
            for (unsigned y = 0; y < 8; ++y) {
                unsigned plane = 0;
                for (unsigned x = 0; x < 8; ++x) {
                    int u = (int)(tx * 8 + x), v = (int)(ty * 8 + y);
                    int a = (u + 2 * v + 1 + 128) / 32;
                    int b = (u - 2 * v + 1 + 128) / 32;
                    if ((a ^ b) & 1) plane |= 1u << (7 - x);
                }
                ppu.vram[tile * 16 + y] = (uint16_t)(0xff | (plane << 8));
                ppu.vram[tile * 16 + 8 + y] = 0;
            }
        }
    for (unsigned y = 0; y < 64; ++y)
        for (unsigned x = 0; x < 64; ++x)
            Word(0x13000 + (y * 64 + x) * 2, 1 + (y % 4) * 4 + x % 4);
    Snapshot();
    NativeAngle();
    Zoom(-32768, 100);
    Draw();
    memcpy(first, pixels, sizeof(first));
    hd_enabled = true;
    Draw();
    EXPECT(memcmp(first, pixels, sizeof(first)) != 0);
    memcpy(first, pixels, sizeof(first));
    Draw();
    EXPECT(memcmp(first, pixels, sizeof(first)) == 0);
    ppu.cgram[3] = 0x001f;
    Snapshot();
    Draw();
    EXPECT(memcmp(first, pixels, sizeof(first)) != 0);
    uint16_t checker_words[16 * 16];
    memcpy(checker_words, ppu.vram + 16, sizeof(checker_words));
    for (unsigned tile = 1; tile <= 16; ++tile)
        for (unsigned y = 0; y < 8; ++y) ppu.vram[tile * 16 + y] = 0x00ff;
    Snapshot();
    Draw();
    memcpy(first, pixels, sizeof(first));
    hd_enabled = false;
    Draw();
    EXPECT(memcmp(first, pixels, sizeof(first)) == 0);
    ppu.cgram[3] = 0;
    /* Camera-relative artwork reaches the real texel sampler. Synthetic
     * DMA tables select a red native view and white quarter-turn view,
     * without modifying the emulated VRAM or the original sprite list. */
    const uint8_t transfer[] = {0, 32, 0, 0, 0x80, 0x8d, 0, 0x7c, 0xff};
    memcpy(rom + 0x38000, transfer, sizeof(transfer));
    memcpy(rom + 0x38009, transfer, sizeof(transfer));
    rom[0x38009 + 3] = 32;
    rom[0x3ab5e] = 0; rom[0x3ab5f] = 0x80;
    rom[0x3ab5e + 24] = 9; rom[0x3ab5f + 24] = 0x80;
    for (unsigned y = 0; y < 8; ++y) {
        rom[0x68000 + 2 * y] = 255;
        rom[0x68020 + 2 * y + 1] = 255;
        ppu.vram[0x7c00 + y] = 255;
    }
    ppu.cgram[130] = 0x7fff;
    custom_count = 1;
    custom[0] = Body(2, 2, 3, -8);
    custom[0].attributes = 0x31c0;
    custom[0].view = (KdcSpriteView){.family = KDC_SPRITE_VIEW_BALL};
    Ball(2, 2, 0);
    Snapshot();
    NativeAngle();
    Draw();
    EXPECT(CountColor(pixels, red, &cx, &cy) == 64);
    QuarterTurn();
    Draw();
    EXPECT(CountColor(pixels, white, &cx, &cy) == 64);
    EXPECT(custom[0].view_tiles[0] == 0);
    /* Checker reconstruction and directional artwork coexist in one draw,
     * including reuse of unchanged checker entries on the next draw. */
    memcpy(ppu.vram + 16, checker_words, sizeof(checker_words));
    hd_enabled = true;
    Snapshot();
    Draw();
    EXPECT(CountColor(pixels, white, &cx, &cy) == 64);
    memcpy(first, pixels, sizeof(first));
    Draw();
    EXPECT(memcmp(first, pixels, sizeof(first)) == 0);
    hd_enabled = false;
    Snapshot();
    /* Stale matching tiles in an inactive OBJ page are not evidence. */
    ppu.obsel = 0;
    for (unsigned y = 0; y < 8; ++y) ppu.vram[0x1c00 + y] = 255;
    Snapshot();
    Draw();
    EXPECT(CountColor(pixels, red, &cx, &cy) == 64);
    ppu.obsel = 3;
    Snapshot();
    NativeAngle();
    Draw();
    EXPECT(CountColor(pixels, red, &cx, &cy) == 64);
    /* An attachment changes front/back order relative to its own body,
     * without relying on a depth bias or the original queue order. */
    custom[0].view.family = KDC_SPRITE_VIEW_NONE;
    custom[0].view_tiles[0] = 0x68000;
    custom[0].motion_id = 0x100;
    custom[1] = custom[0];
    custom[1].motion_id = 0x300;
    custom[1].view.attachment = true;
    custom[1].view_tiles[0] = 0x68020;
    custom[1].view_order = 1;
    custom_count = 2;
    Draw();
    EXPECT(CountColor(pixels, white, &cx, &cy) == 64);
    custom[1].view_tiles[0] = 0; /* UP/SIDE canopy stays in resident VRAM. */
    custom[1].attributes = 0x31c1;
    for (unsigned y = 0; y < 8; ++y) ppu.vram[0x7c10 + y] = 0xff00;
    Snapshot();
    Draw();
    EXPECT(CountColor(pixels, white, &cx, &cy) == 64);
    custom[1].view_order = -1;
    Draw();
    EXPECT(CountColor(pixels, red, &cx, &cy) == 64);
    Snapshot();
    use_custom = false;
    return failures != 0;
}

static void HdEdgeHud(void)
{
    static Ppu before;
    before = ppu;
    ppu.renderFlags = kPpuRenderFlags_NewRenderer;
    ppu.windowsel = 0x220000; ppu.cgwsel = 0x10; ppu.cgadsub = 0x33;
    ppu.fixedColor = 0x7fff; ppu.screenWindowed[0] = 0x13;
    ppu.window1left = 56; ppu.window1right = 235;
    ppu.vram[0x3c00 + 22 * 32 + 2] = 0x3519;
    memset(ppu.oam, 0, sizeof(ppu.oam));
    memset(ppu.highOam, 0, sizeof(ppu.highOam));
    for (unsigned i = 0; i < 4; ++i) {
        ppu.oam[i * 2] = 20 + (i & 1) * 16 + ((179 + (i >> 1) * 16) << 8);
        ppu.oam[i * 2 + 1] = 0x33a0 + i * 2;
        ppu.highOam[0] |= 2 << (i * 2);
    }
    KdcPrepareHud(&ppu, g_ram, 43, true);
    ppu.presentationPrepare(&ppu, 185, NULL);
    assert(KdcHudActive(&ppu));
    assert(KdcHudPortraitShift(&ppu, 20, 179, 0x33a0) == -51);
    use_custom = true; custom_count = 1;
    custom[0] = (KdcOrbitSprite){.offset_x = 20, .offset_y = 179, .attributes = 0x33a0, .large = true};
    for (unsigned ty = 0; ty < 2; ++ty)
        for (unsigned tx = 0; tx < 2; ++tx)
            for (unsigned y = 0; y < 8; ++y) {
                unsigned at = (PPU_objTileAdr2((&ppu)) + (0xa0 + ty * 16 + tx) * 16) & 0x7fff;
                ppu.vram[at + y] = 0x00ff;
                ppu.vram[at + y + 8] = 0;
            }
    enum { w = 342 * 4, h = height * 4 };
    static uint32_t output[w * h];
    hd_enabled = true;
    assert(KdcOrbitDrawHd((uint8_t *)output, w * 4, NULL, 342, height, 1, 4));
    /* 43px margin + 20px source X - 51px shift = 12px from left. */
    assert(output[(179 * 4) * w + 12 * 4] == 0xffffff00);
    assert(output[(194 * 4 + 3) * w + 27 * 4 + 3] == 0xffffff00);
    hd_enabled = false; use_custom = false;
    KdcPrepareHud(&ppu, g_ram, 0, false);
    ppu = before;
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
    Ball(3, 2, 0); /* Player one's ball beneath the red original body. */
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

    /* The host switches from PPU tiles to composition after course loading.
     * Its HD hook must retain subpixel detail, HUD placement and guest state. */
    enum { hd_width = width * 4, hd_height = height * 4, hd_stride = hd_width + padding };
    static uint32_t hd_pixels[hd_stride * hd_height + 8];
    hd_enabled = true;
    Fill(hd_pixels, sizeof(hd_pixels) / sizeof(*hd_pixels));
    assert(KdcOrbitDrawHd((uint8_t *)hd_pixels, hd_stride * 4, (uint8_t *)native_field,
                         width, height, 1, 4));
    unsigned detail = 0;
    for (unsigned y = 0; y < hd_height; ++y) {
        for (unsigned x = 0; x < hd_width; ++x)
            detail += hd_pixels[y * hd_stride + x] != hd_pixels[(y / 4 * 4) * hd_stride + x / 4 * 4];
        for (unsigned x = hd_width; x < hd_stride; ++x) assert(hd_pixels[y * hd_stride + x] == 0xdeadbeef);
    }
    assert(detail > 0); /* Terrain boundaries are rasterized at HD resolution. */
    for (unsigned y = 191 * 4; y < 192 * 4; ++y)
        for (unsigned x = 16 * 4; x < 40 * 4; ++x)
            assert(hd_pixels[y * hd_stride + x] == native_field[(y / 4) * width + x / 4]);
    for (unsigned i = hd_stride * hd_height; i < sizeof(hd_pixels) / sizeof(*hd_pixels); ++i)
        assert(hd_pixels[i] == 0xdeadbeef);
    assert(!memcmp(g_ram, saved_ram, sizeof(g_ram)));
    assert(!memcmp(&ppu, &saved_ppu, sizeof(ppu)));
    assert(!KdcOrbitDrawHd((uint8_t *)hd_pixels, hd_stride * 4, NULL, width, height, 1, 0));
    assert(!KdcOrbitDrawHd((uint8_t *)hd_pixels, hd_stride * 4, NULL, width, height, 1, 5));
    assert(!KdcOrbitDrawHd((uint8_t *)hd_pixels, width * 4, NULL, width, height, 1, 4));
    hd_enabled = false;

    for (int i = 0; i < 30; ++i) KdcOrbitInput(32767, 0, false);
    Draw();
    assert(CountColor(pixels, 0xffff0000, &other_x, &other_y) > 40);
    /* Actual world orbit, not a menu setting: 45 degrees about the ball's
     * ground point, with the original look-ahead scaled by cos(45). */
    assert(other_x == red_x - 15 && other_y == red_y + 8);
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
    HdEdgeHud();

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
    return Scenarios();
}
