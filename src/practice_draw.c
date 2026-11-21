/* Decode the typeface from the user's live title-menu VRAM. No extracted
 * artwork ships with this panel; its geometry is drawn by the host. */
#include "practice.h"
#include "course_pack.h"
#include "hd_textures.h"
#include "hd_ui.h"
#include "course_hud.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include "snes_overlay_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t font[64][8][8];
static bool font_ready;
extern Snes *g_snes;

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

typedef struct { uint32_t *pixels; int stride, height, left, scale; } Canvas;

static void Box(Canvas *c, int x, int y, int w, int h, uint32_t color)
{
    int s = c->scale ? c->scale : 1;
    snes_ovl_fill_rect(c->pixels, c->stride, c->height, (c->left + x) * s, y * s, w * s, h * s, color);
}

static void RoundedBox(Canvas *c, int x, int y, int w, int h, uint32_t color)
{
    int s = c->scale ? c->scale : 1;
    if (s <= 1) {
        Box(c, x, y, w, h, color);
        return;
    }
    int x0 = (c->left + x) * s, y0 = y * s, sw = w * s, sh = h * s, r = s * 2;
    for (int py = 0; py < sh; ++py) {
        int dy = py < r ? (r - 1 - py) : (py >= sh - r ? py - (sh - r) : 0);
        int inset = 0;
        while (inset < r && (r - 1 - inset) * (r - 1 - inset) + dy * dy > r * r) ++inset;
        snes_ovl_fill_rect(c->pixels, c->stride, c->height, x0 + inset, y0 + py, sw - 2 * inset, 1, color);
    }
}

static void BlitArt(Canvas *c, int x, int y, const uint32_t pixels[1024])
{
    x=(c->left+x)*4;
    y*=4;
    for (int j=0;j<32;++j) {
        if (y+j<0 || y+j>=c->height) continue;
        uint32_t *row=c->pixels+(size_t)(y+j)*c->stride;
        for (int i=0;i<32;++i) if (x+i>=0 && x+i<c->stride)
            row[x+i]=KdcHdUiBlend(row[x+i],pixels[j*32+i]);
    }
}

static void DrawExtraGlyph(Canvas *c, int x, int y, char ch, uint32_t color)
{
    /* Minimal host-drawn fallback for punctuation absent from the guest's
     * title font sheet (apostrophe and ampersand in custom course names). */
    const uint32_t shadow = 0xff161038;
    if (ch == '\'') {
        Box(c, x + 3, y + 1, 3, 4, shadow);
        Box(c, x + 3, y + 1, 2, 3, color);
    } else if (ch == '&') {
        static const uint8_t rows[7] = {0x38, 0x44, 0x38, 0x54, 0x48, 0x44, 0x3a};
        for (int py = 0; py < 7; ++py)
            for (int px = 0; px < 8; ++px)
                if (rows[py] & (0x80u >> px)) {
                    Box(c, x + px + 1, y + py + 1, 1, 1, shadow);
                    Box(c, x + px, y + py, 1, 1, color);
                }
    }
}
static void Text(Canvas *c, int x, int y, const char *s, uint32_t color)
{
    int scale = c->scale ? c->scale : 1;
    if (scale==4) {
        static struct { uint32_t color, pixels[1024]; } glyphs[128];
        for (; *s; ++s, x+=8) {
            unsigned ch=(unsigned char)*s;
            if (ch>=128 || ch==' ') continue;
            if (glyphs[ch].color!=color) {
                KdcHdUiGlyph(ch,color,glyphs[ch].pixels);
                glyphs[ch].color=color;
            }
            BlitArt(c,x,y,glyphs[ch].pixels);
        }
        return;
    }
    if (!font_ready) {
        snes_ovl_draw_text(c->pixels, c->stride, c->height, (c->left + x) * scale, y * scale, s, color, scale);
        return;
    }
    for (; *s; ++s, x += 8) {
        unsigned glyph = KdcPracticeGuestGlyph(*s);
        if (glyph >= 64) {
            DrawExtraGlyph(c, x, y, *s, color);
            continue;
        }
        for (int py = 0; py < 8; ++py)
            for (int px = 0; px < 8; ++px) {
                uint8_t ink = font[glyph][py][px];
                if (ink) Box(c, x + px, y + py, 1, 1, ink == 2 ? color : 0xff161038);
            }
    }
}
/* A 4x7 triangle centred on y, apex pointing left unless right is set. The
 * guest font sheet carries chevrons at tiles 10 and 12, but the picker draws
 * solid triangles so both arrow pairs match the mode row's style. */
