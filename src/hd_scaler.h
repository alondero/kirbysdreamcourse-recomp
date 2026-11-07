#pragma once

#include <stdint.h>

/* Palette-locked edge-directed upscaling of an indexed image.
 *
 * A C port of Hyllian's xBR-lv2-noblend shader (MIT; see
 * THIRD_PARTY_ATTRIBUTION.md). Every output pixel is one of the source
 * pixel's neighbours, so the result only contains indices already present and
 * live palettes keep working. Edges are decided on luma; index 0 is
 * transparent and treated as a distinct, far luma so silhouettes are
 * smoothed too. Pixels outside the image repeat the nearest edge pixel.
 *
 * src: width*height indices, row-major. luma: one value per index (0..255),
 * typically 0..1 for colours and -1 for transparency. dst receives
 * (width*scale) x (height*scale) indices. scale is 1..8. */
void KdcXbrScale(const uint8_t *src, int width, int height,
                 const float luma[256], unsigned scale, uint8_t *dst);

/* Rec. 709 luma of a BGR555 colour, as the shader computes it. */
float KdcXbrLuma555(uint16_t color);
