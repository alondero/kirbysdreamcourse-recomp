/* ROM-free checks for the whole-image enlargement (hd_image.c): isometric
 * checker squares become exact rhombi, a line painted across a square does
 * not round the square's corners, and updating part of the image gives the
 * same texels as enlarging all of it. */
#include "hd_image.h"
#include "hd_scaler.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    kWidth = 96, kHeight = 64, kScale = kKdcHdImageScale,
    kLight = 2, kDark = 1, kLine = 3, kShadow = 4,
};

static float luma[256];
static int failures;

#define CHECK(condition, ...) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); ++failures; } } while (0)

/* An isometric checker square as the game draws it: two rows per step,
 * two pixels wider per side each row, with a four-pixel tip. */
static void Rhombus(uint8_t *image, int cx, int cy, uint8_t color)
{
    for (int y = cy - 8; y < cy + 8; ++y) {
        const int dy = y < cy ? cy - 1 - y : y - cy;
        const int half = 16 - 2 * dy;
        for (int x = cx - half; x < cx + half; ++x)
            if (x >= 0 && y >= 0 && x < kWidth && y < kHeight) image[y * kWidth + x] = color;
    }
}

/* A course boundary as the game draws it: a 1:2 staircase, four pixels of
 * line and two of shadow on every row. */
static void Line(uint8_t *image, int x_at_top)
{
    for (int y = 0; y < kHeight; ++y) {
        const int x0 = x_at_top - 2 * y;
        for (int i = 0; i < 6; ++i) {
            const int x = x0 + i;
            if (x >= 0 && x < kWidth) image[y * kWidth + x] = i < 4 ? kLine : kShadow;
        }
    }
}

static void Enlarge(const uint8_t *image, uint8_t *out)
{
    const KdcHdImage hd = {.src = image, .width = kWidth, .height = kHeight, .src_stride = kWidth,
                           .dst = out, .dst_stride = kWidth * kScale, .luma = luma};
    CHECK(KdcHdImageUpdate(&hd, 0, 0, kWidth, kHeight), "enlargement failed");
}

static uint8_t Texel(const uint8_t *out, int x, int y)
{
    return out[(size_t)y * kWidth * kScale + x];
}

static void TestRhombusIsExact(void)
{
    static uint8_t image[kWidth * kHeight], out[kWidth * kHeight * kScale * kScale];
    memset(image, kLight, sizeof(image));
    Rhombus(image, 48, 32, kDark);
    Enlarge(image, out);
    /* Every texel row crosses one run of the square whose ends move by
     * exactly the 1:2 slope: two texels per row, never a 4x4 staircase or a
     * rounded end. The middle two rows are the left and right corners. */
    int previous_left = -1, previous_right = -1, rows = 0;
    for (int y = 0; y < kHeight * kScale; ++y) {
        int left = -1, right = -1, runs = 0;
        for (int x = 0; x < kWidth * kScale; ++x) {
            const bool dark = Texel(out, x, y) == kDark;
            if (dark && (x == 0 || Texel(out, x - 1, y) != kDark)) ++runs;
            if (dark) { if (left < 0) left = x; right = x; }
        }
        if (left < 0) { previous_left = -1; continue; }
        ++rows;
        CHECK(runs == 1, "texel row %d crosses the square %d times", y, runs);
        /* The rows either side of the corners' vertex are equally wide. */
        const int expected = y == 32 * kScale ? 0 : 2;
        if (previous_left >= 0) {
            CHECK(abs(left - previous_left) == expected, "left edge moves %d at texel row %d",
                  left - previous_left, y);
            CHECK(abs(right - previous_right) == expected, "right edge moves %d at texel row %d",
                  right - previous_right, y);
        }
        previous_left = left;
        previous_right = right;
    }
    CHECK(rows > 0, "the square is missing");
}

