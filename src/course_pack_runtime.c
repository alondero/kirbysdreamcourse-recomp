/* USA runtime injection hooks for extra 2P / 1P-vs-CPU / Practice courses.
 * Disabled completely when no custom course is active. Hardware behavior,
 * generated code and the interpreter fallback remain unchanged. */
#include "course_pack.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned ReadWord(const uint8_t *b, size_t off)
{
    return (unsigned)b[off] | ((unsigned)b[off + 1] << 8);
}

static void WriteWord(uint8_t *b, size_t off, unsigned value)
{
    b[off] = (uint8_t)(value & 0xffu);
    b[off + 1] = (uint8_t)((value >> 8) & 0xffu);
}

static bool LoadExternalStream(const KdcCoursePack *pack, const KdcPackHole *hole,
                               unsigned stream, uint8_t *dst, size_t dst_cap)
{
    uint32_t off = hole->stream_offset[stream];
    uint32_t sz = hole->stream_size[stream];
    if (off >= pack->storage_size || sz > pack->storage_size - off) return false;
    return KdcHalDecompress(pack->storage + off, sz, dst, dst_cap, NULL, NULL);
}

static void Observe(CpuState *cpu, uint32_t pc)
{
    if (!KdcCoursePackHasActive()) return;
    const KdcCoursePack *pack = KdcCoursePackActivePack();
    const KdcPackCourse *course = KdcCoursePackActiveCourse();
    if (!pack || !course) return;
    uint8_t *ram = cpu->ram;
    unsigned hole_idx = ReadWord(ram, 0x6a5eu) & 7u;
    const KdcPackHole *hole = &course->holes[hole_idx];

    switch (pc & 0x7fffffu) {
    case 0x00c168u:
        /* B0A4 just computed $ABB2/$ABB4/$ABB6 from proxy $6A5A. For built-in
         * USA 1P courses in 2P, redirect the map index directly to the USA
         * 1P map slot so the original loader reads all 11 streams natively. */
        if (!hole->external) {
            unsigned m = hole->rom_map;
            WriteWord(ram, 0xabb2u, m);
            WriteWord(ram, 0xabb4u, m * 2u);
            WriteWord(ram, 0xabb6u, m * 3u);
            KdcCoursePackBumpGeneration();
        }
        break;
    case 0x02b1cau:
        /* Gameplay map loader ($82:B1CA -> RTL at $82:B270). */
        if (!hole->external) return;
        memset(ram + 0x6b90u, 0, 0x2001u);
        if (hole->stream_offset[0] + 32u <= pack->storage_size) {
            memcpy(ram + 0x8b90u, pack->storage + hole->stream_offset[0], 32u);
        }
        WriteWord(ram, 0x8b9cu, 0u);
        WriteWord(ram, 0x8b98u, (ReadWord(ram, 0x8b98u) << 3) & 0xffffu);
        WriteWord(ram, 0x8b9au, (ReadWord(ram, 0x8b9au) << 3) & 0xffffu);
        (void)LoadExternalStream(pack, hole, 1u, ram + 0x6b90u, 0x800u);
        (void)LoadExternalStream(pack, hole, 2u, ram + 0x8390u, 0x800u);
        (void)LoadExternalStream(pack, hole, 3u, ram + 0x7390u, 0x800u);
        (void)LoadExternalStream(pack, hole, 4u, ram + 0x7b90u, 0x800u);
        (void)LoadExternalStream(pack, hole, 10u, ram + 0xa3b2u, 0x800u);
        KdcCoursePackBumpGeneration();
        interp_bridge_pre_opcode_redirect(0x82b270u);
        break;
    case 0x02b14bu:
        /* Visual tilemap loader ($82:B14B -> RTL at $82:B1C9). */
        if (!hole->external) return;
        memset(ram + 0x8bb0u, 0, 0x17ffu);
        (void)LoadExternalStream(pack, hole, 5u, ram + 0x93b0u, 0x800u);
        (void)LoadExternalStream(pack, hole, 6u, ram + 0x9bb0u, 0x800u);
        (void)LoadExternalStream(pack, hole, 7u, ram + 0x8bb0u, 0x800u);
        (void)LoadExternalStream(pack, hole, 8u, ram + 0x13000u, 0x6800u);
        (void)LoadExternalStream(pack, hole, 9u, ram + 0x19800u, 0x6800u);
        interp_bridge_pre_opcode_redirect(0x82b1c9u);
        break;
    case 0x00c988u:
        /* Per-hole music loader ($80:C988 -> JML $80B4AC at $80:C999). */
        cpu->A = hole->music_id;
        interp_bridge_pre_opcode_redirect(0x80c999u);
        break;
    case 0x00d843u:
        /* Course palette 0..9 ($80:D8ED + pal * 0x20 -> JSL $8090E7). */
        cpu->A = (uint16_t)(0xd8edu + (unsigned)course->palette * 0x20u);
        interp_bridge_pre_opcode_redirect(0x80d84cu);
        break;
    case 0x00d85bu:
        /* Course alt/night palette 0..9 ($80:DA2D + pal * 0x20 -> JSL $8090E7). */
        cpu->A = (uint16_t)(0xda2du + (unsigned)course->palette * 0x20u);
        interp_bridge_pre_opcode_redirect(0x80d864u);
        break;
    case 0x0482b2u:
        /* Course animated palette scripts ($84:E030 / $84:E05A + anim * 0x120). */
        if (cpu->Y < 128u) {
            WriteWord(ram, 0x541cu + cpu->Y, 0xe030u + (unsigned)course->anim_palette * 0x120u);
            WriteWord(ram, 0x549cu + cpu->Y, 0xe05au + (unsigned)course->anim_palette * 0x120u);
        }
        interp_bridge_pre_opcode_redirect(0x8482c3u);
        break;
    case 0x00d6e8u:
    case 0x00d703u:
    case 0x00d718u:
    case 0x00d733u:
    case 0x00d4fdu:
    case 0x04cd38u:
        /* Scenery CHR/MAP, color ramp, and weather task all compute (course & 7)
         * into A right before these sites. Substitute the custom course theme. */
        cpu->A = (uint16_t)(course->theme & 7u);
        break;
    case 0x06fc95u:
        /* Skip the hardcoded vanilla course intro banner in custom 2P matches. */
        if (ReadWord(ram, 0x17ffu) == 1u) {
            WriteWord(ram, 0xecebu, 3u);
            WriteWord(ram, 0xecedu, 0u);
            WriteWord(ram, 0xece9u, 0u);
            interp_bridge_pre_opcode_redirect(0x86fcffu);
        }
        break;
    }
}

void KdcCoursePackInitialize(void)
{
    static const uint32_t sites[] = {
        0x80c168u, 0x82b1cau, 0x82b14bu,
        0x80c988u, 0x80d843u, 0x80d85bu, 0x8482b2u,
        0x80d6e8u, 0x80d703u, 0x80d718u, 0x80d733u, 0x80d4fdu, 0x84cd38u,
        0x86fc95u,
    };
    KdcCoursePacksEnsureLoaded();
    for (unsigned i = 0; i < sizeof(sites) / sizeof(sites[0]); ++i) {
        if (!interp_bridge_add_pre_opcode_hook(sites[i], Observe)) {
            fputs("Unable to register course pack hooks\n", stderr);
            abort();
        }
    }
}
