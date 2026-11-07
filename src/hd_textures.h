#pragma once

#include <stdbool.h>
#include <stdint.h>

/* HD textures: reconstruct regular terrain edges analytically and re-draw
 * other 8x8 tiles at 4x with a palette-locked scaler. The PPU composes each HD subpixel
 * with the original priorities, windows, colour math and palettes; anything it
 * cannot reproduce stays native. Presentation only: guest state is unchanged.
 *
 * Off by default; the launcher Mods page stores it in kdc-hd.cfg. For
 * headless checks, KDC_HD=0/1 overrides it and KDC_HD_SOURCE=nearest selects
 * the exact nearest-neighbour source used as the identity oracle. */
enum { kKdcHdScale = 4 };

/* The launcher setting, or KDC_HD when set. */
bool KdcHdEnabled(void);

/* Desktop host hook: binds or releases the tile source and returns the
 * presentation scale for the next frame. */
unsigned KdcHdScale(void);

struct PpuHdTileQuery;
/* Exposed for tests: the tile sources the game binds. */
bool KdcHdSmoothTile(const struct PpuHdTileQuery *query, uint8_t *indices, void *context);
bool KdcHdNearestTile(const struct PpuHdTileQuery *query, uint8_t *indices, void *context);