static void Triangle(Canvas *c, int x, int y, bool right, uint32_t color)
{
    int s = c->scale ? c->scale : 1;
    if (s == 4) {
        for (int i = 0; i < 16; ++i) {
            int col = right ? (x * 4 + 15 - i) : (x * 4 + i);
            snes_ovl_fill_rect(c->pixels, c->stride, c->height, c->left * 4 + col, y * 4 + 2 - i, 1, i * 2 + 1, color);
        }
        return;
    }
    for (int i = 0; i < 4; ++i) {
        Box(c, right ? x + 3 - i : x + i, y - i, 1, i * 2 + 1, color);
    }
}
static void Center(Canvas *c, int y, const char *text, uint32_t color)
{
    Text(c, (256 - (int)strlen(text) * 8) / 2, y, text, color);
}

/* USA conditions portraits: 849E07 selects two 256-byte DMA strips.
 * Decode directly from the player's verified cartridge, including its palette;
 * no artwork is bundled or written into guest VRAM. */
static unsigned RomByte(unsigned address)
{
    return g_snes->cart->rom[((address >> 16) & 0x7f) * 0x8000 + (address & 0x7fff)];
}
static unsigned RomWord(unsigned address)
{
    return RomByte(address) | (RomByte(address + 1) << 8);
}
/* The game's own 4bpp character, each pixel repeated 4x4. */
static void NearestTile(const PpuHdTileQuery *q, uint32_t pixels[1024])
{
    for (unsigned y = 0; y < 32; ++y)
        for (unsigned x = 0; x < 32; ++x) {
            unsigned row = y / 4, bit = 7 - x / 4;
            unsigned lo = q->words[row], hi = q->words[row + 8];
            unsigned ink = ((lo >> bit) & 1) | ((lo >> (bit + 8)) & 1) << 1 |
                ((hi >> bit) & 1) << 2 | ((hi >> (bit + 8)) & 1) << 3;
            unsigned c = q->colors[ink];
            pixels[y * 32 + x] = !ink ? 0 : 0xff000000u | ((c & 31) * 255 / 31) << 16 |
                ((c >> 5 & 31) * 255 / 31) << 8 | (c >> 10 & 31) * 255 / 31;
        }
}
static void Ability(Canvas *c, int x, int y, unsigned ability)
{
    if (!g_snes || !g_snes->cart || g_snes->cart->romImageSize != 0x100000 ||
        ability >= KDC_PRACTICE_ABILITIES) return;
    unsigned script = 0x840000 | RomWord(0x849e07 + ability * 4);
    if (c->scale==4) {
        static uint32_t tiles[16][1024];
        static int cached=-1;
        if ((int)ability!=cached) {
            PpuHdTileQuery q={.scale=4,.layer=4,.bpp=4};
            for (unsigned k=1;k<16;++k) q.colors[k]=RomWord(0x94a849+k*2);
            for (unsigned i=0;i<16;++i) {
                unsigned strip=script+((i/4)%2)*8;
                unsigned source=RomWord(strip+3)|(RomByte(strip+5)<<16);
                unsigned addr=source+((i/8)*4+i%4)*32;
                for (unsigned k=0;k<16;++k) q.words[k]=RomWord(addr+2*k);
                if (!KdcHdUiRgba(&q,tiles[i])) NearestTile(&q,tiles[i]);
            }
            cached=(int)ability;
        }
        for (unsigned i=0;i<16;++i) BlitArt(c,x+8*(i%4),y+8*(i/4),tiles[i]);
        return;
    }
    uint32_t pal[16] = {0};
    for (unsigned i = 1; i < 16; ++i) {
        unsigned color = RomWord(0x94a849 + i * 2);
        unsigned r = color & 31, g = (color >> 5) & 31, b = (color >> 10) & 31;
        pal[i] = 0xff000000u | ((r * 255 / 31) << 16) | ((g * 255 / 31) << 8) | (b * 255 / 31);
    }
    for (unsigned py = 0; py < 32; ++py) {
        unsigned strip = script + (py % 16 / 8) * 8;
        unsigned source = RomWord(strip + 3) | (RomByte(strip + 5) << 16);
        for (unsigned px = 0; px < 32; ++px) {
            unsigned tile = (py / 16 * 4 + px / 8) * 32;
            unsigned row = source + tile + (py % 8) * 2;
            unsigned bit = 7 - px % 8;
            unsigned ink = ((RomByte(row) >> bit) & 1) |
                (((RomByte(row + 1) >> bit) & 1) << 1) |
                (((RomByte(row + 16) >> bit) & 1) << 2) |
                (((RomByte(row + 17) >> bit) & 1) << 3);
            if (ink)
                Box(c, x + (int)px, y + (int)py, 1, 1, pal[ink]);
        }
    }
}

