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
static uint64_t pack_revision;
static uint32_t pack_ink;
extern bool KdcHdSmoothTile(const PpuHdTileQuery *, uint8_t *, void *);
bool CountedArt(const PpuHdTileQuery *q, uint8_t *pixels, void *context)
{
    ++art_fills;
    return KdcHdSmoothTile(q, pixels, context);
}

/* Unused host-facing paths still need symbols on Windows linkers. */
bool KdcIsCourse(const Ppu *ppu) { (void)ppu; return true; }
bool KdcHdEnabled(void) { return true; }
bool KdcHdPackAvailable(void) { return pack_revision != 0; }
uint64_t KdcHdPackRevision(void) { return pack_revision; }
bool KdcHdPackTile(const PpuHdTileQuery *q, uint32_t *pixels)
{
    if (q->address != 16) return false;
    for (unsigned i = 0; i < 64 * q->scale * q->scale; ++i)
        pixels[i] = pack_ink;
    return true;
}
bool KdcHudActive(const Ppu *ppu) { (void)ppu; return false; }
bool KdcHudOverlayPixel(const Ppu *ppu, int x, int y)
{ (void)ppu; (void)x; (void)y; return false; }
bool KdcHudSample(const Ppu *ppu, unsigned layer, int x, int y,
                  uint16_t *tile, int *px, int *py, void *context)
{ (void)ppu; (void)layer; (void)x; (void)y; (void)tile; (void)px; (void)py; (void)context; return false; }
int KdcHudPortraitShift(const Ppu *ppu, int x, int y, uint16_t attributes)
{ (void)ppu; (void)x; (void)y; (void)attributes; return 0; }
void KdcVisitOrbitSprites(const Ppu *ppu, const uint8_t *ram,
                         KdcOrbitSpriteVisitor visitor, void *context)
{ (void)ppu; (void)ram; (void)visitor; (void)context; }

/* The index at the centre of an enlarged 8x8 character. */
static unsigned Centre(const Render *render, unsigned address)
{
    const uint32_t *pixels = HdArtTileLookup(render, address, false, 0, 4);
    assert(pixels);
    return pixels[(4 * kKdcHdScale) * (8 * kKdcHdScale) + 4 * kKdcHdScale];
}

int main(void)
{
    static Ppu ppu;
    static Snes snes;
    snes.ppu = &ppu;
    g_snes = &snes;
    ppu.cgram[1] = 1; ppu.cgram[2] = 2; ppu.cgram[3] = 3;
    Render render = {.ppu = &ppu, .hd = true, .scale = 4};
    for (unsigned y = 0; y < 8; ++y) {
        ppu.vram[16 + y] = 0x00ff;   /* plane 0: index 1 */
        ppu.vram[0x1010 + y] = 0xff00; /* plane 1: index 2 */
    }
    /* One enlargement per character however often a draw samples it. */
    ++art_generation;
    for (unsigned i = 0; i < 5000; ++i) {
        assert(Centre(&render, 16) == Color(1));
        assert(Centre(&render, 0x1010) == Color(2));
    }
    assert(art_fills == 2);
    /* A later draw with the same words and colours reuses the artwork. */
    ++art_generation;
    assert(Centre(&render, 16) == Color(1));
    assert(art_fills == 2);
    /* Uploads and palette changes are observed on the next draw. */
    for (unsigned y = 0; y < 8; ++y) ppu.vram[16 + y] = 0xffff;
    ++art_generation;
    assert(Centre(&render, 16) == Color(3));
    assert(art_fills == 3);
    ppu.cgram[3] = 0x7fff;
    ++art_generation;
    assert(Centre(&render, 16) == Color(0x7fff));
    assert(art_fills == 4);
    /* Even cache pressure cannot regenerate artwork admitted this draw, nor
     * retry a source that found no room with another enlargement. */
    for (unsigned i = 0; i < 4096; ++i)
        (void)HdArtTileLookup(&render, (i * 16) & 0x7fff, false, (i / 2048) * 16, 4);
    const unsigned warmed = art_fills;
    for (unsigned i = 0; i < 4096; ++i)
        (void)HdArtTileLookup(&render, (i * 16) & 0x7fff, false, (i / 2048) * 16, 4);
    assert(art_fills == warmed);
    pack_revision = 1;
    pack_ink = 0xff123456;
    ++art_generation;
    assert(Centre(&render, 16) == pack_ink);
    ++pack_revision;
    pack_ink = 0xffabcdef;
    ++art_generation;
    assert(Centre(&render, 16) == pack_ink);
    pack_revision = 0;
    ++art_generation;
    assert(Centre(&render, 16) == Color(0x7fff));
    puts("orbit art cache: per-draw reuse, streamed updates and pressure passed");
    return 0;
}
