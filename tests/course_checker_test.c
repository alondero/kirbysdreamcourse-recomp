#include "course_checker.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void Encode(const uint8_t pixels[64], uint16_t words[16])
{
    memset(words, 0, 32);
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
            for (unsigned bit = 0; bit < 4; ++bit)
                words[y + 8 * (bit / 2)] |=
                    ((pixels[8 * y + x] >> bit) & 1) << (7 - x + 8 * (bit % 2));
}

static int Floor32(int x) { return x >= 0 ? x / 32 : (x - 31) / 32; }

static bool CheckerTile(int ox, int oy, int phase, KdcChecker *checker)
{
    uint8_t pixels[64], border[80];
    uint16_t words[16];
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            int a = Floor32(ox + x + 2 * (oy + y) + phase);
            int b = Floor32(ox + x - 2 * (oy + y) + phase);
            pixels[8 * y + x] = ((a ^ b) & 1) ? 3 : 12;
        }
    Encode(pixels, words);
    for (int k = 0; k < 80; ++k) {
        int x = k < 48 ? k % 12 - 2 : (k - 48) / 8 < 2 ? (k - 48) / 8 - 2 : (k - 48) / 8 + 6;
        int y = k < 48 ? (k / 12 < 2 ? k / 12 - 2 : k / 12 + 6) : (k - 48) % 8;
        int a = Floor32(ox + x + 2 * (oy + y) + phase);
        int b = Floor32(ox + x - 2 * (oy + y) + phase);
        border[k] = ((a ^ b) & 1) ? 3 : 12;
    }
    return KdcCheckerFit(words, border, checker);
}

static void Continuity(void)
{
    KdcChecker a, b;
    /* Neighbours expose corners hidden outside the original pixel centres. */
    assert(CheckerTile(8, 8, 1, &a));
    assert(CheckerTile(16, 8, 1, &b));
    /* A second border row establishes the hidden phase-zero corner too. */
    assert(CheckerTile(8, 0, 0, &a));
    unsigned compared = 0;
    for (int phase = 0; phase < 32; ++phase)
    for (int y = -32; y < 32; y += 8)
        for (int x = -32; x < 32; x += 8) {
            if (!CheckerTile(x, y, phase, &a)) continue;
            for (int vertical = 0; vertical < 2; ++vertical) {
                if (!CheckerTile(x + (vertical ? 0 : 8), y + (vertical ? 8 : 0), phase, &b)) continue;
                for (unsigned k = 0; k < 128; ++k) {
                    double t = (k + 0.5) / 16;
                    uint8_t left = KdcCheckerSample(&a, vertical ? t : 8 - 1e-7,
                                                    vertical ? 8 - 1e-7 : t);
                    uint8_t right = KdcCheckerSample(&b, vertical ? t : 1e-7,
                                                     vertical ? 1e-7 : t);
                    assert(left == right);
                }
                ++compared;
            }
        }
    assert(compared > 0);
}

/* Independent 90-degree cone, rotated around the tile. The offset places
 * one tile corner strictly inside, exposing top-left scan-order dependence. */
static uint8_t Corner(double x, double y, unsigned rotation, bool invert)
{
    for (unsigned i = 0; i < rotation; ++i) {
        double next_x = 7 - y;
        y = x; x = next_x;
    }
    bool inside = x + y >= 5.5 && x - y >= 1.5;
    return inside != invert ? 3 : 12;
}