/* Pixels at least two pixels from any line or shadow pixel. */
static bool AwayFromLine(const uint8_t *image, int x, int y)
{
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            const int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= kWidth || ny >= kHeight) continue;
            const uint8_t value = image[ny * kWidth + nx];
            if (value == kLine || value == kShadow) return false;
        }
    return true;
}

static void TestLineDoesNotRoundSquare(void)
{
    static uint8_t plain[kWidth * kHeight], crossed[kWidth * kHeight];
    static uint8_t expected[kWidth * kHeight * kScale * kScale];
    static uint8_t actual[kWidth * kHeight * kScale * kScale];
    for (int offset = 40; offset <= 100; offset += 6) {
        memset(plain, kLight, sizeof(plain));
        Rhombus(plain, 48, 32, kDark);
        memcpy(crossed, plain, sizeof(crossed));
        Line(crossed, offset + 64);
        Enlarge(plain, expected);
        Enlarge(crossed, actual);
        int differences = 0, first_x = 0, first_y = 0;
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x) {
                if (!AwayFromLine(crossed, x, y)) continue;
                for (int sy = 0; sy < kScale; ++sy)
                    for (int sx = 0; sx < kScale; ++sx) {
                        const int tx = x * kScale + sx, ty = y * kScale + sy;
                        if (Texel(actual, tx, ty) == Texel(expected, tx, ty)) continue;
                        if (!differences++) { first_x = x; first_y = y; }
                    }
            }
        CHECK(!differences, "line at %d changes %d texels away from it, first in pixel (%d,%d)",
              offset, differences, first_x, first_y);
    }
}

/* Pieces a line cuts off a square are redrawn without painting over the
 * smoothing the general scaler gave the line along it. (Where the line meets
 * the square's outline, an exact corner may cut into that smoothing, so
 * pixels beside the background are not checked.) */
static void TestLineKeepsItsSmoothing(void)
{
    static uint8_t image[kWidth * kHeight];
    static uint8_t smooth[kWidth * kHeight * kScale * kScale];
    static uint8_t actual[kWidth * kHeight * kScale * kScale];
    for (int offset = 40; offset <= 100; offset += 6) {
        memset(image, kLight, sizeof(image));
        Rhombus(image, 48, 32, kDark);
        Line(image, offset + 64);
        for (int y = 1; y < kHeight; y += 2)
            for (int x = 0; x < kWidth; ++x)
                if (image[y * kWidth + x] == kShadow) image[y * kWidth + x] = kLine;
        KdcXbrScaleRegion(image, kWidth, kHeight, kWidth, 0, 0, kWidth, kHeight, luma, kScale,
                          smooth, kWidth * kScale);
        Enlarge(image, actual);
        int lost = 0, first_x = 0, first_y = 0;
        for (size_t i = 0; i < sizeof(smooth); ++i) {
            const int x = (int)(i % (kWidth * kScale)) / kScale, y = (int)(i / (kWidth * kScale)) / kScale;
            if (image[y * kWidth + x] != kDark || (smooth[i] != kLine && smooth[i] != kShadow) ||
                actual[i] == smooth[i]) continue;
            bool beside_background = false;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    beside_background |= image[(y + dy) * kWidth + x + dx] == kLight;
            if (beside_background) continue;
            if (!lost++) { first_x = x; first_y = y; }
        }
        CHECK(!lost, "line at %d loses %d smoothed texels in the square, first in (%d,%d)",
              offset, lost, first_x, first_y);
    }
}

/* A checker: dark rhombi on a light network joined at the rhombi's tips. */
static void Checker(uint8_t *image)
{
    memset(image, kLight, (size_t)kWidth * kHeight);
    for (int cy = 0; cy < kHeight + 16; cy += 16)
        for (int cx = (cy / 16) % 2 ? 16 : 0; cx < kWidth + 32; cx += 32) Rhombus(image, cx, cy, kDark);
}

