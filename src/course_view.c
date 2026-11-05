#include "course_view.h"
#include "snes/ppu.h"
#include "snes/ws_shadow.h"

typedef struct CourseView {
    Ppu *ppu;
    const uint8_t *ram;
} CourseView;

static CourseView view;

static bool HasCourseLayout(const Ppu *ppu)
{
    /* The preview uses bounded $50/$58 maps, not the streamed course. */
    return PPU_mode(ppu) == 1 && ppu->bgXsc[0] == 0x51 &&
           ppu->bgXsc[1] == 0x59 && (ppu->bgTileAdr & 0xff) == 0 &&
           !PPU_bigTiles(ppu, 0) && !PPU_bigTiles(ppu, 1);
}

bool KdcIsCourse(const Ppu *ppu)
{
    /* The scorecard reuses the course maps and character base, but replaces
     * BG1 graphics and disables the second terrain plane. Map addresses
     * alone cannot authorize terrain lookup or recovered gameplay sprites. */
    return HasCourseLayout(ppu) &&
           ((ppu->screenEnabled[0] | ppu->screenEnabled[1]) & 2) != 0;
}

static bool IsCourseSelect(const Ppu *ppu)
{
    /* USA selection island, cloud plane and split sky/course-label map. */
    return PPU_mode(ppu) == 1 && ppu->bgXsc[0] == 0x51 &&
           ppu->bgXsc[1] == 0x59 && ppu->bgXsc[2] == 0x61 &&
           (ppu->bgTileAdr == 0x230 || ppu->bgTileAdr == 0x240) &&
           !(ppu->bgmode & 0x70);
}

static bool IsTitle(const Ppu *ppu)
{
    return PPU_mode(ppu) == 1 && ppu->bgXsc[0] == 0x40 &&
           ppu->bgXsc[1] == 0x48 && ppu->bgXsc[2] == 0x58 &&
           ppu->bgTileAdr == 0x500 && !(ppu->bgmode & 0x70);
}

static bool IsMembers(const Ppu *ppu)
{
    /* The name editor changes the foreground character banks, retaining
     * the same scrolling, tileable BG3 star wallpaper. */
    return PPU_mode(ppu) == 1 && ppu->bgXsc[0] == 0x43 &&
           ppu->bgXsc[1] == 0x53 && ppu->bgXsc[2] == 0x2c &&
           (ppu->bgTileAdr == 0x200 || ppu->bgTileAdr == 0x233) &&
           !(ppu->bgmode & 0x70);
}

static bool IsCourseIntro(const Ppu *ppu)
{
    return PPU_mode(ppu) == 1 && ppu->bgXsc[0] == 0x50 &&
           ppu->bgXsc[1] == 0x58 && ppu->bgXsc[2] == 0x3c &&
           ppu->bgTileAdr == 0x400 && !(ppu->bgmode & 0x70);
}

static uint16_t IntroWallpaper(const uint16_t *vram, unsigned column, unsigned row)
{
    /* BG2's frame is baked into its map. Rebuild only the margin source:
     * keep the outer wallpaper, restore stars occluded by the frame, and
     * decorate its hole using complete motifs already present below it.
     * These are USA map coordinates, not copies of character graphics. */
    unsigned index = row * 32 + column;
    if (column < 2 || column > 29 || row < 2 || row > 20)
        return vram[0x5800 + index];
    static const uint8_t large[][2] = {
        {13, 1}, {28, 6}, {1, 11}, {19, 19},
        {6, 5}, {20, 10}, {10, 15},
    };
    static const uint8_t small[][2] = {{28, 18}, {17, 5}, {5, 16}};
    for (unsigned i = 0; i < sizeof(large) / sizeof(large[0]); ++i) {
        unsigned x = column - large[i][0], y = row - large[i][1];
        if (x < 3 && y < 3) return vram[0x5800 + (23 + y) * 32 + 1 + x];
    }
    for (unsigned i = 0; i < sizeof(small) / sizeof(small[0]); ++i) {
        unsigned x = column - small[i][0], y = row - small[i][1];
        if (x < 2 && y < 2) return vram[0x5800 + (25 + y) * 32 + 6 + x];
    }
    return vram[0x5800]; /* Solid wallpaper, with its live palette flags. */
}

static uint16_t MenuTile(const Ppu *ppu, int screen_x, uint32_t scrolled_y,
                         uint16_t scroll_x)
{
    bool flip;
    unsigned column = KdcReflectedColumn(screen_x, scroll_x, &flip) & 31;
    unsigned row = (scrolled_y >> 3) & 31;
    const uint16_t *vram = PpuRenderVram(ppu);
    if (IsCourseIntro(ppu))
        return IntroWallpaper(vram, column, row) ^ (flip ? 0x4000 : 0);
    /* Title: one checkerboard frame in the native field, surrounded by
     * stars. Reflect the visible star strip vertically above its own top
     * edge; no checkerboard panels or lettering enter the side margins. */
    bool flip_y = false;
    if (row < 17) {
        unsigned phase = (row + 5) % 22;
        flip_y = phase >= 11;
        row = 17 + (flip_y ? 21 - phase : phase);
    }
    return vram[0x4800 + row * 32 + column] ^
           (flip ? 0x4000 : 0) ^ (flip_y ? 0x8000 : 0);
}

