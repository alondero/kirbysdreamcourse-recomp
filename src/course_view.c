#include "course_view.h"
#include "snes/ppu.h"
#include "snes/ws_shadow.h"

typedef struct CourseView {
    Ppu *ppu;
    const uint8_t *ram;
} CourseView;

static CourseView view;

bool KdcIsCourse(const Ppu *ppu)
{
    /* The preview uses bounded $50/$58 maps, not the streamed course. */
    return PPU_mode(ppu) == 1 && ppu->bgXsc[0] == 0x51 &&
           ppu->bgXsc[1] == 0x59 && (ppu->bgTileAdr & 0xff) == 0 &&
           !PPU_bigTiles(ppu, 0) && !PPU_bigTiles(ppu, 1);
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
    if (!KdcIsCourse(source->ppu)) return native_tile;
    /* Mosaic groups may span several tiles and the native boundary. Keep
     * their hardware source during the effect instead of changing a group. */
    if (PPU_mosaicEnabled(source->ppu, layer) && PPU_mosaicSize(source->ppu) > 1)
        return native_tile;
    /* Keep live raster fine scroll; WRAM only disambiguates its period. */
    int x = CourseScroll(scroll_x, KdcReadWord(source->ram, 0x86)) + screen_x;
    int raster_y = source->ppu->vScroll[layer];
    int y = (int)scrolled_y - raster_y +
            CourseScroll(raster_y, KdcReadWord(source->ram, 0x88));
    return KdcCourseTile(source->ram, layer, TileCoordinate(x), TileCoordinate(y));
}

void KdcPrepareCourseView(Ppu *ppu, const uint8_t *ram, int extra)
{
    static const uint8_t no_hints[16] = {0};
    view = (CourseView){ppu, ram};
    WsShadowSetTileProvider(3, extra ? CourseTile : NULL, &view);
    /* In streamed courses BG3 is scenery; the HUD is made of objects.
     * The runtime otherwise assumes BG3 is a centered status bar. Visible
     * SNES scanlines start at 1; zero disables the widening policy. */
    PpuSetWidescreenBg3Widen(ppu, extra && KdcIsCourse(ppu) ? 1 : 0);
    /* This ROM still emits ordinary signed 9-bit OAM coordinates. Do not
     * reinterpret left-edge sprites as right-margin objects, or include
     * hidden left-side objects in the hardware sprite budget. */
    PpuWsSetOamRightHints(ppu, extra ? no_hints : NULL);
    PpuWsSetOamLeftHints(ppu, extra ? no_hints : NULL);
    ppu->wsOamMotionGraceOn = extra ? 0 : 1;
    /* Bounded foregrounds stay centered; decorative BG2/backdrop can fill
     * the sides. The provider checks the live raster signature itself. */
    if (extra && !KdcIsCourse(ppu)) PpuSetWidescreenLayerClamp(ppu, 1);
}
