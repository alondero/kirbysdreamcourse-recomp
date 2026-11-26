/* HD interface texture pack: lookup by character content, recolour live. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "hd_pack.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef FORCEINLINE
#else
#include <unistd.h>
#endif
#include "snes/ppu.h"

enum { kPackHeader = 24, kPackEntry = 16, kPackTile = 32 * 32 * 5, kPackAnyAddress = 0xffff };

typedef struct PackEntry {
    uint64_t hash;
    uint16_t address;
    uint8_t bpp, layer;
    uint32_t offset;
} PackEntry;

typedef struct {
    uint8_t *data;
    PackEntry *entries;
    unsigned count;
} Pack;

static Pack g_pack;
static bool tried;
static uint64_t revision;
static const char config_path[] = "kdc-hd-ui.cfg";
static const char default_path[] = "kdc-hd-ui.pack";
static const char invalid_pack[] = "Invalid HD interface pack.";
static const char unavailable_pack[] = "Could not read the HD interface pack.";
static const char out_of_memory[] = "Insufficient memory to load the HD interface pack.";

static uint32_t Le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void FreePack(Pack *pack)
{
    free(pack->data);
    free(pack->entries);
    *pack = (Pack){0};
}

void KdcHdPackUnload(void)
{
    if (g_pack.count) ++revision;
    FreePack(&g_pack);
    tried = false;
}

static int Compare(uint64_t hash, unsigned bpp, unsigned layer, unsigned address, const PackEntry *e)
{
    if (hash != e->hash) return hash < e->hash ? -1 : 1;
    if (bpp != e->bpp) return bpp < e->bpp ? -1 : 1;
    if (layer != e->layer) return layer < e->layer ? -1 : 1;
    if (address != e->address) return address < e->address ? -1 : 1;
    return 0;
}

static const char *ParsePack(Pack *pack, uint8_t *data, size_t size)
{
    if (size < kPackHeader || memcmp(data, "KDCHDUI1", 8)) return invalid_pack;
    if (Le32(data + 8) != 1 || Le32(data + 16) != 4)
        return "Unsupported HD interface pack version or scale.";
    const uint32_t count = Le32(data + 12);
    if (!count || count > (size - kPackHeader) / kPackEntry) return invalid_pack;
    const size_t tiles_start = kPackHeader + (size_t)count * kPackEntry;
    PackEntry *entries = calloc(count, sizeof(*entries));
    if (!entries) return out_of_memory;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *e = data + kPackHeader + (size_t)i * kPackEntry;
        PackEntry *out = &entries[i];
        out->hash = (uint64_t)Le32(e) | (uint64_t)Le32(e + 4) << 32;
        out->address = (uint16_t)(e[8] | e[9] << 8);
        out->bpp = e[10];
        out->layer = e[11];
        out->offset = Le32(e + 12);
        if ((out->bpp != 2 && out->bpp != 4) || (out->layer > 2 && out->layer != 4) ||
            out->offset < tiles_start || out->offset > size || size - out->offset < kPackTile ||
            (i && Compare(out->hash, out->bpp, out->layer, out->address, &entries[i - 1]) <= 0)) {
            free(entries);
            return invalid_pack;
        }
    }
    *pack = (Pack){data, entries, count};
    return NULL;
}

bool KdcHdPackLoadMemory(const void *source, size_t size)
{
    KdcHdPackUnload();
    tried = true;
    if (!source || size < kPackHeader) return false;
    uint8_t *data = malloc(size);
    if (!data) return false;
    memcpy(data, source, size);
    if (ParsePack(&g_pack, data, size)) {
        free(data);
        return false;
    }
    ++revision;
    return true;
}

static FILE *OpenPackFile(const char *path, bool override)
{
#ifdef _WIN32
    if (override) {
        const wchar_t *wide = _wgetenv(L"KDC_HD_PACK");
        return wide ? _wfopen(wide, L"rb") : fopen(path, "rb");
    }
    wchar_t wide[kKdcHdPackPathMax];
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide,
                            kKdcHdPackPathMax))
        return _wfopen(wide, L"rb");
    errno = EINVAL;
    return NULL;
#else
    (void)override;
    return fopen(path, "rb");
#endif
}

static const char *ReadPackFile(Pack *pack, const char *path, bool override, bool missing_ok)
{
    FILE *file = OpenPackFile(path, override);
    if (!file) {
        if (errno == ENOENT)
            return missing_ok ? NULL : "HD interface pack file was not found.";
        return unavailable_pack;
    }
    const char *error = unavailable_pack;
    if (fseek(file, 0, SEEK_END) == 0) {
        long size = ftell(file);
        if (size >= 0 && size < kPackHeader) {
            error = invalid_pack;
        } else if (size >= kPackHeader && fseek(file, 0, SEEK_SET) == 0) {
            uint8_t *data = malloc((size_t)size);
            if (!data) {
                error = out_of_memory;
            } else {
                if (fread(data, 1, (size_t)size, file) == (size_t)size)
                    error = ParsePack(pack, data, (size_t)size);
                if (error) free(data);
            }
        }
    }
    fclose(file);
    return error;
}

bool KdcHdPackLoadFile(const char *path)
{
    KdcHdPackUnload();
    tried = true;
    if (!path || ReadPackFile(&g_pack, path, false, false)) return false;
    ++revision;
    return true;
}

KdcHdPackSettings KdcHdPackReadSettings(void)
{
    KdcHdPackSettings settings = {.enabled = true};
    FILE *file = fopen(config_path, "rb");
    if (!file) {
        if (errno == ENOENT) memcpy(settings.path, default_path, sizeof(default_path));
        return settings;
    }
    const int enabled = fgetc(file);
    if ((enabled == '0' || enabled == '1') && fgetc(file) == '\n') {
        const size_t length = fread(settings.path, 1, sizeof(settings.path), file);
        if (length < sizeof(settings.path) && !ferror(file) && !memchr(settings.path, 0, length)) {
            settings.enabled = enabled == '1';
        } else {
            settings.path[0] = '\0';
        }
    }
    fclose(file);
    return settings;
}

bool KdcHdPackSaveSettings(const KdcHdPackSettings *settings)
{
#ifdef _WIN32
    wchar_t temporary[MAX_PATH];
    if (!GetTempFileNameW(L".", L"khd", 0, temporary)) return false;
    FILE *file = _wfopen(temporary, L"wb");
#else
    char temporary[] = "kdc-hd-ui.cfg.XXXXXX";
    const int fd = mkstemp(temporary);
    if (fd < 0) return false;
    FILE *file = fdopen(fd, "wb");
    if (!file) close(fd);
#endif
    bool ok = file && fprintf(file, "%d\n%s", settings->enabled, settings->path) >= 0;
    if (file && fclose(file) != 0) ok = false;
#ifdef _WIN32
    if (ok) ok = MoveFileExW(temporary, L"kdc-hd-ui.cfg", MOVEFILE_REPLACE_EXISTING) != 0;
    if (!ok) DeleteFileW(temporary);
#else
    if (ok) ok = rename(temporary, config_path) == 0;
    if (!ok) remove(temporary);
#endif
    return ok;
}

bool KdcHdPackOverridden(void)
{
    const char *path = getenv("KDC_HD_PACK");
    return path && *path;
}

static const char *ReadSelection(Pack *pack, const KdcHdPackSettings *settings, bool inspect)
{
    const char *override = getenv("KDC_HD_PACK");
    if (override && *override)
        return ReadPackFile(pack, override, true, false);
    if ((!inspect && !settings->enabled) || !settings->path[0]) return NULL;
    const bool missing_ok = strcmp(settings->path, default_path) == 0;
    return ReadPackFile(pack, settings->path, false, missing_ok);
}

const char *KdcHdPackInspect(const KdcHdPackSettings *settings, bool *available)
{
    Pack pack = {0};
    const char *error = ReadSelection(&pack, settings, true);
    *available = pack.count != 0;
    FreePack(&pack);
    return error;
}

const char *KdcHdPackReload(const KdcHdPackSettings *settings)
{
    KdcHdPackUnload();
    tried = true;
    const char *error = ReadSelection(&g_pack, settings, false);
    if (error)
        fprintf(stderr, "[hd] %s (%s)\n", error,
                KdcHdPackOverridden() ? getenv("KDC_HD_PACK") : settings->path);
    if (g_pack.count) ++revision;
    return error;
}

void KdcHdPackPrewarm(void)
{
    if (tried) return;
    const KdcHdPackSettings settings = KdcHdPackReadSettings();
    KdcHdPackReload(&settings);
}

bool KdcHdPackAvailable(void) { return g_pack.count != 0; }

unsigned KdcHdPackCount(void) { return g_pack.count; }

uint64_t KdcHdPackRevision(void) { return revision; }

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
