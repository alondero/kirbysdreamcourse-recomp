/* Host-side extra course pack catalog and IPS course loader.
 *
 * Provides:
 *   1. Built-in wrappers for the verified USA ROM's 16 single-player courses
 *      (1P Normal 1-8 and 1P Extra 1-8) so they can be played in 2P / 1P vs CPU.
 *   2. An IPS parser for user-supplied Grump's Dream Course IPS patches
 *      (e.g. GrumpsDreamCourse[1.01].ips placed in courses/ or passed via
 *      --course-pack). Although the IPS targets Japanese Kirby Bowl, all 152
 *      custom holes (19 courses) and their course names, palette indices,
 *      scenery themes and hole music IDs are 100% self-contained inside the
 *      IPS file and are injected cleanly into the verified USA ROM at runtime.
 *
 * No ROM data, IPS data or extracted assets are shipped in this repository. */
#include "course_pack.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#endif

enum {
    kMaxIpsFileBytes = 4u * 1024u * 1024u,
    kMaxExpandedBytes = 4u * 1024u * 1024u,
    kGdcCourses = 19u,
    kGdcHoles = kGdcCourses * KDC_PACK_HOLES_PER_COURSE, /* 152 */
    kGdcTableBase = 0x52670u,
    kGdcTableStride = 0x300u,
    kGdcNamesOffset = 0x4652au,
    kGdcPal1Offset = 0x5425u,
    kGdcAnimPalOffset = 0x204afu,
    kGdcMusicOffset = 0x7440u,
    kGdcExpandedMin = 0x140000u,
    kGdcPal1Base = 0xd4a9u,
    kGdcAnimPalBase = 0xa444u,
};

static KdcCoursePack packs[KDC_PACK_MAX_PACKS];
static unsigned pack_count;
static bool catalog_initialized;
static bool directory_scanned;
static bool menu_enabled;
static bool cli_forced;

static bool active;
static unsigned active_pack_index;
static unsigned active_course_index;
static uint32_t load_generation;

static unsigned ReadWord(const uint8_t *b, size_t off)
{
    return (unsigned)b[off] | ((unsigned)b[off + 1] << 8);
}

static uint32_t ReadLong24(const uint8_t *b, size_t off)
{
    return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) | ((uint32_t)b[off + 2] << 16);
}

static void SetError(char *buf, size_t cap, const char *msg)
{
    if (buf && cap) snprintf(buf, cap, "%s", msg);
}

static uint8_t ReverseBits8(uint8_t v)
{
    v = (uint8_t)(((v & 0xf0u) >> 4) | ((v & 0x0fu) << 4));
    v = (uint8_t)(((v & 0xccu) >> 2) | ((v & 0x33u) << 2));
    return (uint8_t)(((v & 0xaau) >> 1) | ((v & 0x55u) << 1));
}

