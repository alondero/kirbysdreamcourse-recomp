#include "course_geometry.h"

#include <math.h>
#include <string.h>

static unsigned Word(const uint8_t *bytes, unsigned address)
{
    return bytes[address] | (unsigned)bytes[address + 1] << 8;
}

static unsigned RomWord(const KdcCourseGeometry *view, unsigned address)
{
    unsigned offset = ((address >> 16) & 0x7f) * 0x8000 + (address & 0x7fff);
    return Word(view->rom, offset);
}

bool KdcCourseGeometryInit(KdcCourseGeometry *view,
                           const uint8_t *ram, size_t ram_size,
                           const uint8_t *rom, size_t rom_size)
{
    if (!view) return false;
    memset(view, 0, sizeof(*view));
    if (!ram || !rom || ram_size < 0x20000 || rom_size != 0x100000)
        return false;
    unsigned width = Word(ram, 0x8b92), height = Word(ram, 0x8b94);
    if (!width || !height || width > 255 || height > 255 || width * height > 0x800)
        return false;
    /* $81:94B3 validates row/column against these dimensions. Each of the
     * four $800-byte planes is byte indexed, not a fixed 32x32 map.
     * $81:9D81 contains 28 shapes, with four slope selectors per shape. */
    for (unsigned i = 0; i < width * height; ++i) {
        if (ram[0x6b90 + i] >= 28 || ram[0x7390 + i] >= 100)
            return false;
    }
    *view = (KdcCourseGeometry){ram, rom, width, height,
        (int16_t)Word(ram, 0x8b9c), (int16_t)Word(ram, 0x8b9e)};
    return true;
}

bool KdcCourseCellHeight(const KdcCourseGeometry *view,
                         unsigned column, unsigned row, double u, double v,
                         double *height)
{
    if (!view || !view->ram || !view->rom || !height ||
        column >= view->width || row >= view->height ||
        !isfinite(u) || !isfinite(v) || u < 0 || u > 1 || v < 0 || v > 1)
        return false;
    unsigned index = row * view->width + column;
    unsigned shape = view->ram[0x6b90 + index];
    unsigned elevation = view->ram[0x7390 + index];
    if (!shape || shape >= 28 || elevation >= 100) return false;

    /* $81:9661 splits the square on both diagonals. The collision routine
     * samples fixed-point ramps through $88:8190. Render their continuous
     * plane here; this deliberately avoids reproducing subpixel table
     * quantization in geometry and never replaces guest collision. */
    unsigned quarter = u + v >= 1 ? (u >= v ? 1 : 2) : (u >= v ? 0 : 3);
    unsigned offset = shape * 8 + quarter * 2;
    unsigned slope = RomWord(view, 0x819d81 + offset);
    double surface;
    if (!(shape & 16)) {
        switch (slope) {
        case 0: surface = 1; break;
        case 1: surface = v; break;
        case 2: surface = 1 - u; break;
        case 3: surface = 1 - v; break;
        case 4: surface = u; break;
        default: return false;
        }
    } else {
        /* $81:9727 first selects a constant or a diagonal ramp. The latter
         * subtracts the per-shape height in $81:9629. */
        int flat = (int16_t)RomWord(view, 0x819569 + offset);
        if (flat >= 0) {
            surface = flat / 23170.0;
        } else {
            switch (slope) {
            case 5: surface = 1 - u + v; break;
            case 6: surface = 2 - u - v; break;
            case 7: surface = 1 + u - v; break;
            case 8: surface = u + v; break;
            default: return false;
            }
            surface -= RomWord(view, 0x819629 + shape * 2) / 23170.0;
        }
    }
    unsigned low = RomWord(view, 0x888000 + elevation * 2);
    unsigned high = RomWord(view, 0x8880c8 + elevation * 2);
    *height = (low + high * 65536.0) / 23170.0 + surface;
    return true;
}

bool KdcCourseGeometrySample(const KdcCourseGeometry *view,
                             double x, double y, double *height)
{
    if (!view || !isfinite(x) || !isfinite(y) ||
        x < 0 || y < 0 || x >= view->width || y >= view->height)
        return false;
    unsigned column = (unsigned)x, row = (unsigned)y;
    return KdcCourseCellHeight(view, column, row, x - column, y - row, height);
}

void KdcCourseGeometryProject(const KdcCourseGeometry *view,
                              double x, double y, double height,
                              double *screen_x, double *screen_y)
{
    /* $81:A4C6 projects physical U/V/Z, and the draw path subtracts its
     * intermediate Y from $8B9A. That origin cancels; $8B9E remains. The
     * 16-pixel elevation unit is the idealized limit of the game's two
     * fixed-point factors ($5A82 and $2D42). */
    *screen_x = view->origin_x + 32 * (x + y);
    *screen_y = view->origin_y + 16 * (x - y - height);
}
