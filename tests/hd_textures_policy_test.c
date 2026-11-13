/* ROM-free unit test for src/hd_textures.c course/HUD policy hooks. */
#include "course_checker.h"
#include "course_hud.h"
#include "course_orbit.h"
#include "course_sprites.h"
#include "course_view.h"
#include "hd_scaler.h"
#include "hd_textures.h"
#include "launcher_settings.h"
#include "snes/ppu.c"
#include "snes/snes.h"
#include <assert.h>
#include <string.h>

static Snes snes_state;
Snes *g_snes = &snes_state;
uint8_t g_ram[0x20000];
int snes_frame_counter = 1;
unsigned char g_snesrecomp_last_hdmaen;

enum {
    kExtra = 96,
    kFullWidth = 256 + kExtra * 2,
    kHeight = 224,
    kScale = kKdcHdScale,
};

static uint32_t native_buf[kHeight * kFullWidth];
static uint32_t hd_buf[kHeight * kScale * kFullWidth * kScale];
static int meter_sprite_x = 100;

bool KdcHdTexturesEnabled(void) { return true; }
bool KdcCheckerNeedsContext(const uint16_t words[16]) { (void)words; return false; }
bool KdcOrbitComposing(void) { return false; }

bool KdcHdNearestTile(const PpuHdTileQuery *q, uint8_t *indices, void *context)
{
    (void)context;
    memset(indices, 2, (size_t)(8 * q->scale) * (8 * q->scale));
    return true;
}

bool KdcHdSmoothTile(const PpuHdTileQuery *q, uint8_t *indices, void *context)
{
    return KdcHdNearestTile(q, indices, context);
}

void KdcVisitOrbitSprites(const struct Ppu *ppu, const uint8_t *ram,
                          KdcOrbitSpriteVisitor visit, void *context)
{
    (void)ppu;
    (void)ram;
    for (unsigned i = 0; i < 4; ++i) {
        KdcOrbitSprite s = {
            .offset_x = 20 + (int)(i & 1) * 16,
            .offset_y = 179 + (int)(i >> 1) * 16,
            .attributes = (uint16_t)(0x33a0 + i * 2),
            .large = 1,
            .world = false,
            .unresolved = false,
        };
        visit(&s, context);
    }
    KdcOrbitSprite meter = {
        .offset_x = meter_sprite_x,
        .offset_y = 100,
        .attributes = 0x3000,
        .large = 0,
        .world = false,
        .unresolved = false,
    };
    visit(&meter, context);
}

static uint32_t HdAt(int screen_x, int line)
{
    int col = kExtra + screen_x;
    assert(col >= 0 && col < kFullWidth && line >= 1 && line <= kHeight);
    return hd_buf[((line - 1) * kScale) * (kFullWidth * kScale) + col * kScale];
}

static uint32_t Rgb(const Ppu *p, uint16_t color)
{
    return (uint32_t)p->brightnessMult[color & 31] << 16 |
           (uint32_t)p->brightnessMult[(color >> 5) & 31] << 8 |
           (uint32_t)p->brightnessMult[(color >> 10) & 31];
}

int main(void)
{
    Ppu *p = ppu_init();
    assert(p);
    ppu_reset(p);
    g_snes->ppu = p;

    p->bgmode = 9;
    p->bgXsc[0] = 0x51;
    p->bgXsc[1] = 0x59;
    p->bgXsc[2] = 0x3c;
    p->bgTileAdr = 0x400;
    p->screenEnabled[0] = 23;
    p->screenWindowed[0] = 19;
    p->inidisp = 15;
    p->obsel = 3;
    p->windowsel = 0x220000;
    p->cgwsel = 0x10;
    p->cgadsub = 0x33;
    p->fixedColor = 0x7fff;
    p->window1left = 128;
    p->window1right = 127;
    PpuSetExtraSpace(p, kExtra);

    /* Clean BG3 scenery tile 1 decodes to native palette index 1; HD fill outputs index 2. */
    p->cgram[0] = 0x0000;
    p->cgram[1] = 0x001f;
    p->cgram[2] = 0x03e0;
    for (unsigned r = 0; r < 8; ++r) p->vram[0x4008 + r] = 0x00ff;

    /* Clean BG3 scenery map in WRAM and VRAM uses tile 1. */
    for (unsigned i = 0; i < 1024; ++i) {
        p->vram[0x3c00 + i] = 1;
        g_ram[0xdadc + i * 2] = 1;
        g_ram[0xdadc + i * 2 + 1] = 0;
    }

    /* 1P expanded HUD signature: border tile at (2, 22) and 4 portrait OAM slots. */
    g_ram[0x17ff] = 0;
    p->vram[0x3c00 + 22 * 32 + 2] = 0x3519;
    p->vram[0x3c00 + 23 * 32 + 20] = 0x3520;
    for (unsigned r = 0; r < 8; ++r)
        p->vram[0x4000 + (0x3520 & 1023) * 8 + r] = 0xffff;
    for (unsigned i = 0; i < 4; ++i) {
        int x = 20 + (int)(i & 1) * 16;
        int y = 179 + (int)(i >> 1) * 16;
        p->oam[i * 2] = (uint16_t)(x | (y << 8));
        p->oam[i * 2 + 1] = (uint16_t)(0x33a0 + i * 2);
        p->highOam[0] |= (uint8_t)(2u << (i * 2));
    }

    PpuBeginDrawing(p, (uint8_t *)native_buf, kFullWidth * 4, kPpuRenderFlags_NewRenderer);
    assert(PpuBindMode7HdSurface(p, hd_buf, sizeof(hd_buf), kFullWidth * kScale * 4,
                                 kFullWidth, kHeight, kScale));
    KdcPrepareCourseView(p, g_ram, kExtra);
    KdcPrepareHud(p, g_ram, kExtra, true);
    assert(KdcHdScale() == kScale);

    /* Line 1 prepares line 0 (window 128..127) and triggers HdLinePolicy collection. */
    p->window1left = 128;
    p->window1right = 127;
    ppu_drawCapturedLine(p, 1, true);
    ppu_drawCapturedLine(p, 102, true);

    /* Line 185 prepares line 184 (expanded 1P window 56..235). */
    p->window1left = 56;
    p->window1right = 235;
    ppu_drawCapturedLine(p, 185, true);

    uint32_t hd_color = Rgb(p, p->cgram[2]);
    uint32_t native_color = Rgb(p, p->cgram[1]);
    int left = 12 - kExtra; /* -84 */
    assert(hd_color != native_color);
    /* 1. Relocated portrait (left .. left+31) stays native; unshifted portrait x=40 gets HD. */
    assert(HdAt(left + 4, 185) != hd_color);
    assert(HdAt(40, 185) == hd_color);

    /* 2. Relocated Edge HUD card (left+36) stays native; old center HUD x=128 gets HD. */
    assert(HdAt(left + 36, 185) != hd_color);
    assert(HdAt(128, 185) == hd_color);

    /* 3. Re-rendering from line 1 without advancing snes_frame_counter refreshes hud_rects. */
    assert(HdAt(102, 102) == native_color);
    assert(HdAt(142, 102) == hd_color);
    meter_sprite_x = 140;
    p->window1left = 128;
    p->window1right = 127;
    ppu_drawCapturedLine(p, 1, true);
    ppu_drawCapturedLine(p, 102, true);
    assert(HdAt(102, 102) == hd_color);
    assert(HdAt(142, 102) == native_color);

    free(p);
    return 0;
}