bool KdcHalDecompress(const uint8_t *src, size_t src_size,
                      uint8_t *dst, size_t dst_cap,
                      size_t *out_size, size_t *out_consumed)
{
    if (!src || !dst) return false;
    size_t i = 0, o = 0;
    while (i < src_size) {
        uint8_t cmd = src[i];
        if (cmd == 0xffu) {
            if (out_size) *out_size = o;
            if (out_consumed) *out_consumed = i + 1u;
            return true;
        }
        uint8_t op;
        size_t count;
        if ((cmd & 0xe0u) == 0xe0u) {
            if (i + 1 >= src_size) return false;
            op = (uint8_t)((cmd << 3) & 0xe0u);
            count = ((((size_t)(cmd & 0x03u)) << 8) | src[i + 1]) + 1u;
            i += 2;
        } else {
            op = (uint8_t)(cmd & 0xe0u);
            count = (size_t)(cmd & 0x1fu) + 1u;
            i += 1;
        }
        switch (op) {
        case 0x00u:
            if (i + count > src_size || o + count > dst_cap) return false;
            memcpy(dst + o, src + i, count);
            i += count;
            o += count;
            break;
        case 0x20u:
            if (i + 1 > src_size || o + count > dst_cap) return false;
            memset(dst + o, src[i], count);
            i += 1;
            o += count;
            break;
        case 0x40u:
            if (i + 2 > src_size || o + count * 2u > dst_cap) return false;
            for (size_t k = 0; k < count; ++k) {
                dst[o++] = src[i];
                dst[o++] = src[i + 1];
            }
            i += 2;
            break;
        case 0x60u: {
            if (i + 1 > src_size || o + count > dst_cap) return false;
            uint8_t v = src[i++];
            for (size_t k = 0; k < count; ++k) dst[o++] = v++;
            break;
        }
        case 0x80u:
        case 0xa0u:
        case 0xc0u: {
            if (i + 2 > src_size || o + count > dst_cap) return false;
            size_t ref = ((size_t)src[i] << 8) | src[i + 1];
            i += 2;
            for (size_t k = 0; k < count; ++k) {
                if (ref >= o) return false;
                uint8_t b = dst[ref];
                dst[o++] = (op == 0xa0u) ? ReverseBits8(b) : b;
                if (op == 0xc0u) {
                    if (k + 1 < count && ref == 0) return false;
                    ref--;
                } else {
                    ref++;
                }
            }
            break;
        }
        default:
            return false;
        }
    }
    return false;
}

/* USA vanilla per-hole music table ($80:C9D5..$80:CA14) for 1P courses 0..7.
 * Extra 1P courses 8..15 reuse the exact same eight rows in $80:C99D. */
static const uint8_t kUsaSoloMusic[8][8] = {
    {0x89, 0x89, 0x89, 0x89, 0x85, 0x85, 0x85, 0x8b},
    {0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x84, 0x8b},
    {0x89, 0x89, 0x89, 0x89, 0x89, 0x89, 0x89, 0x8b},
    {0x8c, 0x8c, 0x8c, 0x8c, 0x8c, 0x8c, 0x8c, 0x8b},
    {0x88, 0x88, 0x88, 0x88, 0x86, 0x86, 0x86, 0x8b},
    {0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x8b},
    {0x88, 0x88, 0x89, 0x89, 0x84, 0x84, 0x84, 0x8b},
    {0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8a, 0x8b},
};

static bool AddBuiltinSoloPack(const char *id, const char *name,
                               const char *short_name, unsigned base_course)
{
    if (pack_count >= KDC_PACK_MAX_PACKS) return false;
    KdcPackCourse *courses = calloc(8u, sizeof(*courses));
    if (!courses) return false;
    for (unsigned c = 0; c < 8u; ++c) {
        unsigned rom_course = base_course + c;
        uint8_t pal = (uint8_t)((rom_course == 14u || rom_course == 15u) ? 8u : (c & 7u));
        snprintf(courses[c].name, sizeof(courses[c].name),
                 base_course ? "EXTRA COURSE %u" : "COURSE %u", c + 1u);
        courses[c].palette = pal;
        courses[c].anim_palette = pal;
        courses[c].theme = (uint8_t)(c & 7u);
        for (unsigned h = 0; h < KDC_PACK_HOLES_PER_COURSE; ++h) {
            courses[c].holes[h].external = false;
            courses[c].holes[h].rom_map = (uint8_t)(rom_course * 8u + h);
            courses[c].holes[h].music_id = kUsaSoloMusic[c][h];
        }
    }
    KdcCoursePack *p = &packs[pack_count++];
    memset(p, 0, sizeof(*p));
    snprintf(p->id, sizeof(p->id), "%s", id);
    snprintf(p->name, sizeof(p->name), "%s", name);
    snprintf(p->short_name, sizeof(p->short_name), "%s", short_name);
    p->external = false;
    p->course_count = 8u;
    p->courses = courses;
    return true;
}

static void EnsureBuiltins(void)
{
    if (catalog_initialized) return;
    catalog_initialized = true;
    AddBuiltinSoloPack("1p-normal", "1P NORMAL", "1P NORMAL", 0u);
    AddBuiltinSoloPack("1p-extra", "1P EXTRA", "1P EXTRA", 8u);
}

