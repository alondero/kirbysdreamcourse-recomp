/* HD interface texture pack: lookup by character content, recolour live. */
#include "hd_pack.h"
#include "snes/ppu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kPackHeader = 24, kPackEntry = 16, kPackTile = 32 * 32 * 5, kPackAnyAddress = 0xffff };

typedef struct PackEntry {
    uint64_t hash;
    uint16_t address;
    uint8_t bpp, layer;
    uint32_t offset;
} PackEntry;

static struct {
    uint8_t *data;
    PackEntry *entries;
    unsigned count;
    bool tried;
} g_pack;

static uint32_t Le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

void KdcHdPackUnload(void)
{
    free(g_pack.data);
    free(g_pack.entries);
    memset(&g_pack, 0, sizeof(g_pack));
}

static int Compare(uint64_t hash, unsigned bpp, unsigned layer, unsigned address, const PackEntry *e)
{
    if (hash != e->hash) return hash < e->hash ? -1 : 1;
    if (bpp != e->bpp) return bpp < e->bpp ? -1 : 1;
    if (layer != e->layer) return layer < e->layer ? -1 : 1;
    if (address != e->address) return address < e->address ? -1 : 1;
    return 0;
}

bool KdcHdPackLoadMemory(const void *source, size_t size)
{
    KdcHdPackUnload();
    g_pack.tried = true;
    const uint8_t *p = source;
    if (!p || size < kPackHeader || memcmp(p, "KDCHDUI1", 8) || Le32(p + 8) != 1 || Le32(p + 16) != 4)
        return false;
    const uint32_t count = Le32(p + 12);
    if (count > (size - kPackHeader) / kPackEntry) return false;
    PackEntry *entries = calloc(count ? count : 1, sizeof(*entries));
    uint8_t *data = malloc(size);
    if (!entries || !data) {
        free(entries);
        free(data);
        return false;
    }
    memcpy(data, p, size);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *e = data + kPackHeader + (size_t)i * kPackEntry;
        PackEntry *out = &entries[i];
        out->hash = (uint64_t)Le32(e) | (uint64_t)Le32(e + 4) << 32;
        out->address = (uint16_t)(e[8] | e[9] << 8);
        out->bpp = e[10];
        out->layer = e[11];
        out->offset = Le32(e + 12);
        if ((out->bpp != 2 && out->bpp != 4) || out->offset > size || size - out->offset < kPackTile ||
            (i && Compare(out->hash, out->bpp, out->layer, out->address, &entries[i - 1]) <= 0)) {
            free(entries);
            free(data);
            return false;
        }
    }
    g_pack.data = data;
    g_pack.entries = entries;
    g_pack.count = count;
    return true;
}

bool KdcHdPackLoadFile(const char *path)
{
    KdcHdPackUnload();
    g_pack.tried = true;
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return false;
    bool ok = false;
    if (fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);
        if (size > 0 && fseek(f, 0, SEEK_SET) == 0) {
            uint8_t *buffer = malloc((size_t)size);
            if (buffer && fread(buffer, 1, (size_t)size, f) == (size_t)size)
                ok = KdcHdPackLoadMemory(buffer, (size_t)size);
            free(buffer);
        }
    }
    fclose(f);
    g_pack.tried = true;
    return ok;
}

bool KdcHdPackAvailable(void)
{
    if (!g_pack.tried) {
        const char *path = getenv("KDC_HD_PACK");
        if (!KdcHdPackLoadFile(path && *path ? path : "kdc-hd-ui.pack") && path && *path)
            fprintf(stderr, "[hd] texture pack '%s' could not be loaded\n", path);
    }
    return g_pack.count != 0;
}

unsigned KdcHdPackCount(void) { return g_pack.count; }

static uint64_t HashWords(const uint16_t *words, unsigned count)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (unsigned i = 0; i < count; ++i) {
        h = (h ^ (words[i] & 0xffu)) * UINT64_C(0x100000001b3);
        h = (h ^ (words[i] >> 8 & 0xffu)) * UINT64_C(0x100000001b3);
    }
    return h;
}

static const PackEntry *Find(const PpuHdTileQuery *q)
{
    const uint64_t hash = HashWords(q->words, 4u * q->bpp);
    unsigned lo = 0, hi = g_pack.count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (Compare(hash, q->bpp, q->layer, 0, &g_pack.entries[mid]) > 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    const PackEntry *any = NULL;
    for (; lo < g_pack.count; ++lo) {
        const PackEntry *e = &g_pack.entries[lo];
        if (e->hash != hash || e->bpp != q->bpp || e->layer != q->layer) break;
        if (e->address == q->address) return e;
        if (e->address == kPackAnyAddress) any = e;
    }
    return any;
}

bool KdcHdPackTile(const PpuHdTileQuery *q, uint32_t *pixels)
{
    if (!q || !pixels || q->scale != 4 || (q->bpp != 2 && q->bpp != 4) || !KdcHdPackAvailable()) return false;
    const PackEntry *e = Find(q);
    if (!e) return false;
    uint32_t rgb[16] = {0};
    for (unsigned i = 1; i < (1u << q->bpp); ++i) {
        const unsigned c = q->colors[i];
        const uint32_t v = ((c & 31u) << 19) | ((c & 992u) << 6) | ((c >> 10 & 31u) << 3);
        rgb[i] = v | ((v >> 5) & 0x070707u);
    }
    const uint8_t *s = g_pack.data + e->offset;
    for (unsigned k = 0; k < 32 * 32; ++k, s += 5) {
        const unsigned idx[3] = {s[0] & 15u, s[0] >> 4, s[1] & 15u};
        const unsigned w[3] = {s[2], s[3], s[4]};
        const unsigned alpha = w[0] + w[1] + w[2];
        if (!alpha) {
            pixels[k] = 0;
            continue;
        }
        uint32_t out = (alpha > 255 ? 255u : alpha) << 24;
        for (unsigned shift = 0; shift <= 16; shift += 8) {
            unsigned sum = 0;
            for (unsigned j = 0; j < 3; ++j) sum += w[j] * (rgb[idx[j]] >> shift & 255u);
            out |= ((sum + alpha / 2) / alpha) << shift;
        }
        pixels[k] = out;
    }
    return true;
}
