/* Exercise the real orbit sampling path, counting expensive fits rather than
 * asserting a machine-dependent timing threshold. No game assets required. */
#define KdcCheckerFit CountedFit
#include "../src/course_orbit.c"
#undef KdcCheckerFit
#include "../src/course_checker.c"
#include <assert.h>

Snes *g_snes;
uint8_t g_ram[0x20000];
static uint16_t vram[0x8000];
static unsigned fits;

/* Unused host-facing paths still need symbols on Windows linkers. */
bool KdcIsCourse(const Ppu *ppu) { (void)ppu; return true; }
bool KdcHdEnabled(void) { return true; }
bool KdcHudActive(const Ppu *ppu) { (void)ppu; return false; }
bool KdcHudOverlayPixel(const Ppu *ppu, int x, int y)
{ (void)ppu; (void)x; (void)y; return false; }
int KdcHudPortraitShift(const Ppu *ppu, int x, int y, uint16_t attributes)
{ (void)ppu; (void)x; (void)y; (void)attributes; return 0; }
void KdcVisitOrbitSprites(const Ppu *ppu, const uint8_t *ram,
                         KdcOrbitSpriteVisitor visitor, void *context)
{ (void)ppu; (void)ram; (void)visitor; (void)context; }

bool CountedFit(const uint16_t words[16], const uint8_t border[kKdcCheckerBorderSize],
                KdcChecker *checker)
{
    ++fits;
    return KdcCheckerFit(words, border, checker);
}

static void Word(unsigned at, unsigned value)
{
    g_ram[at] = (uint8_t)value;
    g_ram[at + 1] = (uint8_t)(value >> 8);
}

static unsigned Sample(int column, int row)
{
    Render render = {.hd = true};
    /* Both chosen locations use character 6 in the periodic synthetic map. */
    return TerrainPixel(&render, vram, 0, 6, column * 8 + 4.25, row * 8 + 4.25);
}

int main(void)
{
    for (unsigned row = 0; row < 64; ++row) {
        Word(0x93b0 + row * 2, 0);
        Word(0x9bb0 + row * 2, 63);
        Word(0x8bb0 + row * 2, row * 64);
        for (unsigned column = 0; column < 64; ++column)
            Word(0x13000 + (row * 64 + column) * 2, 1 + (row % 4) * 4 + column % 4);
    }
    for (int ty = 0; ty < 4; ++ty)
        for (int tx = 0; tx < 4; ++tx)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    int u = tx * 8 + x, v = ty * 8 + y;
                    bool light = (((u + 2 * v + 129) / 32) ^ ((u - 2 * v + 129) / 32)) & 1;
                    unsigned at = (1 + ty * 4 + tx) * 16 + y;
                    vram[at] |= (uint16_t)(1u << (7 - x));
                    if (light) vram[at] |= (uint16_t)(1u << (15 - x));
                }
    checker_generation = 1;
    /* (49,1) and (1,17) collide under the former direct-mapped hash. */
    unsigned a = Sample(49, 1), b = Sample(1, 17);
    assert(a && b && fits == 2);
    for (unsigned i = 0; i < 5000; ++i) {
        assert(Sample(49, 1) == a);
        assert(Sample(1, 17) == b);
    }
    assert(fits == 2);
    /* Pressure beyond a collision bucket's capacity must not evict models
     * already used in this draw, or retry failed admission with a solver. */
    for (int i = 1; i < 100; ++i) Sample(49 + i * 2048, 1);
    unsigned warmed = fits;
    for (int i = 1; i < 100; ++i) Sample(49 + i * 2048, 1);
    assert(Sample(49, 1) == a && Sample(1, 17) == b);
    assert(fits == warmed);
    /* A new draw still observes streamed words and refits each location once. */
    ++checker_generation;
    vram[6 * 16] ^= 0x0100;
    Sample(49, 1); Sample(1, 17);
    assert(fits == warmed + 2);
    Sample(49, 1); Sample(1, 17);
    assert(fits == warmed + 2);
    puts("orbit checker cache: collision pressure and streamed updates passed");
}
