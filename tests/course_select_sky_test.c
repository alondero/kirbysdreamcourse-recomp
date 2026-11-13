#include "course_view.h"
#include "snes/ppu.h"
#include "snes/ws_shadow.h"
#include <assert.h>
#include <stdlib.h>

/* ROM-free fixture for the course-select sky beneath the course label.
 * The table values are synthetic; only the USA layout is reproduced. */
int snes_frame_counter;
static uint8_t ram[0x20000];
static uint8_t rom[0x100000];
void PpuSetWidescreenLayerClamp(Ppu *ppu, uint8_t mask) { ppu->wsLayerClamp = mask; }
void PpuSetWidescreenBg3Widen(Ppu *ppu, uint8_t y) { ppu->wsBg3WidenY = y; }
void PpuWsSetOamRightHints(Ppu *ppu, const uint8_t *hints) { (void)ppu; (void)hints; }
void PpuWsSetOamLeftHints(Ppu *ppu, const uint8_t *hints) { (void)ppu; (void)hints; }
void PpuSetBackgroundSample(Ppu *ppu,
    bool (*sample)(const Ppu *, unsigned, int, int, uint16_t *, int *, int *, void *),
    void *context)
{
    ppu->backgroundSample = sample;
    ppu->backgroundSampleContext = context;
}

static void OtherPrepare(Ppu *ppu, int line, void *context)
{
    (void)ppu; (void)line; (void)context;
}

enum { kRomTable = 0x1a75c, kLiveTable = 0x120f, kPhase = -58, kSplit = 175 };

static int Base(unsigned entry) { return (int)(entry + entry / 3); }

static void Entry(uint8_t *table, unsigned entry, uint8_t lines, int value)
{
    table[3 * entry] = lines;
    table[3 * entry + 1] = (uint8_t)value;
    table[3 * entry + 2] = (uint8_t)((unsigned)value >> 8);
}

/* The game scrolls the first 175 HDMA entries and replaces the rest with
 * one 72-line entry that selects the label page. */
static void LiveTable(int phase)
{
    for (unsigned i = 0; i < kSplit; ++i) Entry(ram + kLiveTable, i, 1, Base(i) + phase);
    Entry(ram + kLiveTable, kSplit, 72, 0);
    ram[kLiveTable + 3 * (kSplit + 1)] = 0;
}

/* The PPU prepares each scanline during sprite evaluation, one line
 * before the backgrounds of the same scanline are drawn. */
static void Prepare(Ppu *p, int line)
{
    p->presentationPrepare(p, line - 1, p->presentationContext);
}

static bool Sample(Ppu *p, unsigned layer, int x, int y, uint16_t *tile, int *px, int *py)
{
    *tile = 0xffff; *px = *py = -1;
    return p->backgroundSample &&
           p->backgroundSample(p, layer, x, y, tile, px, py, p->backgroundSampleContext);
}

/* Expected margin source: the clean sky page, scrolled as the ROM table
 * continues below the split. HDMA entry n is visible on scanline n + 1. */
static void ExpectSky(Ppu *p, int x, int line, int phase)
{
    uint16_t tile; int px, py;
    assert(Sample(p, 2, x, line, &tile, &px, &py));
    unsigned v = (unsigned)(line + ((Base((unsigned)line - 1) + phase) & 1023));
    bool flip;
    unsigned column = KdcReflectedColumn(x, 0, &flip) & 31;
    assert(tile == (p->vram[0x6000 + ((v >> 3) & 31) * 32 + column] ^ (flip ? 0x4000 : 0)));
    assert(px == (x & 7));
    assert(py == (int)(v & 7));
}

