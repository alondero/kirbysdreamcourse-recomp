/* Exercise the orbit view's enlarged sprite artwork cache, counting the
 * expensive enlargements rather than asserting a machine-dependent timing
 * threshold. Terrain artwork is covered by course_terrain_test.c. No game
 * assets required. */
#define KdcHdSmoothTile CountedArt
#include "../src/course_orbit.c"
#undef KdcHdSmoothTile
#include <assert.h>

Snes *g_snes;
uint8_t g_ram[0x20000];

static unsigned art_fills;
extern bool KdcHdSmoothTile(const PpuHdTileQuery *, uint8_t *, void *);
bool CountedArt(const PpuHdTileQuery *q, uint8_t *pixels, void *context)
{
    ++art_fills;
    return KdcHdSmoothTile(q, pixels, context);
}

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

/* The index at the centre of an enlarged 8x8 character. */
static unsigned Centre(const Render *render, unsigned address)
{
    const uint8_t *pixels = HdArtTileLookup(render, address, false, 0, 4);
    assert(pixels);
    return pixels[(4 * kKdcHdScale) * (8 * kKdcHdScale) + 4 * kKdcHdScale];
}

int main(void)
{
    static Ppu ppu;
    static Snes snes;
    snes.ppu = &ppu;
    g_snes = &snes;
    Render render = {.ppu = &ppu, .hd = true, .scale = 4};
    for (unsigned y = 0; y < 8; ++y) {
        ppu.vram[16 + y] = 0x00ff;   /* plane 0: index 1 */
        ppu.vram[0x1010 + y] = 0xff00; /* plane 1: index 2 */
    }
    /* One enlargement per character however often a draw samples it. */
    ++art_generation;
    for (unsigned i = 0; i < 5000; ++i) {
        assert(Centre(&render, 16) == 1);
        assert(Centre(&render, 0x1010) == 2);
    }
    assert(art_fills == 2);
    /* A later draw with the same words and colours reuses the artwork. */
    ++art_generation;
    assert(Centre(&render, 16) == 1);
    assert(art_fills == 2);
    /* Uploads and palette changes are observed on the next draw. */
    for (unsigned y = 0; y < 8; ++y) ppu.vram[16 + y] = 0xffff;
    ++art_generation;
    assert(Centre(&render, 16) == 3);
    assert(art_fills == 3);
    ppu.cgram[3] = 0x7fff;
    ++art_generation;
    assert(Centre(&render, 16) == 3);
    assert(art_fills == 4);
    /* Even cache pressure cannot regenerate artwork admitted this draw, nor
     * retry a source that found no room with another enlargement. */
    for (unsigned i = 0; i < 4096; ++i)
        (void)HdArtTileLookup(&render, (i * 16) & 0x7fff, false, (i / 2048) * 16, 4);
    const unsigned warmed = art_fills;
    for (unsigned i = 0; i < 4096; ++i)
        (void)HdArtTileLookup(&render, (i * 16) & 0x7fff, false, (i / 2048) * 16, 4);
    assert(art_fills == warmed);
    puts("orbit art cache: per-draw reuse, streamed updates and pressure passed");
    return 0;
}
