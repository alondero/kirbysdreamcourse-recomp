#include "course_geometry.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static uint8_t ram[0x20000], rom[0x100000];

static void Word(uint8_t *bytes, unsigned address, unsigned value)
{
    bytes[address] = (uint8_t)value;
    bytes[address + 1] = (uint8_t)(value >> 8);
}

static void RomWord(unsigned address, unsigned value)
{
    Word(rom, ((address >> 16) & 0x7f) * 0x8000 + (address & 0x7fff), value);
}

static void Near(double actual, double expected)
{
    assert(fabs(actual - expected) < 0.000001);
}

int main(void)
{
    /* Synthetic two-by-two map, with deliberately different planes and
     * quadrants. These fixtures contain no extracted ROM or terrain data. */
    KdcCourseGeometry view;
    Word(ram, 0x8b92, 2);
    Word(ram, 0x8b94, 2);
    Word(ram, 0x8b9c, (unsigned)(uint16_t)-20);
    Word(ram, 0x8b9e, 200);
    ram[0x6b90] = 1;
    ram[0x7390] = 2;
    RomWord(0x888004, 46340);
    assert(KdcCourseGeometryInit(&view, ram, sizeof(ram), rom, sizeof(rom)));
    double z = -999, sx, sy;
    assert(KdcCourseGeometrySample(&view, 0.3, 0.7, &z));
    Near(z, 3);
    assert(!KdcCourseGeometrySample(&view, 1.3, 0.7, &z)); /* void */
    assert(!KdcCourseGeometrySample(&view, 0, 2, &z));
    assert(!KdcCourseGeometrySample(&view, -0.1, 0, &z));
    assert(!KdcCourseGeometrySample(&view, NAN, 0, &z));
    assert(!KdcCourseCellHeight(&view, 0, 0, INFINITY, 0, &z));
    KdcCourseGeometryProject(&view, 2, 1, 3, &sx, &sy);
    Near(sx, 76);
    Near(sy, 168);

    /* Exercise each triangle's selection with asymmetric test values. */
    ram[0x6b90] = 4;
    for (unsigned q = 0; q < 4; ++q)
        RomWord(0x819d81 + 4 * 8 + q * 2, q + 1);
    assert(KdcCourseCellHeight(&view, 0, 0, 0.3, 0.1, &z)); Near(z, 2.1);
    assert(KdcCourseCellHeight(&view, 0, 0, 0.9, 0.4, &z)); Near(z, 2.1);
    assert(KdcCourseCellHeight(&view, 0, 0, 0.4, 0.9, &z)); Near(z, 2.1);
    assert(KdcCourseCellHeight(&view, 0, 0, 0.1, 0.3, &z)); Near(z, 2.1);

    /* A diagonal ramp and constant triangle meet along a diagonal; a
     * neighboring void must not borrow its height. */
    ram[0x6b90] = 16;
    RomWord(0x819d81 + 16 * 8, 5);
    RomWord(0x819569 + 16 * 8, 0xffff);
    RomWord(0x819569 + 16 * 8 + 6, 23170);
    assert(KdcCourseCellHeight(&view, 0, 0, 0.7, 0.2, &z)); Near(z, 2.5);
    assert(KdcCourseCellHeight(&view, 0, 0, 0.2, 0.7, &z)); Near(z, 3);
    assert(KdcCourseCellHeight(&view, 0, 0, 0.4, 0.4, &z)); Near(z, 3);
    assert(!KdcCourseCellHeight(&view, 1, 0, 0, 0, &z));
    RomWord(0x819d81 + 16 * 8, 99);
    assert(!KdcCourseCellHeight(&view, 0, 0, 0.7, 0.2, &z));

    ram[0x6b90] = 28;
    assert(!KdcCourseGeometryInit(&view, ram, sizeof(ram), rom, sizeof(rom)));
    assert(!view.ram && !view.width);
    ram[0x6b90] = 1;
    ram[0x7390] = 100;
    assert(!KdcCourseGeometryInit(&view, ram, sizeof(ram), rom, sizeof(rom)));
    ram[0x7390] = 0;
    Word(ram, 0x8b92, 256);
    assert(!KdcCourseGeometryInit(&view, ram, sizeof(ram), rom, sizeof(rom)));
    Word(ram, 0x8b92, 64);
    Word(ram, 0x8b94, 64);
    assert(!KdcCourseGeometryInit(&view, ram, sizeof(ram), rom, sizeof(rom)));
    Word(ram, 0x8b92, 2);
    Word(ram, 0x8b94, 2);
    assert(!KdcCourseGeometryInit(&view, ram, 4, rom, sizeof(rom)));
    assert(!KdcCourseGeometryInit(&view, ram, sizeof(ram), rom, 4));
    assert(!KdcCourseGeometryInit(&view, NULL, sizeof(ram), rom, sizeof(rom)));
    return 0;
}