int main(void)
{
    Ppu *p = calloc(1, sizeof(*p));
    assert(p);
    for (unsigned i = 0; i < 225; ++i) Entry(rom + kRomTable, i, 1, Base(i));
    rom[kRomTable + 3 * 225] = 0;
    for (unsigned i = 0; i < 1024; ++i) p->vram[0x6000 + i] = (uint16_t)(0x0800 | i);
    LiveTable(kPhase);
    p->bgmode = 9;
    p->bgXsc[0] = 0x51; p->bgXsc[1] = 0x59; p->bgXsc[2] = 0x61;
    p->bgTileAdr = 0x230;
    p->screenEnabled[0] = 0x17;
    KdcPrepareCourseView(p, ram, 43);
    KdcPrepareSelectSky(p, rom, sizeof(rom));
    assert(p->presentationPrepare);

    /* Label scanlines: the native field shows BG3's second page. */
    p->hScroll[2] = 256; p->vScroll[2] = 0;
    for (int line = kSplit + 1; line <= 224; ++line) {
        Prepare(p, line);
        ExpectSky(p, -1, line, kPhase);
        ExpectSky(p, -43, line, kPhase);
        ExpectSky(p, 256, line, kPhase);
        ExpectSky(p, 298, line, kPhase);
    }
    /* The game advances the phase every frame; the margins follow it. */
    LiveTable(kPhase - 1);
    Prepare(p, 200);
    ExpectSky(p, -8, 200, kPhase - 1);

    uint16_t tile; int px, py;
    /* The native panel and the other planes keep their hardware tiles. */
    assert(!Sample(p, 2, 0, 200, &tile, &px, &py));
    assert(!Sample(p, 2, 255, 200, &tile, &px, &py));
    assert(!Sample(p, 0, -8, 200, &tile, &px, &py));
    assert(!Sample(p, 1, 300, 200, &tile, &px, &py));

    /* Sky scanlines already scroll natively: use the ordinary fast path. */
    p->hScroll[2] = 0; p->vScroll[2] = 40;
    Prepare(p, 100);
    assert(!p->backgroundSample);
    p->hScroll[2] = 256; p->vScroll[2] = 0;
    /* Beyond the ROM table, mosaic, or an unrecognised live table. */
    Prepare(p, 225);
    assert(!p->backgroundSample);
    p->mosaic = 0x24;
    Prepare(p, 200);
    assert(!p->backgroundSample);
    p->mosaic = 0;
    ram[kLiveTable + 3] = 2;
    Prepare(p, 200);
    assert(!p->backgroundSample);
    ram[kLiveTable + 3] = 1;
    Entry(ram + kLiveTable, 1, 1, Base(1) + kPhase + 1);
    Prepare(p, 200);
    assert(!p->backgroundSample);
    LiveTable(kPhase);
    rom[kRomTable + 3 * 199] = 2;
    Prepare(p, 200);
    assert(!p->backgroundSample);
    rom[kRomTable + 3 * 199] = 1;
    Prepare(p, 200);
    assert(p->backgroundSample);
    /* A mid-frame scene change revokes the source on the next scanline. */
    p->bgXsc[2] = 0x60;
    Prepare(p, 201);
    assert(!p->backgroundSample);
    p->bgXsc[2] = 0x61;

    /* Frame policy: only widened course select with the verified ROM. */
    KdcPrepareSelectSky(p, NULL, 0);
    assert(!p->presentationPrepare && !p->backgroundSample);
    KdcPrepareSelectSky(p, rom, 0x80000);
    assert(!p->presentationPrepare);
    KdcPrepareCourseView(p, ram, 0);
    KdcPrepareSelectSky(p, rom, sizeof(rom));
    assert(!p->presentationPrepare);
    KdcPrepareCourseView(p, ram, 43);
    p->bgXsc[2] = 0x60;
    KdcPrepareSelectSky(p, rom, sizeof(rom));
    assert(!p->presentationPrepare);
    p->bgXsc[2] = 0x61;
    p->bgTileAdr = 0x240; /* Cloud animation bank. */
    KdcPrepareSelectSky(p, rom, sizeof(rom));
    assert(p->presentationPrepare);
    /* Another owner's per-line hook (the edge HUD) survives other scenes. */
    p->bgXsc[2] = 0x3c;
    p->presentationPrepare = OtherPrepare;
    KdcPrepareSelectSky(p, rom, sizeof(rom));
    assert(p->presentationPrepare == OtherPrepare);
    free(p);
    return 0;
}
