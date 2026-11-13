#ifndef KDC_COURSE_VIEW_H
#define KDC_COURSE_VIEW_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

struct Ppu;
/* Live terrain, excluding scorecards that reuse the course layout. */
bool KdcIsCourse(const struct Ppu *ppu);
void KdcPrepareCourseView(struct Ppu *ppu, const uint8_t *ram, int extra);
/* Course select: continue the scrolling sky into the side margins of the
 * course-label scanlines. rom is the verified 1 MiB USA image (NULL
 * disables). Call after KdcPrepareCourseView and any other per-line hook
 * owner (the edge HUD), which it temporarily replaces on course select. */
void KdcPrepareSelectSky(struct Ppu *ppu, const uint8_t *rom, size_t rom_size);

static inline uint16_t KdcReadWord(const uint8_t *ram, unsigned address)
{
    return (uint16_t)(ram[address] | (unsigned)ram[address + 1] << 8);
}

/* PPU scroll retains ten bits. WRAM selects the nearest world period, but
 * may stage the next frame: preserve all ten live raster bits. */
static inline int KdcCourseScroll(uint16_t raster, uint16_t camera)
{
    int origin = (int16_t)camera;
    return origin + (int)(((unsigned)raster - (unsigned)origin + 512) & 1023) - 512;
}

static inline int KdcTileCoordinate(int pixel)
{
    return pixel >= 0 ? pixel / 8 : -((-pixel + 7) / 8);
}

/* Wide course scenery (docs/widescreen.md): reflect outside whole
 * native-edge tiles, keeping the period independent of window width.
 * Inside the native field this is the ordinary scrolled column. The caller
 * XORs the returned flip with the source tile's flags. */
static inline unsigned KdcReflectedColumn(int screen_x, uint16_t scroll_x, bool *flip)
{
    int fine = scroll_x & 7;
    int columns = fine ? 33 : 32;
    int phase = KdcTileCoordinate(screen_x + fine) % (2 * columns);
    if (phase < 0) phase += 2 * columns;
    *flip = phase >= columns;
    return (unsigned)((scroll_x >> 3) + (*flip ? 2 * columns - 1 - phase : phase));
}

/* Course select's complete island/cloud planes: 3x3 SNES tilemap pages
 * (32x32 tiles per page), with separate $5000-byte source allocations. */
static inline uint16_t KdcCourseSelectTile(const uint8_t *ram, unsigned layer,
                                          int column, int row)
{
    if (layer > 1 || column < 0 || column >= 96 || row < 0 || row >= 96)
        return 0;
    unsigned page = (unsigned)(row / 32) * 3 + (unsigned)(column / 32);
    unsigned index = page * 1024 + (unsigned)(row & 31) * 32 + (unsigned)(column & 31);
    return KdcReadWord(ram, 0x10000 + layer * 0x5000 + 2 * index);
}

/* The game composites its HUD panel into the course BG3 map ($3C00) over
 * the clean scenery map at WRAM $DADC. For BG3 map pixel row v, report the
 * screen columns [left, right] spanned by changed entries, including
 * transparent interiors; no fixed screen rectangle is assumed. */
static inline bool KdcHudSpan(const uint16_t *bg3_map, const uint8_t *ram,
                              unsigned v, uint16_t hscroll, int *left, int *right)
{
    int first = 256, last = -1;
    for (int x = 0; x < 256; ++x) {
        unsigned u = (x + hscroll) & 255;
        unsigned index = ((v & 255) / 8) * 32 + u / 8;
        if (bg3_map[index] == KdcReadWord(ram, 0xdadc + 2 * index)) continue;
        if (x < first) first = x;
        last = x;
    }
    *left = first;
    *right = last;
    return first <= last;
}

/* USA ROM $80:BC3E and $80:BE17: inclusive row bounds, packed word
 * offsets, and two parallel tile layers. See docs/widescreen.md. */
static inline uint16_t KdcCourseTile(const uint8_t *ram, unsigned layer,
                                     int column, int row)
{
    if (layer > 1 || row < 0 || row >= 1024 || column < 0) return 0;
    unsigned left = KdcReadWord(ram, 0x93b0 + 2 * row);
    unsigned right = KdcReadWord(ram, 0x9bb0 + 2 * row);
    if (left >= right || (unsigned)column < left || (unsigned)column > right)
        return 0;
    unsigned index = KdcReadWord(ram, 0x8bb0 + 2 * row) + column - left;
    if (index >= 0x3400) return 0;
    return KdcReadWord(ram, 0x13000 + layer * 0x6800 + 2 * index);
}

#endif
