#include "course_view.h"
#include "snes/ppu.h"
#include "snes/ws_shadow.h"
#include <assert.h>

int snes_frame_counter;
static uint8_t ram[0x20000];
/* Policy setter only; tile-provider dispatch uses the patched runtime. */
void PpuSetWidescreenLayerClamp(Ppu *ppu, uint8_t mask) { ppu->wsLayerClamp = mask; }
void PpuWsSetOamRightHints(Ppu *ppu, const uint8_t *hints) { (void)ppu; (void)hints; }
void PpuWsSetOamLeftHints(Ppu *ppu, const uint8_t *hints) { (void)ppu; (void)hints; }
static void word(unsigned a, uint16_t v) { ram[a] = v; ram[a + 1] = v >> 8; }

int main(void)
{
    Ppu *p = calloc(1, sizeof(*p));
    assert(p);
    p->bgmode = 1;
    p->bgXsc[0] = 0x51;
    p->bgXsc[1] = 0x59;
    word(0x93b0 + 48, 1); word(0x9bb0 + 48, 200);
    word(0x13000 + 110, 0x1234); /* row 24, column 56 */
    WsShadowReset();
    KdcPrepareCourseView(p, ram, 272);
    assert(WsShadowHasTileProvider(0));
    /* Live raster scroll 460/196; WRAM's pending camera is deliberately 0. */
    assert(WsShadowResolveTile8x8(0, -8, 196, 460, 0, 0xabcd) == 0x1234);
    /* Preserve whole tiles straddling either original edge without history. */
    assert(WsShadowResolveTile8x8(0, -4, 196, 460, 0, 0xabcd) == 0xabcd);
    assert(WsShadowResolveTile8x8(0, 252, 196, 460, 0, 0x4567) == 0x4567);
    assert(WsShadowResolveTile8x8(0, 0, 196, 460, 0, 0x5678) == 0x5678);
    word(0x13000 + 110, 0x2345);
    assert(WsShadowResolveTile8x8(0, -8, 196, 460, 0, 0) == 0x2345);
    /* Per-line vertical scroll and high rows bypass shadow-store capacity. */
    word(0x93b0 + 2000, 1); word(0x9bb0 + 2000, 200);
    assert(WsShadowResolveTile8x8(0, -8, 8000, 460, 0, 0) == 0x2345);
    assert(WsShadowResolveTile8x8(0, -8, 65528, 460, 0, 0xffff) == 0);
    assert(WsShadowResolveTile8x8(0, -8, 196, 0, 0, 0xffff) == 0);
    assert(WsShadowResolveTile8x8(0, -7, 196, 0, 0, 0xffff) == 0); /* clipped margin tile */
    p->mosaic = 0x11;
    assert(WsShadowResolveTile8x8(0, -8, 196, 460, 0, 0x9876) == 0x9876);
    p->mosaic = 0;
    /* A mid-raster scene signature change cannot read stale course data. */
    p->bgXsc[0] = 0x50;
    assert(WsShadowResolveTile8x8(0, -8, 196, 460, 0, 0x6789) == 0x6789);
    KdcPrepareCourseView(p, ram, 43);
    assert(p->wsLayerClamp == 1);
    KdcPrepareCourseView(p, ram, 0);
    assert(!WsShadowHasTileProvider(0));
    p->bgXsc[0] = 0x51;
    KdcPrepareCourseView(p, ram, 43);
    WsShadowReset();
    assert(!WsShadowHasTileProvider(0));
    free(p);
    return 0;
}