void KdcCoursePacksResetCatalog(void)
{
    for (unsigned i = 0; i < pack_count; ++i) {
        free(packs[i].courses);
        free(packs[i].storage);
        memset(&packs[i], 0, sizeof(packs[i]));
    }
    pack_count = 0;
    catalog_initialized = false;
    directory_scanned = false;
    cli_forced = false;
    active = false;
    active_pack_index = 0;
    active_course_index = 0;
    EnsureBuiltins();
}

static uint8_t MapJpSongToUsa(uint8_t jp_song)
{
    /* Observed JP Kirby Bowl -> USA Kirby's Dream Course SPC track table:
     * 0x7E (silence) and 0x82 (title) stay at their slots; 0x85..0x9F shift
     * down by 1 to USA 0x84..0x9E. Anything out of range falls back to 0x84. */
    if (jp_song == 0x7eu || jp_song == 0x82u) return jp_song;
    if (jp_song >= 0x85u && jp_song <= 0x9fu) return (uint8_t)(jp_song - 1u);
    if (jp_song >= 0x7fu && jp_song <= 0x84u) return jp_song;
    return 0x84u;
}

static void FormatGdcCourseName(const uint8_t *raw16, char *out, size_t cap)
{
    size_t len = 16u;
    while (len > 0 && raw16[len - 1] == ' ') --len;
    size_t w = 0;
    for (size_t i = 0; i < len && w + 1 < cap; ++i) {
        unsigned char c = raw16[i];
        if (c == '[') c = '\'';
        else if (c == '<') c = '&';
        else if (c == '=') c = '-';
        else if (c < 0x20u || c >= 0x7fu) c = ' ';
        out[w++] = (char)toupper(c);
    }
    out[w] = '\0';
}

static bool LoromToFileOffset(uint32_t snes_addr, uint32_t *out_offset)
{
    uint32_t bank = (snes_addr >> 16) & 0xffu;
    uint32_t addr = snes_addr & 0xffffu;
    if (addr < 0x8000u) return false;
    if (bank >= 0x80u) bank -= 0x80u;
    if (bank >= 0x80u) return false;
    *out_offset = bank * 0x8000u + (addr - 0x8000u);
    return true;
}

static bool RangeCovered(const uint8_t *mask, size_t mask_size, size_t off, size_t len)
{
    if (off > mask_size || len > mask_size - off) return false;
    for (size_t i = 0; i < len; ++i)
        if (!mask[off + i]) return false;
    return true;
}

