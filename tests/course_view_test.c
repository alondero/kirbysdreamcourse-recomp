#include "course_view.h"
#include <assert.h>
#include <string.h>

static uint8_t ram[0x20000];
static void word(unsigned address, uint16_t value)
{
    ram[address] = value;
    ram[address + 1] = value >> 8;
}

int main(void)
{
    /* A synthetic irregular row with independently encoded layer entries. */
    word(0x93b0 + 14, 60);
    word(0x9bb0 + 14, 62);
    word(0x8bb0 + 14, 123);
    word(0x13000 + 246, 0x4123);
    word(0x13000 + 250, 0x8456);
    word(0x19800 + 246, 0x2789);
    assert(KdcCourseTile(ram, 0, 60, 7) == 0x4123);
    assert(KdcCourseTile(ram, 0, 62, 7) == 0x8456);
    assert(KdcCourseTile(ram, 1, 60, 7) == 0x2789);
    assert(KdcCourseTile(ram, 0, 59, 7) == 0);
    assert(KdcCourseTile(ram, 0, 63, 7) == 0);
    /* No circular wrapping into an unrelated row or layer. */
    assert(KdcCourseTile(ram, 0, -1, 7) == 0);
    assert(KdcCourseTile(ram, 0, 60, -1) == 0);
    assert(KdcCourseTile(ram, 0, 60, 1024) == 0);
    assert(KdcCourseTile(ram, 2, 60, 7) == 0);
    word(0x8bb0 + 14, 0x33ff);
    word(0x1fffe, 0xabcd);
    assert(KdcCourseTile(ram, 1, 60, 7) == 0xabcd);
    assert(KdcCourseTile(ram, 1, 61, 7) == 0);
    word(0x93b0 + 14, 0xffff);
    word(0x9bb0 + 14, 0xffff);
    assert(KdcCourseTile(ram, 0, 0xffff, 7) == 0);
    memset(ram, 0, sizeof(ram));
    assert(KdcCourseTile(ram, 0, 0, 0) == 0);
    return 0;
}
