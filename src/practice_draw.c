/* Decode the typeface from the user's live title-menu VRAM. No extracted
 * artwork ships with this panel; its geometry is drawn by the host. */
#include "practice.h"
#include "snes/ppu.h"
#include "snes_overlay_draw.h"
#include <string.h>

static uint8_t font[64][8][8];
static bool font_ready;

void KdcPracticeCaptureFont(const Ppu *ppu)
{
    if (!KdcPracticeCurrentView()->title || PPU_forcedBlank(ppu) ||
        PPU_brightness(ppu) != 15) return;
    const uint16_t *vram = PpuRenderVram(ppu);
    unsigned base = PPU_bgTileAdr(ppu, 2);
    for (unsigned c = 0; c < 64; ++c)
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x) {
                uint16_t bits = vram[(base + c * 8 + y) & 0x7fff];
                unsigned index = ((bits >> (7 - x)) & 1) | ((bits >> (14 - x)) & 2);
                uint16_t color = ppu->cgram[index];
                unsigned light = (color & 31) + ((color >> 5) & 31) + ((color >> 10) & 31);
                font[c][y][x] = index ? (light > 40 ? 2 : 1) : 0;
            }
    font_ready = true;
}

typedef struct { uint32_t *pixels; int stride, height, left; } Canvas;
static void Box(Canvas *c, int x, int y, int w, int h, uint32_t color)
{
    snes_ovl_fill_rect(c->pixels, c->stride, c->height, c->left + x, y, w, h, color);
}
static void Text(Canvas *c, int x, int y, const char *s, uint32_t color)
{
    if (!font_ready) {
        snes_ovl_draw_text(c->pixels, c->stride, c->height, c->left + x, y, s, color, 1);
        return;
    }
    for (; *s; ++s, x += 8) {
        unsigned glyph = *s == ' ' ? 31u : (unsigned char)*s - 32u;
        if (glyph >= 64) continue;
        for (int py = 0; py < 8; ++py)
            for (int px = 0; px < 8; ++px) {
                uint8_t ink = font[glyph][py][px];
                if (ink) Box(c, x + px, y + py, 1, 1, ink == 2 ? color : 0xff161038);
            }
    }
}
static void Center(Canvas *c, int y, const char *text, uint32_t color)
{
    Text(c, (256 - (int)strlen(text) * 8) / 2, y, text, color);
}

void KdcPracticeDraw(uint8_t *dst, size_t pitch, int width, int height)
{
    const KdcPracticeView *v = KdcPracticeCurrentView();
    if (!KdcPracticeVisible() || width < 256 || height < 224 || pitch < (size_t)width * 4) return;
    Canvas c = {(uint32_t *)dst, (int)(pitch / 4), height, (width - 256) / 2};
    const uint32_t white = 0xffffffff, yellow = 0xffffe890, pink = 0xffffb8de;
    if (!v->screen) {
        Box(&c, 84, 3, 88, 12, 0xff30206a);
        Center(&c, 5, "PRACTICE", yellow);
        return;
    }
    Box(&c, 14, 22, 230, 184, 0xff161038);
    Box(&c, 12, 20, 230, 184, 0xffffcf68);
    Box(&c, 14, 22, 226, 180, 0xff6654ac);
    for (int y = 24; y < 200; y += 8)
        for (int x = 16; x < 240; x += 8)
            Box(&c, x, y, 8, 8, ((x + y) / 8) % 2 ? 0xff302468 : 0xff382c78);
    Box(&c, 24, 30, 208, 22, 0xffa84480);
    Center(&c, 37, "HOLE PRACTICE", white);
    if (v->screen == 1) {
        static const char *const names[] = {"1P NORMAL", "1P EXTRA", "2P NORMAL", "2P EXTRA"};
        Text(&c, 32, 62, "MODE", yellow);
        if (v->row == 0) Box(&c, 88, 59, 136, 14, 0xffa84480);
        Text(&c, 112, 62, names[v->mode], white);
        for (int i = 0; i < 4; ++i) {
            Box(&c, 96 + i, 65 - i, 1, i * 2 + 1, pink);
            Box(&c, 211 - i, 65 - i, 1, i * 2 + 1, pink);
        }
        for (int row = 1; row <= 2; ++row) {
            int y = row == 1 ? 86 : 122;
            Text(&c, 32, y, row == 1 ? "COURSE" : "HOLE", yellow);
            int count = row == 1 && v->mode >= 2 ? 4 : 8;
            int selected = row == 1 ? v->course : v->hole;
            for (int i = 0; i < count; ++i) {
                int x = 32 + i * 24;
                Box(&c, x - 3, y + 11, 20, 16, i == selected ?
                    (v->row == row ? 0xffbf508a : 0xff6856aa) : 0xff21184a);
                char number[] = {(char)('1' + i), 0};
                Text(&c, x + 3, y + 15, number, i == selected ? yellow : white);
            }
        }
        if (v->row == 3) Box(&c, 60, 160, 136, 17, 0xffa84480);
        Center(&c, 165, "PLAY THIS HOLE", v->row == 3 ? yellow : white);
    } else {
        static const char *const options[] = {"RESUME", "RESTART HOLE", "CHOOSE A HOLE", "EXIT PRACTICE"};
        for (int row = 0; row < 4; ++row) {
            int y = 72 + row * 24;
            if (v->row == row) Box(&c, 40, y - 4, 176, 17, 0xffa84480);
            Center(&c, y, options[row], v->row == row ? yellow : white);
        }
    }
    Center(&c, 187, "A SELECT   B BACK", pink);
    if (v->error) {
        Box(&c, 8, 207, 240, 13, 0xff302468);
        Center(&c, 210, v->error, yellow);
    }
}

void KdcPracticeDrawHd(uint8_t *dst, size_t pitch, int width, int height, unsigned scale)
{
    static uint32_t panel[256 * 224];
    memset(panel, 0, sizeof(panel));
    KdcPracticeDraw((uint8_t *)panel, 256 * 4, 256, 224);
    snes_ovl_blit_panel_rect(dst, (int)pitch, width * (int)scale, height * (int)scale,
                            panel, 256, 224, (width - 256) * (int)scale / 2, 0,
                            256 * (int)scale, 224 * (int)scale);
}