bool KdcCoursePackLoadIpsMemory(const uint8_t *ips, size_t ips_size,
                                const char *label,
                                char *error_buf, size_t error_size)
{
    EnsureBuiltins();
    if (pack_count >= KDC_PACK_MAX_PACKS) {
        SetError(error_buf, error_size, "Maximum number of course packs reached.");
        return false;
    }
    if (!ips || ips_size < 8u || ips_size > kMaxIpsFileBytes ||
        memcmp(ips, "PATCH", 5) != 0) {
        SetError(error_buf, error_size, "Not a valid IPS file.");
        return false;
    }

    /* Pass 1: validate IPS record stream and determine max expanded size. */
    size_t pos = 5u;
    size_t max_end = 0u;
    bool saw_eof = false;
    while (pos < ips_size) {
        if (pos + 3u <= ips_size && memcmp(ips + pos, "EOF", 3) == 0) {
            pos += 3u;
            if (pos == ips_size || pos + 3u == ips_size) saw_eof = true;
            break;
        }
        if (pos + 5u > ips_size) break;
        size_t off = ((size_t)ips[pos] << 16) | ((size_t)ips[pos + 1] << 8) | ips[pos + 2];
        size_t sz = ((size_t)ips[pos + 3] << 8) | ips[pos + 4];
        pos += 5u;
        if (sz == 0u) {
            if (pos + 3u > ips_size) break;
            size_t rle = ((size_t)ips[pos] << 8) | ips[pos + 1];
            pos += 3u;
            if (off + rle > kMaxExpandedBytes) {
                SetError(error_buf, error_size, "IPS patch exceeds maximum supported ROM size.");
                return false;
            }
            if (off + rle > max_end) max_end = off + rle;
        } else {
            if (pos + sz > ips_size) break;
            pos += sz;
            if (off + sz > kMaxExpandedBytes) {
                SetError(error_buf, error_size, "IPS patch exceeds maximum supported ROM size.");
                return false;
            }
            if (off + sz > max_end) max_end = off + sz;
        }
    }
    if (!saw_eof || max_end <= kGdcExpandedMin) {
        SetError(error_buf, error_size, "IPS patch is truncated or does not contain expanded course banks.");
        return false;
    }

    /* Pass 2: materialize patched bytes and a byte-coverage mask. */
    uint8_t *img = calloc(max_end, 1u);
    uint8_t *mask = calloc(max_end, 1u);
    if (!img || !mask) {
        free(img);
        free(mask);
        SetError(error_buf, error_size, "Out of memory loading IPS course pack.");
        return false;
    }
    pos = 5u;
    while (pos + 3u <= ips_size && memcmp(ips + pos, "EOF", 3) != 0) {
        size_t off = ((size_t)ips[pos] << 16) | ((size_t)ips[pos + 1] << 8) | ips[pos + 2];
        size_t sz = ((size_t)ips[pos + 3] << 8) | ips[pos + 4];
        pos += 5u;
        if (sz == 0u) {
            size_t rle = ((size_t)ips[pos] << 8) | ips[pos + 1];
            uint8_t val = ips[pos + 2];
            pos += 3u;
            memset(img + off, val, rle);
            memset(mask + off, 1, rle);
        } else {
            memcpy(img + off, ips + pos, sz);
            memset(mask + off, 1, sz);
            pos += sz;
        }
    }

    /* Validate Grump's Dream Course 11-table map index and metadata coverage. */
    bool tables_ok = true;
    for (unsigned t = 0; t < KDC_PACK_STREAMS; ++t) {
        size_t base = kGdcTableBase + t * kGdcTableStride;
        if (!RangeCovered(mask, max_end, base, kGdcHoles * 3u)) {
            tables_ok = false;
            break;
        }
    }
    if (!tables_ok ||
        !RangeCovered(mask, max_end, kGdcNamesOffset, kGdcCourses * 16u) ||
        !RangeCovered(mask, max_end, kGdcPal1Offset, kGdcCourses * 2u) ||
        !RangeCovered(mask, max_end, kGdcAnimPalOffset, kGdcCourses * 2u) ||
        !RangeCovered(mask, max_end, kGdcMusicOffset, kGdcHoles)) {
        free(img);
        free(mask);
        SetError(error_buf, error_size,
                 "IPS file does not match Grump's Dream Course 152-hole layout.");
        return false;
    }

    /* Verify expanded region coverage and decompress/validate all 152 holes. */
    KdcPackCourse *courses = calloc(kGdcCourses, sizeof(*courses));
    uint8_t *scratch = malloc(0x6800u);
    if (!courses || !scratch) {
        free(courses);
        free(scratch);
        free(img);
        free(mask);
        SetError(error_buf, error_size, "Out of memory allocating course pack.");
        return false;
    }

    /* GDC bank 0 theme-table permutation mapped to USA scenery themes 0..7. */
    static const uint8_t kGdcThemeMap[8] = {0, 7, 2, 1, 5, 4, 0, 6};
    static const size_t kStreamMaxOut[KDC_PACK_STREAMS] = {
        32u, 0x800u, 0x800u, 0x800u, 0x800u,
        0x800u, 0x800u, 0x800u, 0x6800u, 0x6800u, 0x800u,
    };

    bool valid = true;
    for (unsigned c = 0; c < kGdcCourses && valid; ++c) {
        FormatGdcCourseName(img + kGdcNamesOffset + c * 16u,
                            courses[c].name, sizeof(courses[c].name));
        unsigned raw_pal = ReadWord(img, kGdcPal1Offset + c * 2u);
        unsigned pal = (raw_pal >= kGdcPal1Base && (raw_pal - kGdcPal1Base) % 0x20u == 0u)
                           ? (raw_pal - kGdcPal1Base) / 0x20u : (c & 7u);
        unsigned raw_anim = ReadWord(img, kGdcAnimPalOffset + c * 2u);
        unsigned anim = (raw_anim >= kGdcAnimPalBase && (raw_anim - kGdcAnimPalBase) % 0x120u == 0u)
                            ? (raw_anim - kGdcAnimPalBase) / 0x120u : pal;
        courses[c].palette = (uint8_t)(pal < 10u ? pal : 0u);
        courses[c].anim_palette = (uint8_t)(anim < 10u ? anim : courses[c].palette);
        courses[c].theme = kGdcThemeMap[c & 7u];

        for (unsigned h = 0; h < KDC_PACK_HOLES_PER_COURSE && valid; ++h) {
            unsigned hid = c * KDC_PACK_HOLES_PER_COURSE + h;
            KdcPackHole *hole = &courses[c].holes[h];
            hole->external = true;
            hole->rom_map = 0u;
            hole->music_id = MapJpSongToUsa(img[kGdcMusicOffset + hid]);

            for (unsigned t = 0; t < KDC_PACK_STREAMS; ++t) {
                uint32_t ptr = ReadLong24(img, kGdcTableBase + t * kGdcTableStride + hid * 3u);
                uint32_t fo = 0u;
                if (!LoromToFileOffset(ptr, &fo) || fo < kGdcExpandedMin || fo >= max_end) {
                    valid = false;
                    break;
                }
                size_t avail = max_end - fo;
                if (t == 0u) {
                    if (avail < 32u || !RangeCovered(mask, max_end, fo, 32u)) {
                        valid = false;
                        break;
                    }
                    unsigned w = ReadWord(img, fo + 2u);
                    unsigned ht = ReadWord(img, fo + 4u);
                    if (!w || !ht || w > 255u || ht > 255u || w * ht > 0x800u) {
                        valid = false;
                        break;
                    }
                    hole->stream_offset[0] = fo - kGdcExpandedMin;
                    hole->stream_size[0] = 32u;
                } else {
                    size_t dec_len = 0u, consumed = 0u;
                    if (!KdcHalDecompress(img + fo, avail, scratch, kStreamMaxOut[t],
                                          &dec_len, &consumed) ||
                        !RangeCovered(mask, max_end, fo, consumed)) {
                        valid = false;
                        break;
                    }
                    if (t == 1u) {
                        for (size_t k = 0; k < dec_len; ++k)
                            if (scratch[k] >= 28u) { valid = false; break; }
                    } else if (t == 2u) {
                        bool has_tee = false;
                        for (size_t k = 0; k < dec_len; ++k)
                            if (scratch[k] == 0x0cu || scratch[k] == 0xc3u) { has_tee = true; break; }
                        if (!has_tee) valid = false;
                    } else if (t == 3u) {
                        for (size_t k = 0; k < dec_len; ++k)
                            if (scratch[k] >= 100u) { valid = false; break; }
                    }
                    if (!valid) break;
                    hole->stream_offset[t] = fo - kGdcExpandedMin;
                    hole->stream_size[t] = (uint32_t)consumed;
                }
            }
        }
    }

    free(scratch);
    free(mask);
    if (!valid) {
        free(courses);
        free(img);
        SetError(error_buf, error_size, "IPS course data failed structural validation.");
        return false;
    }

    /* Compact storage: keep only the expanded banks (kGdcExpandedMin..max_end). */
    size_t storage_size = max_end - kGdcExpandedMin;
    uint8_t *storage = malloc(storage_size);
    if (!storage) {
        free(courses);
        free(img);
        SetError(error_buf, error_size, "Out of memory storing course pack.");
        return false;
    }
    memcpy(storage, img + kGdcExpandedMin, storage_size);
    free(img);

    unsigned ext_num = KdcCoursePackExternalCount();
    KdcCoursePack *p = &packs[pack_count++];
    memset(p, 0, sizeof(*p));
    if (ext_num == 0u) {
        snprintf(p->id, sizeof(p->id), "gdc");
        snprintf(p->name, sizeof(p->name), "GRUMP'S DREAM");
        snprintf(p->short_name, sizeof(p->short_name), "GRUMP'S");
    } else {
        snprintf(p->id, sizeof(p->id), "gdc-%u", ext_num + 1u);
        snprintf(p->name, sizeof(p->name), "GRUMP'S DREAM %u", ext_num + 1u);
        snprintf(p->short_name, sizeof(p->short_name), "GDC%u", ext_num + 1u);
    }
    p->external = true;
    p->course_count = kGdcCourses;
    p->courses = courses;
    p->storage = storage;
    p->storage_size = storage_size;

    fprintf(stderr, "Course pack loaded (%s): %s (%u courses, %u holes)\n",
            label ? label : "memory", p->name, p->course_count,
            p->course_count * KDC_PACK_HOLES_PER_COURSE);
    return true;
}

