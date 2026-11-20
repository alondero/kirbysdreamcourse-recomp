#include "course_pack.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); \
        return 1; \
    } \
} while (0)

static uint32_t redirect;
static uint32_t hook_pcs[32];
static InterpPreOpcodeHook hook_fns[32];
static unsigned hook_count;

bool interp_bridge_add_pre_opcode_hook(uint32_t pc, InterpPreOpcodeHook hook)
{
    if (hook_count >= 32) return false;
    hook_pcs[hook_count] = pc;
    hook_fns[hook_count++] = hook;
    return true;
}

void interp_bridge_pre_opcode_redirect(uint32_t pc) { redirect = pc; }

static void Fire(CpuState *cpu, uint32_t pc)
{
    redirect = 0;
    bool matched = false;
    for (unsigned i = 0; i < hook_count; ++i) {
        if (hook_pcs[i] == pc) {
            matched = true;
            hook_fns[i](cpu, pc);
            if (redirect) return;
        }
    }
    if (!matched) {
        fprintf(stderr, "No hook registered for $%06X\n", pc);
        exit(1);
    }
}

static unsigned Word(const uint8_t *ram, size_t off)
{
    return (unsigned)ram[off] | ((unsigned)ram[off + 1] << 8);
}

/* Build a minimal valid synthetic 152-hole GDC-format IPS buffer in memory. */
typedef struct {
    uint8_t *data;
    size_t len, cap;
} Buf;

