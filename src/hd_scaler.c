/*
 * Palette-locked xBR scaling for indexed images.
 *
 * Ported from Hyllian's xBR-lv2-noblend shader:
 *
 *   Copyright (C) 2011-2016 Hyllian - sergiogdb@gmail.com
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit
 *   persons to whom the Software is furnished to do so, subject to the
 *   following conditions:
 *
 *   The above copyright notice and this permission notice shall be included
 *   in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 *   NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 *   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 *   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 *   USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *   Incorporates some of the ideas from SABR shader. Thanks to Joshua Street.
 *
 * The shader's CORNER_A rule and default parameters are used. Its four vector
 * lanes are the four corners of the source pixel, unrolled here. Colours are
 * palette indices; comparisons use each index's luma, as the shader does.
 */
#include "hd_scaler.h"

#include <math.h>
#include <stdbool.h>

enum { kLv2Coefficient = 2 };

float KdcXbrLuma555(uint16_t color)
{
    float r = (float)(color & 31) / 31.0f;
    float g = (float)(color >> 5 & 31) / 31.0f;
    float b = (float)(color >> 10 & 31) / 31.0f;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

static int Clamp(int value, int limit)
{
    return value < 0 ? 0 : value >= limit ? limit - 1 : value;
}

void KdcXbrScale(const uint8_t *src, int width, int height,
                 const float luma[256], unsigned scale, uint8_t *dst)
{
    KdcXbrScaleRegion(src, width, height, width, 0, 0, width, height, luma, scale,
                      dst, width * (int)scale);
}

void KdcXbrScaleRegion(const uint8_t *src, int width, int height, int stride,
                       int x0, int y0, int x1, int y1, const float luma[256],
                       unsigned scale, uint8_t *dst, int dst_stride)
{
    if (!src || !dst || !luma || width < 1 || height < 1 || scale < 1 || scale > 8)
        return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width) x1 = width;
    if (y1 > height) y1 = height;
    const int out_width = dst_stride;
    /* Lane-invariant line coefficients from the shader. */
    static const float Ao[4] = {1.0f, -1.0f, -1.0f, 1.0f};
    static const float Bo[4] = {1.0f, 1.0f, -1.0f, -1.0f};
    static const float Co[4] = {1.5f, 0.5f, -0.5f, 0.5f};
    static const float Ax[4] = {1.0f, -1.0f, -1.0f, 1.0f};
    static const float Bx[4] = {0.5f, 2.0f, -0.5f, -2.0f};
    static const float Cx[4] = {1.0f, 1.0f, -0.5f, 0.0f};
    static const float Ay[4] = {1.0f, -1.0f, -1.0f, 1.0f};
    static const float By[4] = {2.0f, 0.5f, -2.0f, -0.5f};
    static const float Cy[4] = {2.0f, 0.0f, -1.0f, 0.5f};

    /* Precomputed scale==4 subpixel masks: bit (sy * 4 + sx) set iff
     * A[k] * fpy + B[k] * fpx > C[k] for fpx=(sx+0.5)/4, fpy=(sy+0.5)/4. */
    static const uint16_t kFxMask[4]  = {0x8000u, 0x0008u, 0x0001u, 0x1000u};
    static const uint16_t kFxLMask[4] = {0xe800u, 0x088cu, 0x0017u, 0x3110u};
    static const uint16_t kFxUMask[4] = {0xc880u, 0x008eu, 0x0113u, 0x7100u};

    for (int y = y0; y < y1; ++y) {
        const uint8_t *row_m2 = src + (size_t)Clamp(y - 2, height) * stride;
        const uint8_t *row_m1 = src + (size_t)Clamp(y - 1, height) * stride;
        const uint8_t *row_0  = src + (size_t)y * stride;
        const uint8_t *row_p1 = src + (size_t)Clamp(y + 1, height) * stride;
        const uint8_t *row_p2 = src + (size_t)Clamp(y + 2, height) * stride;
        for (int x = x0; x < x1; ++x) {
            const int xm1 = x > 0 ? x - 1 : 0;
            const int xp1 = x + 1 < width ? x + 1 : width - 1;
            const uint8_t B = row_m1[x], D = row_0[xm1], E = row_0[x], F = row_0[xp1], H = row_p1[x];
            /* Every rule requires the centre to differ from two adjacent edge
             * neighbours; most pixels of flat artwork differ from none. */
            const float e = luma[E];
            if (luma[B] == e && luma[D] == e && luma[F] == e && luma[H] == e) {
                uint8_t *out = dst + (size_t)(y * (int)scale) * out_width + x * (int)scale;
                for (unsigned sy = 0; sy < scale; ++sy, out += out_width)
                    for (unsigned sx = 0; sx < scale; ++sx) out[sx] = E;
                continue;
            }
            const int xm2 = x >= 2 ? x - 2 : 0;
            const int xp2 = x + 2 < width ? x + 2 : width - 1;
            const uint8_t A1 = row_m2[xm1], B1 = row_m2[x], C1 = row_m2[xp1];
            const uint8_t A = row_m1[xm1], C = row_m1[xp1];
            const uint8_t G = row_p1[xm1], I = row_p1[xp1];
            const uint8_t G5 = row_p2[xm1], H5 = row_p2[x], I5 = row_p2[xp1];
            const uint8_t A0 = row_m1[xm2], D0 = row_0[xm2], G0 = row_p1[xm2];
            const uint8_t C4 = row_m1[xp2], F4 = row_0[xp2], I4 = row_p1[xp2];
            /* Shader lanes: b = (B, D, H, F), c = (C, A, G, I) and their
             * rotations d, f, g, h, i; i4, i5, h5 and f4 likewise. */
            const uint8_t lb[4] = {B, D, H, F}, lc[4] = {C, A, G, I};
            const uint8_t ld[4] = {D, H, F, B}, lf[4] = {F, B, D, H};
            const uint8_t lg[4] = {G, I, C, A}, lh[4] = {H, F, B, D};
            const uint8_t li[4] = {I, C, A, G};
            const uint8_t li4[4] = {I4, C1, A0, G5}, li5[4] = {I5, C4, A1, G0};
            const uint8_t lh5[4] = {H5, F4, B1, D0}, lf4[4] = {F4, B1, D0, H5};
            int edr[4], edr_l[4], edr_u[4], px[4];
            int any_edr = 0;
            for (int k = 0; k < 4; ++k) {
                const float b = luma[lb[k]], c = luma[lc[k]], d = luma[ld[k]];
                const float f = luma[lf[k]], g = luma[lg[k]], h = luma[lh[k]];
                const float i = luma[li[k]], i4 = luma[li4[k]], i5 = luma[li5[k]];
                const float h5 = luma[lh5[k]], f4 = luma[lf4[k]];
                /* CORNER_A */
                const int irlv1 = e != f && e != h;
                if (!irlv1) { edr[k] = edr_l[k] = edr_u[k] = 0; px[k] = 0; continue; }
                const float wd1 = fabsf(e - c) + fabsf(e - g) + fabsf(i - h5) +
                                  fabsf(i - f4) + 4.0f * fabsf(h - f);
                const float wd2 = fabsf(h - d) + fabsf(h - i5) + fabsf(f - i4) +
                                  fabsf(f - b) + 4.0f * fabsf(e - i);
                edr[k] = wd2 >= wd1 + 0.1f;
                if (!edr[k]) { edr_l[k] = edr_u[k] = 0; px[k] = 0; continue; }
                any_edr = 1;
                const int irlv2l = e != g && d != g;
                const int irlv2u = e != c && b != c;
                edr_l[k] = fabsf(h - c) >= kLv2Coefficient * fabsf(f - g) && irlv2l;
                edr_u[k] = fabsf(f - g) >= kLv2Coefficient * fabsf(h - c) && irlv2u;
                px[k] = fabsf(e - f) <= fabsf(e - h);
            }
            uint8_t *out = dst + (size_t)(y * (int)scale) * out_width + x * (int)scale;
            if (!any_edr) {
                for (unsigned sy = 0; sy < scale; ++sy, out += out_width)
                    for (unsigned sx = 0; sx < scale; ++sx) out[sx] = E;
                continue;
            }
            if (scale == 4) {
                uint16_t m[4];
                for (int k = 0; k < 4; ++k)
                    m[k] = edr[k] ? (uint16_t)(kFxMask[k] | (edr_l[k] ? kFxLMask[k] : 0u) |
                                               (edr_u[k] ? kFxUMask[k] : 0u)) : 0u;
                uint16_t any_bits = (uint16_t)(m[0] | m[1] | m[2] | m[3]);
                for (unsigned sy = 0; sy < 4; ++sy) {
                    uint8_t *row = out + (size_t)sy * out_width;
                    row[0] = row[1] = row[2] = row[3] = E;
                }
                const uint8_t p0 = px[0] ? F : H, p1 = px[1] ? B : F;
                const uint8_t p2 = px[2] ? D : B, p3 = px[3] ? H : D;
                while (any_bits) {
                    const unsigned bit = (unsigned)__builtin_ctz(any_bits);
                    const uint16_t mask = (uint16_t)(1u << bit);
                    any_bits &= (uint16_t)(any_bits - 1u);
                    const uint8_t res1 = (m[0] & mask) ? p0 :
                                         (m[1] & mask) ? p1 :
                                         (m[2] & mask) ? p2 : E;
                    const uint8_t res2 = (m[3] & mask) ? p3 :
                                         (m[2] & mask) ? p2 :
                                         (m[1] & mask) ? p1 : E;
                    const float d1 = fabsf(luma[res1] - e), d2 = fabsf(luma[res2] - e);
                    out[(bit >> 2) * (size_t)out_width + (bit & 3u)] = d2 >= d1 ? res2 : res1;
                }
                continue;
            }
            for (unsigned sy = 0; sy < scale; ++sy)
                for (unsigned sx = 0; sx < scale; ++sx) {
                    const float fpx = ((float)sx + 0.5f) / (float)scale;
                    const float fpy = ((float)sy + 0.5f) / (float)scale;
                    int nc[4];
                    for (int k = 0; k < 4; ++k) {
                        const int fx = Ao[k] * fpy + Bo[k] * fpx > Co[k];
                        const int fx_l = Ax[k] * fpy + Bx[k] * fpx > Cx[k];
                        const int fx_u = Ay[k] * fpy + By[k] * fpx > Cy[k];
                        nc[k] = (edr[k] && (fx || (edr_l[k] && fx_l))) ||
                                (edr_u[k] && fx_u);
                    }
                    const uint8_t res1 = nc[0] ? (px[0] ? F : H) :
                                         nc[1] ? (px[1] ? B : F) :
                                         nc[2] ? (px[2] ? D : B) : E;
                    const uint8_t res2 = nc[3] ? (px[3] ? H : D) :
                                         nc[2] ? (px[2] ? D : B) :
                                         nc[1] ? (px[1] ? B : F) : E;
                    /* mix(res1, res2, step(df12.x, df12.y)) */
                    const float d1 = fabsf(luma[res1] - e), d2 = fabsf(luma[res2] - e);
                    out[sy * (size_t)out_width + sx] = d2 >= d1 ? res2 : res1;
                }
        }
    }
}
