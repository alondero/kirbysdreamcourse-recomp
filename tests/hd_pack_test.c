/* ROM-free unit test for the HD interface texture pack (src/hd_pack.c). */
#include "hd_pack.h"
#include "snes/ppu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kTile = 32 * 32 * 5 };

static uint64_t Hash(const uint16_t *words, unsigned count)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (unsigned i = 0; i < count; ++i) {
        h = (h ^ (words[i] & 0xffu)) * UINT64_C(0x100000001b3);
        h = (h ^ (words[i] >> 8 & 0xffu)) * UINT64_C(0x100000001b3);
    }
    return h;
}

static void Put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

typedef struct Entry {
    uint64_t hash;
    uint16_t address;
    uint8_t bpp, layer;
    uint8_t first_index; /* index painted fully into sample 0 */
} Entry;

/* Builds a pack whose tiles all share one sample layout:
 *   sample 0: first_index at full weight
 *   sample 1: indices 1 and 2 mixed half and half
 *   sample 2: index 3 at half coverage
 *   others:   transparent */
static uint8_t *BuildPack(const Entry *entries, unsigned count, size_t *size)
{
    *size = 24 + (size_t)count * 16 + (size_t)count * kTile;
    uint8_t *p = calloc(1, *size);
    memcpy(p, "KDCHDUI1", 8);
    Put32(p + 8, 1);
    Put32(p + 12, count);
    Put32(p + 16, 4);
    for (unsigned i = 0; i < count; ++i) {
        uint8_t *e = p + 24 + i * 16;
        Put32(e, (uint32_t)entries[i].hash);
        Put32(e + 4, (uint32_t)(entries[i].hash >> 32));
        e[8] = (uint8_t)entries[i].address;
        e[9] = (uint8_t)(entries[i].address >> 8);
        e[10] = entries[i].bpp;
        e[11] = entries[i].layer;
        uint32_t offset = 24 + count * 16 + i * kTile;
        Put32(e + 12, offset);
        uint8_t *t = p + offset;
        t[0] = entries[i].first_index;
        t[2] = 255;
        t[5] = 1 | 2 << 4;
        t[7] = 128;
        t[8] = 127;
        t[10] = 3;
        t[12] = 128;
    }
    return p;
}

static uint16_t Bgr(unsigned r, unsigned g, unsigned b) { return (uint16_t)(r | g << 5 | b << 10); }

int main(void)
{
    PpuHdTileQuery q;
    memset(&q, 0, sizeof(q));
    q.bpp = 2;
    q.layer = 2;
    q.scale = 4;
    for (unsigned i = 0; i < 8; ++i) q.words[i] = (uint16_t)(0x1234 + i * 0x0101);
    q.colors[1] = Bgr(31, 0, 0);
    q.colors[2] = Bgr(0, 0, 31);
    q.colors[3] = Bgr(0, 31, 0);
    const uint64_t hash = Hash(q.words, 8);
    uint32_t px[1024];

    /* Malformed or truncated data is rejected and leaves no pack loaded. */
    assert(!KdcHdPackLoadMemory("nope", 4));
    size_t size;
    Entry one = {hash, 0xffff, 2, 2, 1};
    uint8_t *pack = BuildPack(&one, 1, &size);
    assert(!KdcHdPackLoadMemory(pack, size - 1));
    assert(KdcHdPackCount() == 0);
    assert(!KdcHdPackTile(&q, px));

    /* A matching character is recoloured from the query's live palette. */
    assert(KdcHdPackLoadMemory(pack, size));
    assert(KdcHdPackCount() == 1);
    assert(KdcHdPackTile(&q, px));
    assert(px[0] == 0xffff0000u);              /* index 1: red */
    assert(px[1] == 0xff80007fu);              /* red/blue half mix */
    assert(px[2] == 0x8000ff00u);              /* half-covered green */
    assert(px[3] == 0);
    /* A palette change recolours the same artwork. */
    q.colors[1] = Bgr(31, 31, 31);
    assert(KdcHdPackTile(&q, px) && px[0] == 0xffffffffu);

    /* Other content, depth or layer is not covered. */
    q.layer = 4;
    assert(!KdcHdPackTile(&q, px));
    q.layer = 2;
    q.words[0] ^= 1;
    assert(!KdcHdPackTile(&q, px));
    q.words[0] ^= 1;
    free(pack);

    /* Address-specific artwork wins; other addresses use the shared entry. */
    Entry two[2] = {{hash, 0x1000, 2, 2, 2}, {hash, 0xffff, 2, 2, 1}};
    pack = BuildPack(two, 2, &size);
    assert(KdcHdPackLoadMemory(pack, size));
    q.colors[1] = Bgr(31, 0, 0);
    q.address = 0x1000;
    assert(KdcHdPackTile(&q, px) && px[0] == 0xff0000ffu);
    q.address = 0x2000;
    assert(KdcHdPackTile(&q, px) && px[0] == 0xffff0000u);
    /* Entries must be sorted; an unsorted table is rejected. */
    Entry unsorted[2] = {two[1], two[0]};
    free(pack);
    pack = BuildPack(unsorted, 2, &size);
    assert(!KdcHdPackLoadMemory(pack, size));
    free(pack);

    /* A missing file is not an error and loads nothing. */
    assert(!KdcHdPackLoadFile("local/does-not-exist.pack"));
    assert(KdcHdPackCount() == 0);

    const char *default_pack = "kdc-hd-ui.pack";
    pack = BuildPack(&one, 1, &size);
    FILE *file = fopen(default_pack, "wb");
    assert(file && fwrite(pack, 1, size, file) == size && fclose(file) == 0);
    KdcHdPackUnload();
    assert(!KdcHdPackAvailable());
    assert(!KdcHdPackTile(&q, px));
    KdcHdPackPrewarm();
    assert(KdcHdPackAvailable());
    assert(remove(default_pack) == 0);
    KdcHdPackPrewarm();
    assert(KdcHdPackTile(&q, px));

    KdcHdPackUnload();
    KdcHdPackPrewarm();
    file = fopen(default_pack, "wb");
    assert(file && fwrite(pack, 1, size, file) == size && fclose(file) == 0);
    KdcHdPackPrewarm();
    assert(!KdcHdPackAvailable());
    free(pack);
    puts("hd_pack_test: ok");
    return 0;
}