static void BufPut(Buf *b, const void *src, size_t n)
{
    if (b->len + n > b->cap) {
        b->cap = (b->cap ? b->cap * 2u : 4096u) + n;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
}

static void BufRec(Buf *b, uint32_t offset, const void *payload, uint16_t size)
{
    uint8_t hdr[5] = {
        (uint8_t)(offset >> 16), (uint8_t)(offset >> 8), (uint8_t)offset,
        (uint8_t)(size >> 8), (uint8_t)size,
    };
    BufPut(b, hdr, 5);
    BufPut(b, payload, size);
}

static uint8_t *BuildSyntheticGdcIpsEx(size_t *out_size, uint8_t shape_val,
                                       uint8_t tee_val, unsigned s1_len,
                                       unsigned s2_len, unsigned tee_pos,
                                       unsigned s3_len, unsigned s4_len)
{
    Buf b = {0};
    BufPut(&b, "PATCH", 5);

    /* One shared valid hole placed at file 0x140000 ($A8:8000):
     * Stream 0 (32 bytes): 4x4 hole, origins 0x10 / 0x20, name "MAP80       ".
     * Streams 1..10: minimal HAL-LZSS compressed streams (literal/RLE + 0xFF). */
    uint8_t hdr32[32] = {0};
    hdr32[0] = 0xff; hdr32[1] = 0xff;
    hdr32[2] = 4; hdr32[4] = 4; /* width=4, height=4 (16 cells) */
    hdr32[8] = 0x10; hdr32[10] = 0x20; hdr32[14] = 0x40;
    memcpy(hdr32 + 16, "MAP80       ", 12);

    uint8_t s1[3] = {(uint8_t)(0x20u | ((s1_len - 1u) & 0x1fu)), shape_val, 0xff};
    uint8_t s2_raw[36] = {0};
    s2_raw[0] = (uint8_t)((s2_len - 1u) & 0x1fu);
    if (tee_pos < s2_len) s2_raw[1u + tee_pos] = tee_val;
    if (tee_pos != 1u && 1u < s2_len) s2_raw[2] = 0x40;
    if (tee_pos != 2u && 2u < s2_len) s2_raw[3] = 0x41;
    s2_raw[1u + s2_len] = 0xff;
    uint8_t s3[3] = {(uint8_t)(0x20u | ((s3_len - 1u) & 0x1fu)), 0x02, 0xff};
    uint8_t s4[3] = {(uint8_t)(0x20u | ((s4_len - 1u) & 0x1fu)), 0x01, 0xff};
    uint8_t s5[4] = {0x01, 0x00, 0x00, 0xff};
    uint8_t s6[4] = {0x01, 0x04, 0x00, 0xff};
    uint8_t s7[4] = {0x01, 0x00, 0x00, 0xff};
    uint8_t s8[5] = {0x41, 0x34, 0x01, 0xff, 0}; /* 2 words of 0x0134 */
    uint8_t s9[5] = {0x41, 0x56, 0x02, 0xff, 0}; /* 2 words of 0x0256 */
    uint8_t s10[3] = {0x27, 0xff, 0xff}; /* 8 bytes of 0xFF */

    const uint8_t *blobs[11] = {hdr32, s1, s2_raw, s3, s4, s5, s6, s7, s8, s9, s10};
    const uint16_t lens[11] = {32, 3, (uint16_t)(s2_len + 2u), 3, 3, 4, 4, 4, 4, 4, 3};
    uint32_t ptrs[11];
    uint32_t cur = 0x140000u;
    for (unsigned t = 0; t < 11u; ++t) {
        ptrs[t] = 0xa88000u + (cur - 0x140000u);
        BufRec(&b, cur, blobs[t], lens[t]);
        cur += lens[t];
    }

    /* 11 pointer tables at 0x52670 + t*0x300 (152 * 3 bytes each). */
    uint8_t tbl[152 * 3];
    for (unsigned t = 0; t < 11u; ++t) {
        for (unsigned h = 0; h < 152u; ++h) {
            tbl[h * 3 + 0] = (uint8_t)(ptrs[t] & 0xffu);
            tbl[h * 3 + 1] = (uint8_t)((ptrs[t] >> 8) & 0xffu);
            tbl[h * 3 + 2] = (uint8_t)((ptrs[t] >> 16) & 0xffu);
        }
        BufRec(&b, 0x52670u + t * 0x300u, tbl, (uint16_t)sizeof(tbl));
    }

    /* 19 course names at 0x4652A (using GDC punctuation [, <, =). */
    uint8_t names[19 * 16];
    memset(names, ' ', sizeof(names));
    memcpy(names + 0 * 16, "Danny[s Woods   ", 16);
    memcpy(names + 1 * 16, "Barry < Kevin   ", 16);
    memcpy(names + 11 * 16, "Buckley B=List  ", 16);
    memcpy(names + 18 * 16, "Too Much Water  ", 16);
    BufRec(&b, 0x4652au, names, (uint16_t)sizeof(names));

    /* 19 palette pointers at 0x5425 (course 0 -> pal 9, course 1 -> pal 1). */
    uint8_t pal1[19 * 2];
    for (unsigned c = 0; c < 19u; ++c) {
        unsigned idx = (c == 0u) ? 9u : (c % 9u);
        unsigned v = 0xd4a9u + idx * 0x20u;
        pal1[c * 2] = (uint8_t)(v & 0xffu);
        pal1[c * 2 + 1] = (uint8_t)(v >> 8);
    }
    BufRec(&b, 0x5425u, pal1, (uint16_t)sizeof(pal1));

    /* 19 animated palette pointers at 0x204AF (course 0 -> anim 9). */
    uint8_t apal[19 * 2];
    for (unsigned c = 0; c < 19u; ++c) {
        unsigned idx = (c == 0u) ? 9u : (c % 9u);
        unsigned v = 0xa444u + idx * 0x120u;
        apal[c * 2] = (uint8_t)(v & 0xffu);
        apal[c * 2 + 1] = (uint8_t)(v >> 8);
    }
    BufRec(&b, 0x204afu, apal, (uint16_t)sizeof(apal));

    /* 152 music IDs at 0x7440. */
    uint8_t music[152];
    memset(music, 0x85, sizeof(music));
    music[0] = 0x9au; /* JP 0x9A -> USA 0x99 */
    music[1] = 0x8du; /* JP 0x8D -> USA 0x8C */
    music[2] = 0x7eu; /* JP 0x7E -> USA 0x7E */
    music[3] = 0x82u; /* JP 0x82 -> USA 0x82 */
    music[4] = 0x9fu; /* JP 0x9F -> USA 0x9E */
    BufRec(&b, 0x7440u, music, (uint16_t)sizeof(music));

    BufPut(&b, "EOF", 3);
    *out_size = b.len;
    return b.data;
}

static uint8_t *BuildSyntheticGdcIps(size_t *out_size, uint8_t shape_val, uint8_t tee_val)
{
    return BuildSyntheticGdcIpsEx(out_size, shape_val, tee_val, 16u, 16u, 0u, 16u, 16u);
}

static bool WriteFileBytes(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1u, size, f) == size;
    fclose(f);
    return ok;
}