static uint16_t CourseTile(int layer, int screen_x, uint32_t scrolled_y,
                            uint16_t scroll_x, uint16_t native_tile, void *context)
{
    const CourseView *source = context;
    if (layer == 1 && (IsTitle(source->ppu) || IsCourseIntro(source->ppu))) {
        if (PPU_mosaicEnabled(source->ppu, layer) && PPU_mosaicSize(source->ppu) > 1)
            return native_tile;
        return MenuTile(source->ppu, screen_x, scrolled_y, scroll_x);
    }
    bool selection = IsCourseSelect(source->ppu);
    if (!selection && !HasCourseLayout(source->ppu)) return native_tile;
    /* Mosaic groups may span several tiles and the native boundary. Keep
     * their hardware source during the effect instead of changing a group. */
    if (PPU_mosaicEnabled(source->ppu, layer) && PPU_mosaicSize(source->ppu) > 1)
        return native_tile;
    if (layer == 2) {
        /* USA course scenery: the decompressed 32x32 map survives in WRAM
         * before the HUD is composited into VRAM. Its opposite edges do
         * not join. Reflect the clean visible strip at its tile boundaries
         * instead, preserving the native tiles and live raster phase. */
        if (!selection && (source->ppu->bgXsc[2] != 0x3c ||
            PPU_bgTileAdr(source->ppu, 2) != 0x4000 ||
            PPU_bigTiles(source->ppu, 2))) return native_tile;
        /* Fine scroll exposes 33 tiles rather than 32. Reflect outside
         * those complete tiles so a native-edge chunk never changes.
         * The period is independent of the requested viewport width. */
        bool reflected;
        unsigned column = KdcReflectedColumn(screen_x, scroll_x, &reflected);
        unsigned row = scrolled_y >> 3;
        unsigned index = (row & 31) * 32 + (column & 31);
        /* Selection keeps clean sky in the first VRAM page; raster scroll
         * switches the native field to its label page near the bottom. */
        uint16_t tile = selection ? PpuRenderVram(source->ppu)[0x6000 + index] :
                                  KdcReadWord(source->ram, 0xdadc + 2 * index);
        /* Toggle, rather than set: source artwork already uses flips. */
        return tile ^ (reflected ? 0x4000 : 0);
    }
    if (!selection && !KdcIsCourse(source->ppu)) return native_tile;
    /* Keep live raster fine scroll; WRAM only disambiguates its period. */
    int x = KdcCourseScroll(scroll_x, KdcReadWord(source->ram, 0x86)) + screen_x;
    int raster_y = source->ppu->vScroll[layer];
    int y = (int)scrolled_y - raster_y +
            KdcCourseScroll(raster_y, KdcReadWord(source->ram, 0x88));
    return selection ? KdcCourseSelectTile(source->ram, layer, KdcTileCoordinate(x), KdcTileCoordinate(y)) :
                       KdcCourseTile(source->ram, layer, KdcTileCoordinate(x), KdcTileCoordinate(y));
}

void KdcPrepareCourseView(Ppu *ppu, const uint8_t *ram, int extra)
{
    static const uint8_t no_hints[16] = {0};
    view = (CourseView){ppu, ram};
    WsShadowSetTileProvider(7, extra ? CourseTile : NULL, &view);
    /* In streamed courses BG3 combines scenery and a tile-based HUD.
     * The runtime otherwise assumes BG3 is a centered status bar. Visible
     * SNES scanlines start at 1; zero disables the widening policy. */
    bool world_scene = KdcIsCourse(ppu) || IsCourseSelect(ppu);
    /* Scorecards retain course scenery even though terrain is not live. */
    bool scenery = HasCourseLayout(ppu) || IsCourseSelect(ppu) || IsMembers(ppu);
    PpuSetWidescreenBg3Widen(ppu, extra && scenery ? 1 : 0);
    /* This ROM still emits ordinary signed 9-bit OAM coordinates. Do not
     * reinterpret left-edge sprites as right-margin objects, or include
     * hidden left-side objects in the hardware sprite budget. */
    PpuWsSetOamRightHints(ppu, extra ? no_hints : NULL);
    PpuWsSetOamLeftHints(ppu, extra ? no_hints : NULL);
    ppu->wsOamMotionGraceOn = extra ? 0 : 1;
    /* Bounded foregrounds stay centered; decorative BG2/backdrop can fill
     * the sides. The provider checks the live raster signature itself. */
    PpuSetWidescreenLayerClamp(ppu, extra && !world_scene ?
                              (IsMembers(ppu) ? 3 : 1) : 0);
}