bool KdcCoursePackLoadIpsFile(const char *path, char *error_buf, size_t error_size)
{
    if (!path || !*path) {
        SetError(error_buf, error_size, "Missing IPS file path.");
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        SetError(error_buf, error_size, "Could not open IPS file.");
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        SetError(error_buf, error_size, "Could not measure IPS file.");
        return false;
    }
    long len = ftell(file);
    if (len < 8L || (size_t)len > kMaxIpsFileBytes || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        SetError(error_buf, error_size, "IPS file size is invalid.");
        return false;
    }
    uint8_t *buf = malloc((size_t)len);
    if (!buf) {
        fclose(file);
        SetError(error_buf, error_size, "Out of memory reading IPS file.");
        return false;
    }
    bool ok = fread(buf, 1u, (size_t)len, file) == (size_t)len;
    fclose(file);
    if (!ok) {
        free(buf);
        SetError(error_buf, error_size, "Could not read IPS file.");
        return false;
    }
    ok = KdcCoursePackLoadIpsMemory(buf, (size_t)len, path, error_buf, error_size);
    free(buf);
    return ok;
}

static bool HasIpsExtension(const char *name)
{
    size_t len = strlen(name);
    if (len < 4u) return false;
    const char *ext = name + len - 4u;
    return ext[0] == '.' &&
           tolower((unsigned char)ext[1]) == 'i' &&
           tolower((unsigned char)ext[2]) == 'p' &&
           tolower((unsigned char)ext[3]) == 's';
}

