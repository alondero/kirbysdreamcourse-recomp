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

static int TileCoordinate(int pixel)
{
    return pixel >= 0 ? pixel / 8 : -((-pixel + 7) / 8);
}

static int CourseScroll(uint16_t raster, uint16_t camera)
{
    /* PPU scroll registers retain only ten bits. WRAM selects the world
     * period, but may stage the next frame: keep all ten live raster bits. */
    int origin = (int16_t)camera;
    return origin + (int)(((unsigned)raster - (unsigned)origin + 512) & 1023) - 512;
}

static uint16_t CourseTile(int layer, int screen_x, uint32_t scrolled_y,
                            uint16_t scroll_x, uint16_t native_tile, void *context)
{
    const CourseView *source = context;
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
        int fine = scroll_x & 7;
        int columns = fine ? 33 : 32;
        int phase = TileCoordinate(screen_x + fine) % (2 * columns);
        if (phase < 0) phase += 2 * columns;
        bool reflected = phase >= columns;
        unsigned column = (scroll_x >> 3) +
            (reflected ? 2 * columns - 1 - phase : phase);
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
    int x = CourseScroll(scroll_x, KdcReadWord(source->ram, 0x86)) + screen_x;
    int raster_y = source->ppu->vScroll[layer];
    int y = (int)scrolled_y - raster_y +
            CourseScroll(raster_y, KdcReadWord(source->ram, 0x88));
    return selection ? KdcCourseSelectTile(source->ram, layer, TileCoordinate(x), TileCoordinate(y)) :
                       KdcCourseTile(source->ram, layer, TileCoordinate(x), TileCoordinate(y));
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
    bool scenery = HasCourseLayout(ppu) || IsCourseSelect(ppu);
    PpuSetWidescreenBg3Widen(ppu, extra && scenery ? 1 : 0);
    /* This ROM still emits ordinary signed 9-bit OAM coordinates. Do not
     * reinterpret left-edge sprites as right-margin objects, or include
     * hidden left-side objects in the hardware sprite budget. */
    PpuWsSetOamRightHints(ppu, extra ? no_hints : NULL);
    PpuWsSetOamLeftHints(ppu, extra ? no_hints : NULL);
    ppu->wsOamMotionGraceOn = extra ? 0 : 1;
    /* Bounded foregrounds stay centered; decorative BG2/backdrop can fill
     * the sides. The provider checks the live raster signature itself. */
    PpuSetWidescreenLayerClamp(ppu, extra && !world_scene ? 1 : 0);
}