static int TestHalDecompress(void)
{
    uint8_t out[128];
    size_t out_len = 0, consumed = 0;
    /* Raw(2: 0xAA, 0x55), RLE8(3: 0x77), RLE16(2: 0x12, 0x34), Inc(3: 0x10..0x12),
     * Backref(2 from 0: 0xAA, 0x55), BitRev(1 from 0: 0x55), Reverse(2 from 1: 0x55, 0xAA), EOF. */
    const uint8_t stream[] = {
        0x01, 0xaa, 0x55,
        0x22, 0x77,
        0x41, 0x12, 0x34,
        0x62, 0x10,
        0x81, 0x00, 0x00,
        0xa0, 0x00, 0x00,
        0xc1, 0x00, 0x01,
        0xff,
    };
    const uint8_t expected[] = {
        0xaa, 0x55, 0x77, 0x77, 0x77, 0x12, 0x34, 0x12, 0x34,
        0x10, 0x11, 0x12, 0xaa, 0x55, 0x55, 0x55, 0xaa,
    };
    CHECK(KdcHalDecompress(stream, sizeof(stream), out, sizeof(out), &out_len, &consumed));
    CHECK(out_len == sizeof(expected) && consumed == sizeof(stream));
    CHECK(memcmp(out, expected, sizeof(expected)) == 0);

    /* Reject truncated stream (no 0xFF) and out-of-bounds back-reference. */
    CHECK(!KdcHalDecompress(stream, sizeof(stream) - 1u, out, sizeof(out), NULL, NULL));
    const uint8_t bad_ref[] = {0x80, 0x00, 0x05, 0xff};
    CHECK(!KdcHalDecompress(bad_ref, sizeof(bad_ref), out, sizeof(out), NULL, NULL));
    return 0;
}