static int CompareStrings(const void *a, const void *b)
{
    const char *const *sa = a;
    const char *const *sb = b;
    return strcmp(*sa, *sb);
}

void KdcCoursePacksScanDirectory(const char *dir_path)
{
    EnsureBuiltins();
    if (!dir_path || !*dir_path) return;

    char *names[KDC_PACK_MAX_PACKS];
    unsigned found = 0u;

#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir_path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
            if (!HasIpsExtension(fd.cFileName) || found >= KDC_PACK_MAX_PACKS) continue;
            size_t nlen = strlen(fd.cFileName) + 1u;
            char *copy = malloc(nlen);
            if (copy) { memcpy(copy, fd.cFileName, nlen); names[found++] = copy; }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *dir = opendir(dir_path);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (!HasIpsExtension(ent->d_name) || found >= KDC_PACK_MAX_PACKS) continue;
            size_t nlen = strlen(ent->d_name) + 1u;
            char *copy = malloc(nlen);
            if (copy) { memcpy(copy, ent->d_name, nlen); names[found++] = copy; }
        }
        closedir(dir);
    }
#endif

    if (found > 1u) qsort(names, found, sizeof(names[0]), CompareStrings);
    for (unsigned i = 0; i < found; ++i) {
        char full[640], err[160] = {0};
        snprintf(full, sizeof(full), "%s/%s", dir_path, names[i]);
        if (!KdcCoursePackLoadIpsFile(full, err, sizeof(err))) {
            fprintf(stderr, "Ignored course file %s: %s\n", full, err);
        }
        free(names[i]);
    }
}

