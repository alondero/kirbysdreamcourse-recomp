#ifndef KDC_COURSE_HUD_H
#define KDC_COURSE_HUD_H
#include <stdbool.h>
#include <stdint.h>
struct Ppu;

enum {
    kKdcHudMultiTopX0 = 44,
    kKdcHudMultiTopX1 = 212,
    kKdcHudMultiTopY0 = 8,
    kKdcHudMultiTopY1 = 36,
    kKdcHudMultiTopPad = 4,
    kKdcHudCardY0 = 160,
    kKdcHudCardY1 = 216,
};

void KdcPrepareHud(struct Ppu *ppu, const uint8_t *ram, int extra, bool enabled);
bool KdcHudActive(const struct Ppu *ppu);
bool KdcHudTopStripActive(const struct Ppu *ppu);
/* Raster-space sampling shared by the PPU and orbit renderer. */
bool KdcHudSample(const struct Ppu *ppu, unsigned layer, int x, int y,
                  uint16_t *tile, int *px, int *py, void *context);
bool KdcHudOverlayPixel(const struct Ppu *ppu, int x, int y);
int KdcHudPortraitShift(const struct Ppu *ppu, int x, int y, uint16_t attributes);
#endif