static void TestPartialUpdateMatchesWhole(void)
{
    static uint8_t image[kWidth * kHeight];
    static uint8_t whole[kWidth * kHeight * kScale * kScale];
    static uint8_t parts[kWidth * kHeight * kScale * kScale];
    Checker(image);
    Line(image, 150);
    Enlarge(image, whole);
    memset(parts, 0xee, sizeof(parts));
    const KdcHdImage hd = {.src = image, .width = kWidth, .height = kHeight, .src_stride = kWidth,
                           .dst = parts, .dst_stride = kWidth * kScale, .luma = luma};
    for (int y = 0; y < kHeight; y += 8)
        for (int x = 0; x < kWidth; x += 8) CHECK(KdcHdImageUpdate(&hd, x, y, x + 8, y + 8), "update failed");
    CHECK(!memcmp(whole, parts, sizeof(whole)), "8x8 updates differ from one whole update");
}

/* A relaxed fit looks up to kThin + 1 pixels beyond the pieces of a shape
 * for more of it. Here a band cuts a wide square, and on one row a second
 * band leads to a lone speck of the square's colour. Whether that row's band
 * reaches the speck decides whether the pieces span more than the extent, so
 * one pixel of it changes the fit at the square's far tip, 44 pixels away. An
 * update of everything within kKdcHdImageReach must still match a rebuild. */
static void TestChangeStaysWithinReach(void)
{
    static uint8_t image[kWidth * kHeight], changed[kWidth * kHeight];
    static uint8_t before[kWidth * kHeight * kScale * kScale];
    static uint8_t whole[kWidth * kHeight * kScale * kScale];
    static uint8_t parts[kWidth * kHeight * kScale * kScale];
    enum { kCx = 40, kCy = 32, kHalfRows = 10, kRow = 30 };
    for (int gap = 1; gap <= 6; ++gap) {
        memset(image, kLight, sizeof(image));
        int right = 0;
        for (int y = kCy - kHalfRows; y < kCy + kHalfRows; ++y) {
            const int dy = y < kCy ? kCy - 1 - y : y - kCy;
            const int half = 2 * kHalfRows - 2 * dy;
            for (int x = kCx - half; x < kCx + half; ++x) image[y * kWidth + x] = kDark;
            if (y == kRow) right = kCx + half - 1;
        }
        for (int y = 0; y < kHeight; ++y)
            for (int x = kCx - 16; x < kCx - 11; ++x) image[y * kWidth + x] = kLine;
        for (int i = 1; i <= gap; ++i) image[kRow * kWidth + right + i] = kLine;
        image[kRow * kWidth + right + gap + 1] = kDark;
        Enlarge(image, before);
        for (int i = 1; i <= gap; ++i) {
            const int px = right + i;
            memcpy(changed, image, sizeof(changed));
            changed[kRow * kWidth + px] = kLight;
            Enlarge(changed, whole);
            memcpy(parts, before, sizeof(parts));
            const KdcHdImage hd = {.src = changed, .width = kWidth, .height = kHeight,
                                   .src_stride = kWidth, .dst = parts,
                                   .dst_stride = kWidth * kScale, .luma = luma};
            CHECK(KdcHdImageUpdate(&hd, px - kKdcHdImageReach, kRow - kKdcHdImageReach,
                                   px + kKdcHdImageReach + 1, kRow + kKdcHdImageReach + 1),
                  "update failed");
            CHECK(!memcmp(whole, parts, sizeof(whole)),
                  "changing pixel (%d,%d) alters texels beyond kKdcHdImageReach (%d)",
                  px, kRow, (int)kKdcHdImageReach);
        }
    }
}

int main(void)
{
    luma[0] = -1;
    luma[kDark] = 0.25f;
    luma[kLight] = 0.85f;
    luma[kLine] = 0.55f;
    luma[kShadow] = 0.15f;
    TestRhombusIsExact();
    TestLineDoesNotRoundSquare();
    TestLineKeepsItsSmoothing();
    TestPartialUpdateMatchesWhole();
    TestChangeStaysWithinReach();
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