static void Corners(void)
{
    unsigned origin_inside = 0, origin_outside = 0;
    for (unsigned rotation = 0; rotation < 4; ++rotation)
        for (unsigned invert = 0; invert < 2; ++invert) {
            uint8_t pixels[64], border[kKdcCheckerBorderSize];
            uint16_t words[16];
            KdcChecker checker;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    pixels[y * 8 + x] = Corner(x, y, rotation, invert);
            for (int k = 0; k < kKdcCheckerBorderSize; ++k) {
                int x, y;
                KdcCheckerBorderPosition(k, &x, &y);
                border[k] = Corner(x, y, rotation, invert);
            }
            Encode(pixels, words);
            assert(KdcCheckerFit(words, border, &checker));
            assert(checker.operation == 1);
            origin_inside += Corner(0, 0, rotation, false) == 3;
            origin_outside += Corner(0, 0, rotation, false) == 12;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    assert(KdcCheckerSample(&checker, x + 0.5, y + 0.5) == pixels[y * 8 + x]);
            /* Fractional offsets avoid ties on an edge; compare the whole
             * interior and border, not just the native pixel centres. */
            for (int y = -32; y < 160; ++y)
                for (int x = -32; x < 160; ++x) {
                    double sx = (x + 0.25) / 16, sy = (y + 0.125) / 16;
                    assert(KdcCheckerSample(&checker, sx + 0.5, sy + 0.5) ==
                           Corner(sx, sy, rotation, invert));
                }
        }
    assert(origin_inside && origin_outside);
}

int main(void)
{
    Corners();
    Continuity();
    uint8_t pixels[64], border[80];
    uint16_t words[16];
    KdcChecker checker;
    /* Independent half-plane oracle: all native centres must be preserved,
     * and subpixels lie on a straight line (including beyond 4x zoom). */
    for (int slope = -2; slope <= 2; ++slope) {
        if (!slope) continue;
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x)
                pixels[8 * y + x] = 2 * ((int)x + slope * (int)y) >= 7 * (1 + slope) ? 3 : 12;
        for (int k = 0; k < 80; ++k) {
            int x = k < 48 ? k % 12 - 2 : (k - 48) / 8 < 2 ? (k - 48) / 8 - 2 : (k - 48) / 8 + 6;
            int y = k < 48 ? (k / 12 < 2 ? k / 12 - 2 : k / 12 + 6) : (k - 48) % 8;
            border[k] = 2 * (x + slope * y) >= 7 * (1 + slope) ? 3 : 12;
        }
        Encode(pixels, words);
        assert(KdcCheckerFit(words, border, &checker));
        for (unsigned y = 0; y < 8; ++y)
            for (unsigned x = 0; x < 8; ++x)
                assert(KdcCheckerSample(&checker, x + 0.5, y + 0.5) == pixels[8 * y + x]);
        double boundary = (checker.edge[0].threshold * 0.5) + 0.5;
        for (int i = 0; i < 128; ++i) {
            double y = (i + 0.5) / 16;
            double x = boundary - slope * (y - 0.5);
            if (x <= 0 || x >= 8) continue;
            assert(KdcCheckerSample(&checker, x - 0.00001, y) !=
                   KdcCheckerSample(&checker, x + 0.00001, y));
        }
    }
    /* Two crossing checker edges; test every centre and palette identity. */
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
            pixels[8 * y + x] = (((int)x + 2 * (int)y >= 11) !=
                                 ((int)x - 2 * (int)y >= -3)) ? 5 : 9;
    for (int k = 0; k < 80; ++k) {
        int x = k < 48 ? k % 12 - 2 : (k - 48) / 8 < 2 ? (k - 48) / 8 - 2 : (k - 48) / 8 + 6;
        int y = k < 48 ? (k / 12 < 2 ? k / 12 - 2 : k / 12 + 6) : (k - 48) % 8;
        border[k] = ((x + 2 * y >= 11) != (x - 2 * y >= -3)) ? 5 : 9;
    }
    Encode(pixels, words);
    assert(KdcCheckerFit(words, border, &checker));
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
            assert(KdcCheckerSample(&checker, x + 0.5, y + 0.5) == pixels[8 * y + x]);
    pixels[0] = 0; Encode(pixels, words);
    assert(!KdcCheckerFit(words, border, &checker));
    pixels[0] = 14; Encode(pixels, words);
    assert(!KdcCheckerFit(words, border, &checker));
    memset(pixels, 5, sizeof(pixels)); Encode(pixels, words);
    assert(!KdcCheckerFit(words, border, &checker));
    for (unsigned i = 0; i < 64; ++i) pixels[i] = (i + i / 8) % 2 ? 5 : 9;
    Encode(pixels, words);
    assert(!KdcCheckerFit(words, border, &checker));
    puts("checker geometry: centres, subpixels, crossings and rejection passed");
}