static const KdcCoursePack *FindPracticeExternalPack(int mode, bool *two_player)
{
    if (mode < 4 || !KdcCoursePacksMenuEnabled()) return NULL;
    unsigned target = (unsigned)(mode - 4) / 2u;
    if (two_player) *two_player = ((mode - 4) & 1) != 0;
    unsigned seen = 0u, total = KdcCoursePackCount();
    for (unsigned i = 0; i < total; ++i) {
        const KdcCoursePack *p = KdcCoursePackGet(i);
        if (p && p->external) {
            if (seen == target) return p;
            ++seen;
        }
    }
    return NULL;
}

static void DrawPracticeCanvas(Canvas *c)
{
    const KdcPracticeView *v = KdcPracticeCurrentView();
    const uint32_t white = 0xffffffff, yellow = 0xffffe890, pink = 0xffffb8de;
    if (!v->screen) {
        /* CPU and custom 2P matches do not show the practice badge. */
        int y = g_snes && KdcHudTopStripActive(g_snes->ppu) ? kKdcHudMultiTopY1 + 2 : 3;
        RoundedBox(c, 84, y, 88, 12, 0xff30206a);
        Center(c, y + 2, "PRACTICE", yellow);
        return;
    }
    RoundedBox(c, 14, 22, 230, 184, 0xff161038);
    RoundedBox(c, 12, 20, 230, 184, 0xffffcf68);
    RoundedBox(c, 14, 22, 226, 180, 0xff6654ac);
    for (int y = 24; y < 200; y += 8)
        for (int x = 16; x < 240; x += 8)
            Box(c, x, y, 8, 8, ((x + y) / 8) % 2 ? 0xff302468 : 0xff382c78);
    RoundedBox(c, 24, 30, 208, 22, 0xffa84480);
    const char *banner = v->screen == 3
                             ? (v->cpu ? "1P VS CPU COURSES" : "MORE 2P COURSES")
                             : v->cpu ? "1P VS CPU"
                             : v->custom_match ? "2P MATCH"
                                               : "HOLE PRACTICE";
    Center(c, 37, banner, white);
    if (v->screen == 1) {
        static const char *const names[] = {"1P NORMAL", "1P EXTRA", "2P NORMAL", "2P EXTRA"};
        bool ext_2p = false;
        const KdcCoursePack *ext = FindPracticeExternalPack(v->mode, &ext_2p);
        char mode_label[24];
        if (ext) {
            snprintf(mode_label, sizeof(mode_label), "%s %s",
                     ext->short_name, ext_2p ? "2P" : "1P");
        } else {
            snprintf(mode_label, sizeof(mode_label), "%s",
                     names[v->mode >= 0 && v->mode < 4 ? v->mode : 0]);
        }
        Text(c, 32, 62, "MODE", yellow);
        if (v->row == 0) RoundedBox(c, 88, 59, 136, 14, 0xffa84480);
        Text(c, 108, 62, mode_label, white);
        Triangle(c, 96, 65, false, pink);
        Triangle(c, 208, 65, true, pink);
        if (ext) {
            char hdr[32];
            int ci = (v->course >= 0 && (unsigned)v->course < ext->course_count) ? v->course : 0;
            snprintf(hdr, sizeof(hdr), "COURSE %02d/%02u", ci + 1, ext->course_count);
            Text(c, 32, 82, hdr, yellow);
            RoundedBox(c, 28, 93, 200, 16, v->row == 1 ? 0xffa84480 : 0xff21184a);
            Triangle(c, 34, 100, false, pink);
            Center(c, 97, ext->courses[ci].name, v->row == 1 ? yellow : white);
            Triangle(c, 218, 100, true, pink);
        } else {
            Text(c, 32, 82, "COURSE", yellow);
            int count = v->mode >= 2 ? 4 : 8;
            for (int i = 0; i < count; ++i) {
                int x = 32 + i * 24;
                RoundedBox(c, x - 3, 93, 20, 16, i == v->course ?
                    (v->row == 1 ? 0xffbf508a : 0xff6856aa) : 0xff21184a);
                char number[] = {(char)('1' + i), 0};
                Text(c, x + 3, 97, number, i == v->course ? yellow : white);
            }
        }
        Text(c, 32, 112, "HOLE", yellow);
        for (int i = 0; i < 8; ++i) {
            int x = 32 + i * 24;
            RoundedBox(c, x - 3, 123, 20, 16, i == v->hole ?
                (v->row == 2 ? 0xffbf508a : 0xff6856aa) : 0xff21184a);
            char number[] = {(char)('1' + i), 0};
            Text(c, x + 3, 127, number, i == v->hole ? yellow : white);
        }
        Text(c, 32, 152, "ABILITY", yellow);
        if (v->row == 3) RoundedBox(c, 92, 143, 132, 34, 0xffa84480);
        Ability(c, 96, 144, (unsigned)v->ability);
        Triangle(c, 128, 159, false, pink);
        Text(c, 140, 156, KdcPracticeAbilityName((unsigned)v->ability), white);
        Triangle(c, 216, 159, true, pink);
        if (v->row == 4) RoundedBox(c, 60, 178, 136, 14, 0xffa84480);
        Center(c, 181, "PLAY THIS HOLE", v->row == 4 ? yellow : white);
    } else if (v->screen == 3) {
        const KdcCoursePack *p = KdcCoursePackGet((unsigned)(v->pack >= 0 ? v->pack : 0));
        unsigned ci = (p && v->pack_course >= 0 && (unsigned)v->pack_course < p->course_count)
                          ? (unsigned)v->pack_course : 0u;
        char pack_hdr[32], course_hdr[32];
        snprintf(pack_hdr, sizeof(pack_hdr), "PACK %d/%u",
                 v->pack + 1, KdcCoursePackCount());
        Text(c, 32, 64, pack_hdr, yellow);
        RoundedBox(c, 28, 76, 200, 18, v->row == 0 ? 0xffa84480 : 0xff21184a);
        Triangle(c, 34, 84, false, pink);
        Center(c, 81, p ? p->name : "NONE", v->row == 0 ? yellow : white);
        Triangle(c, 218, 84, true, pink);

        snprintf(course_hdr, sizeof(course_hdr), "COURSE %02u/%02u",
                 ci + 1u, p ? p->course_count : 0u);
        Text(c, 32, 108, course_hdr, yellow);
        RoundedBox(c, 28, 120, 200, 18, v->row == 1 ? 0xffa84480 : 0xff21184a);
        Triangle(c, 34, 128, false, pink);
        Center(c, 125, (p && p->course_count) ? p->courses[ci].name : "NONE",
               v->row == 1 ? yellow : white);
        Triangle(c, 218, 128, true, pink);

        if (v->row == 2) RoundedBox(c, 48, 162, 160, 16, 0xffa84480);
        Center(c, 166, "PLAY THIS COURSE", v->row == 2 ? yellow : white);
    } else {
        static const char *const options[] = {"RESUME", "RESTART HOLE", "CHOOSE A HOLE", "EXIT PRACTICE"};
        static const char *const match[] = {"RESUME", "EXIT MATCH"};
        bool is_match = v->cpu || v->custom_match;
        int rows = is_match ? 2 : 4;
        for (int row = 0; row < rows; ++row) {
            int y = 72 + row * 24;
            if (v->row == row) RoundedBox(c, 40, y - 4, 176, 17, 0xffa84480);
            Center(c, y, is_match ? match[row] : options[row], v->row == row ? yellow : white);
        }
    }
    Center(c, 195, (v->screen == 1 || v->screen == 3) && v->active
                        ? "A SELECT   B EXIT" : "A SELECT   B BACK", pink);
    if (v->error) {
        RoundedBox(c, 8, 207, 240, 13, 0xff302468);
        Center(c, 210, v->error, yellow);
    }
}