int main(int argc, char **argv)
{
    if (TestHalDecompress() != 0) return 1;

    KdcCoursePackSetRom(NULL, 0);
    KdcCoursePacksResetCatalog();
    CHECK(KdcCoursePackCount() == 2u && KdcCoursePackExternalCount() == 0u);
    CHECK(KdcCoursePackFindById("1p-normal") == 0);
    CHECK(KdcCoursePackFindById("1p-extra") == 1);
    CHECK(!KdcCoursePacksMenuEnabled());

    /* Reject malformed IPS inputs cleanly, including wrong plane lengths and
     * tees located outside the declared width x height grid. */
    char err[160] = {0};
    CHECK(!KdcCoursePackLoadIpsMemory((const uint8_t *)"NOPE", 4, "bad", err, sizeof(err)));
    size_t bad_sz = 0;
    uint8_t *bad_shape = BuildSyntheticGdcIps(&bad_sz, 28u, 0x0cu);
    CHECK(!KdcCoursePackLoadIpsMemory(bad_shape, bad_sz, "bad-shape", err, sizeof(err)));
    free(bad_shape);
    uint8_t *no_tee = BuildSyntheticGdcIps(&bad_sz, 1u, 0x00u);
    CHECK(!KdcCoursePackLoadIpsMemory(no_tee, bad_sz, "no-tee", err, sizeof(err)));
    free(no_tee);
    uint8_t *short_shape = BuildSyntheticGdcIpsEx(&bad_sz, 1u, 0x0cu, 1u, 16u, 0u, 16u, 16u);
    CHECK(!KdcCoursePackLoadIpsMemory(short_shape, bad_sz, "short-shape", err, sizeof(err)));
    free(short_shape);
    uint8_t *tee_oob = BuildSyntheticGdcIpsEx(&bad_sz, 1u, 0x0cu, 16u, 17u, 16u, 16u, 16u);
    CHECK(!KdcCoursePackLoadIpsMemory(tee_oob, bad_sz, "tee-oob", err, sizeof(err)));
    free(tee_oob);
    uint8_t *short_elev = BuildSyntheticGdcIpsEx(&bad_sz, 1u, 0x0cu, 16u, 16u, 0u, 15u, 16u);
    CHECK(!KdcCoursePackLoadIpsMemory(short_elev, bad_sz, "short-elev", err, sizeof(err)));
    free(short_elev);
    uint8_t *short_wall = BuildSyntheticGdcIpsEx(&bad_sz, 1u, 0x0cu, 16u, 16u, 0u, 16u, 15u);
    CHECK(!KdcCoursePackLoadIpsMemory(short_wall, bad_sz, "short-wall", err, sizeof(err)));
    free(short_wall);

    /* Directory scan: rejected files must not consume catalog slots. */
    if (argc >= 2 && argv[1][0]) {
        for (unsigned i = 0; i < 20u; ++i) {
            char p[512];
            snprintf(p, sizeof(p), "%s/%02u_rejected.ips", argv[1], i);
            CHECK(WriteFileBytes(p, (const uint8_t *)"PATCHEOF", 8u));
        }
        size_t valid_sz = 0;
        uint8_t *valid_ips = BuildSyntheticGdcIps(&valid_sz, 5u, 0x0cu);
        char vp[512];
        snprintf(vp, sizeof(vp), "%s/zz_valid.ips", argv[1]);
        CHECK(WriteFileBytes(vp, valid_ips, valid_sz));
        free(valid_ips);

        KdcCoursePacksSetDropinDir(argv[1]);
        KdcCoursePacksEnsureLoaded();
        CHECK(KdcCoursePackCount() == 3u && KdcCoursePackExternalCount() == 1u);
        CHECK(KdcCoursePackFindById("gdc") == 2);

        KdcCoursePacksResetCatalog();
        CHECK(KdcCoursePackCount() == 2u && KdcCoursePackExternalCount() == 0u);
    }

    /* Load valid synthetic Grump's Dream Course IPS. */
    size_t ips_sz = 0;
    uint8_t *ips = BuildSyntheticGdcIps(&ips_sz, 5u, 0x0cu);
    CHECK(KdcCoursePackLoadIpsMemory(ips, ips_sz, "synthetic.ips", err, sizeof(err)));
    free(ips);

    CHECK(KdcCoursePackCount() == 3u && KdcCoursePackExternalCount() == 1u);
    int gdc_idx = KdcCoursePackFindById("gdc");
    CHECK(gdc_idx == 2);
    const KdcCoursePack *gdc = KdcCoursePackGet((unsigned)gdc_idx);
    CHECK(gdc && gdc->external && gdc->course_count == 19u);
    CHECK(!strcmp(gdc->courses[0].name, "DANNY'S WOODS"));
    CHECK(!strcmp(gdc->courses[1].name, "BARRY & KEVIN"));
    CHECK(!strcmp(gdc->courses[11].name, "BUCKLEY B-LIST"));
    CHECK(!strcmp(gdc->courses[18].name, "TOO MUCH WATER"));
    CHECK(gdc->courses[0].palette == 9u && gdc->courses[0].anim_palette == 9u);
    CHECK(gdc->courses[0].holes[0].music_id == 0x99u);
    CHECK(gdc->courses[0].holes[1].music_id == 0x8cu);
    CHECK(gdc->courses[0].holes[2].music_id == 0x7eu);
    CHECK(gdc->courses[0].holes[3].music_id == 0x82u);
    CHECK(gdc->courses[0].holes[4].music_id == 0x9eu);

    /* Runtime injection hooks: silent when inactive. */
    KdcCoursePackInitialize();
    uint8_t ram[0x20000] = {0};
    CpuState cpu = {0};
    cpu.ram = ram;
    Fire(&cpu, 0x82b1cau);
    CHECK(redirect == 0 && ram[0x8b92] == 0);

    /* Built-in 1P Extra Course 7 (ROM course 14, map 112..119, palette 8) in 2P. */
    CHECK(KdcCoursePackSelect(1u, 6u));
    ram[0x6a5eu] = 3u; /* hole 4 -> map 115 -> (115 & 0x3F) = 51 */
    ram[0x17ffu] = 1u;
    Fire(&cpu, 0x80c168u);
    CHECK(Word(ram, 0xabb2u) == 115u && Word(ram, 0xabb4u) == 230u && Word(ram, 0xabb6u) == 345u);
    Fire(&cpu, 0x82b1cau);
    CHECK(redirect == 0); /* Built-in pack lets the USA loader run natively. */
    /* Built-in music reads $80:C9D5 from the verified runtime ROM (no-op before ROM load). */
    Fire(&cpu, 0x80c988u);
    CHECK(redirect == 0);
    uint8_t fake_rom[0x5000] = {0};
    for (unsigned i = 0; i < 64u; ++i) fake_rom[0x49d5u + i] = (uint8_t)(0x40u + i);
    KdcCoursePackSetRom(fake_rom, sizeof(fake_rom));
    Fire(&cpu, 0x80c988u);
    CHECK(redirect == 0x80c999u && cpu.A == (uint16_t)(0x40u + 51u));
    Fire(&cpu, 0x80d843u);
    CHECK(redirect == 0x80d84cu && cpu.A == 0xd8edu + 8u * 0x20u);
    Fire(&cpu, 0x86fc95u);
    CHECK(redirect == 0x86fcffu && Word(ram, 0xecebu) == 3u);

    /* External GDC pack course 0 (Danny's Woods, pal 9, theme 0, hole 0). */
    uint32_t gen_before = KdcCoursePackGeneration();
    CHECK(KdcCoursePackSelect((unsigned)gdc_idx, 0u));
    ram[0x6a5eu] = 0u;
    Fire(&cpu, 0x82b1cau);
    CHECK(redirect == 0x82b270u);
    CHECK(KdcCoursePackGeneration() > gen_before);
    CHECK(Word(ram, 0x8b92u) == 4u && Word(ram, 0x8b94u) == 4u);
    CHECK(Word(ram, 0x8b98u) == 0x80u && Word(ram, 0x8b9au) == 0x100u && Word(ram, 0x8b9cu) == 0u);
    CHECK(ram[0x6b90u] == 5u && ram[0x8390u] == 0x0cu && ram[0x7390u] == 2u && ram[0x7b90u] == 1u);
    CHECK(ram[0xa3b2u] == 0xffu);

    Fire(&cpu, 0x82b14bu);
    CHECK(redirect == 0x82b1c9u);
    CHECK(Word(ram, 0x9bb0u) == 4u);
    CHECK(Word(ram, 0x13000u) == 0x0134u && Word(ram, 0x19800u) == 0x0256u);

    Fire(&cpu, 0x80c988u);
    CHECK(redirect == 0x80c999u && cpu.A == 0x99u);
    Fire(&cpu, 0x80d843u);
    CHECK(redirect == 0x80d84cu && cpu.A == 0xda0du);
    Fire(&cpu, 0x80d85bu);
    CHECK(redirect == 0x80d864u && cpu.A == 0xdb4du);
    cpu.Y = 4u;
    Fire(&cpu, 0x8482b2u);
    CHECK(redirect == 0x8482c3u && Word(ram, 0x5420u) == 0xea50u && Word(ram, 0x54a0u) == 0xea7au);
    cpu.A = 5u;
    Fire(&cpu, 0x80d6e8u);
    CHECK(redirect == 0 && cpu.A == 0u);

    KdcCoursePackClearActive();
    CHECK(!KdcCoursePackHasActive());
    return 0;
}