void KdcCoursePacksEnsureLoaded(void)
{
    EnsureBuiltins();
    if (directory_scanned) return;
    directory_scanned = true;
    KdcCoursePacksScanDirectory("courses");
}

bool KdcCoursePacksAddCliPath(const char *path)
{
    EnsureBuiltins();
    char err[160] = {0};
    if (!KdcCoursePackLoadIpsFile(path, err, sizeof(err))) {
        fprintf(stderr, "Could not load course pack '%s': %s\n", path ? path : "", err);
        return false;
    }
    cli_forced = true;
    return true;
}

unsigned KdcCoursePackCount(void)
{
    EnsureBuiltins();
    return pack_count;
}

unsigned KdcCoursePackExternalCount(void)
{
    EnsureBuiltins();
    unsigned n = 0u;
    for (unsigned i = 0; i < pack_count; ++i)
        if (packs[i].external) ++n;
    return n;
}

const KdcCoursePack *KdcCoursePackGet(unsigned index)
{
    EnsureBuiltins();
    return index < pack_count ? &packs[index] : NULL;
}

int KdcCoursePackFindById(const char *id)
{
    EnsureBuiltins();
    if (!id || !*id) return -1;
    for (unsigned i = 0; i < pack_count; ++i)
        if (!strcmp(packs[i].id, id)) return (int)i;
    return -1;
}

void KdcCoursePacksSetEnabled(bool enabled) { menu_enabled = enabled; }
bool KdcCoursePacksMenuEnabled(void) { return menu_enabled || cli_forced; }

void KdcCoursePacksDescribeStatus(char *out, size_t out_size)
{
    KdcCoursePacksEnsureLoaded();
    unsigned ext_packs = KdcCoursePackExternalCount();
    unsigned ext_courses = 0u;
    for (unsigned i = 0; i < pack_count; ++i)
        if (packs[i].external) ext_courses += packs[i].course_count;
    if (ext_packs > 0u) {
        snprintf(out, out_size,
                 "Add MORE COURSES to 2P and 1P vs CPU (16 solo courses plus %u "
                 "custom course%s from courses/). Loaded IPS packs also appear "
                 "in Hole Practice.",
                 ext_courses, ext_courses == 1u ? "" : "s");
    } else {
        snprintf(out, out_size,
                 "Add MORE COURSES to 2P and 1P vs CPU (16 solo courses). "
                 "Place GrumpsDreamCourse[1.01].ips in courses/ to add 19 "
                 "more courses (152 holes) and practice them too.");
    }
}

bool KdcCoursePackSelect(unsigned pack_index, unsigned course_index)
{
    EnsureBuiltins();
    if (pack_index >= pack_count || course_index >= packs[pack_index].course_count)
        return false;
    active = true;
    active_pack_index = pack_index;
    active_course_index = course_index;
    ++load_generation;
    return true;
}

void KdcCoursePackClearActive(void)
{
    if (active) ++load_generation;
    active = false;
}

bool KdcCoursePackHasActive(void) { return active; }

bool KdcCoursePackActiveIsExternal(void)
{
    return active && active_pack_index < pack_count && packs[active_pack_index].external;
}

const KdcCoursePack *KdcCoursePackActivePack(void)
{
    return (active && active_pack_index < pack_count) ? &packs[active_pack_index] : NULL;
}

const KdcPackCourse *KdcCoursePackActiveCourse(void)
{
    if (!active || active_pack_index >= pack_count) return NULL;
    const KdcCoursePack *p = &packs[active_pack_index];
    return active_course_index < p->course_count ? &p->courses[active_course_index] : NULL;
}

uint32_t KdcCoursePackGeneration(void) { return load_generation; }
void KdcCoursePackBumpGeneration(void) { ++load_generation; }