void KdcPracticeDraw(uint8_t *dst, size_t pitch, int width, int height)
{
    if (!KdcPracticeVisible() || width < 256 || height < 224 || pitch < (size_t)width * 4) return;
    Canvas c = {(uint32_t *)dst, (int)(pitch / 4), height, (width - 256) / 2, 1};
    DrawPracticeCanvas(&c);
}

void KdcPracticeDrawHd(uint8_t *dst, size_t pitch, int width, int height, unsigned scale)
{
    if (scale == 4 && KdcHdUiEnabled()) {
        if (!KdcPracticeVisible() || width < 256 || height < 224 || pitch < (size_t)width * scale * 4) return;
        Canvas c = {(uint32_t *)dst, (int)(pitch / 4), height * (int)scale, (width - 256) / 2, (int)scale};
        DrawPracticeCanvas(&c);
        return;
    }
    static uint32_t panel[256 * 224];
    memset(panel, 0, sizeof(panel));
    KdcPracticeDraw((uint8_t *)panel, 256 * 4, 256, 224);
    snes_ovl_blit_panel_rect(dst, (int)pitch, width * (int)scale, height * (int)scale,
                            panel, 256, 224, (width - 256) * (int)scale / 2, 0,
                            256 * (int)scale, 224 * (int)scale);
}
