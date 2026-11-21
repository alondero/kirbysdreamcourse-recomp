#pragma once

#include <stdbool.h>
#include <stdint.h>

struct PpuHdTileQuery;
/* Fill a 32x32 unflipped tile with straight-alpha 0xAARRGGBB samples.
 * False selects the regular tile source. */
bool KdcHdUiRgba(const struct PpuHdTileQuery *query, uint32_t *pixels);
/* Host-composed practice labels and opaque surface composition. */
void KdcHdUiGlyph(unsigned char ch, uint32_t color, uint32_t *pixels);
uint32_t KdcHdUiBlend(uint32_t background, uint32_t ink);
