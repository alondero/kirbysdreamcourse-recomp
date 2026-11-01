#ifndef KDC_COURSE_VIEW_H
#define KDC_COURSE_VIEW_H

#include <stdint.h>

struct Ppu;
void KdcPrepareCourseView(struct Ppu *ppu, const uint8_t *ram, int extra);

static inline uint16_t KdcReadWord(const uint8_t *ram, unsigned address)
{
    return (uint16_t)(ram[address] | (unsigned)ram[address + 1] << 8);
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
