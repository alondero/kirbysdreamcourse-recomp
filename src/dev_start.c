#include "dev_start.h"
#include "course_pack.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned course, hole, map, pack, pack_course;
    int enabled, raw, multiplayer, extra, has_pack;
} DevStart;

static DevStart target;
static int pending;

static void Help(void)
{
    puts("Developer hole start (course/hole numbers are 1-based):\n"
         "  --dev-hole normal:COURSE:HOLE         Courses 1-8, holes 1-8\n"
         "  --dev-hole extra:COURSE:HOLE          Extra courses 1-8\n"
         "  --dev-hole multi:COURSE:HOLE          Two-player courses 1-4\n"
         "  --dev-hole multi-extra:COURSE:HOLE    Extra two-player courses 1-4\n"
         "  --dev-hole pack:ID:COURSE:HOLE        Course pack hole (1P context)\n"
         "  --dev-hole pack-2p:ID:COURSE:HOLE     Course pack hole (2P context)\n"
         "  --dev-hole demo:NUMBER                Attract-mode holes 1-4 (playable)\n"
         "  --dev-hole map:ID                     Raw ROM map slot 0-255\n"
         "  --dev-two-player                      Two players with map:ID\n"
         "  --course-pack PATH                    Load an external IPS course pack\n"
         "  --course-pack-dir PATH                Override drop-in courses/ directory\n"
         "  --dev-hole-list                       Show this help\n"
         "Raw slots include demo/unused data and aliases; some may be incomplete.\n"
         "Use an isolated saves directory for developer sessions.");
}

static int Number(const char **text, unsigned *value)
{
    const char *p = *text;
    if (*p < '0' || *p > '9') return 0;
    unsigned n = 0;
    do {
        n = n * 10 + (unsigned)(*p++ - '0');
        if (n > 255) return 0;
    } while (*p >= '0' && *p <= '9');
    *text = p;
    *value = n;
    return 1;
}

static int ParseTarget(const char *text, DevStart *out)
{
    DevStart result = {.enabled = 1};
    const char *numbers = strchr(text, ':');
    if (!numbers) return 0;
    size_t length = (size_t)(numbers++ - text);
    if (length == 3 && !strncmp(text, "map", length)) {
        if (!Number(&numbers, &result.map) || *numbers) return 0;
        result.raw = 1;
    } else if (length == 4 && !strncmp(text, "demo", length)) {
        /* Course/hole pairs selected by $83DB10's four attract records. */
        static const unsigned maps[] = {0, 42, 44, 77};
        unsigned demo;
        if (!Number(&numbers, &demo) || *numbers || demo < 1 || demo > 4) return 0;
        result.map = maps[demo - 1];
        result.course = result.map / 8;
        result.hole = result.map % 8;
        result.extra = result.course >= 8;
    } else if ((length == 4 && !strncmp(text, "pack", length)) ||
               (length == 7 && !strncmp(text, "pack-2p", length))) {
        const char *sep = strchr(numbers, ':');
        if (!sep || sep == numbers || (size_t)(sep - numbers) >= 24u) return 0;
        char pack_id[24];
        memcpy(pack_id, numbers, (size_t)(sep - numbers));
        pack_id[sep - numbers] = '\0';
        KdcCoursePacksEnsureLoaded();
        int pidx = KdcCoursePackFindById(pack_id);
        if (pidx < 0) return 0;
        const KdcCoursePack *pack = KdcCoursePackGet((unsigned)pidx);
        if (!pack || !pack->course_count) return 0;
        numbers = sep + 1;
        unsigned course, hole;
        if (!Number(&numbers, &course) || *numbers++ != ':' ||
            !Number(&numbers, &hole) || *numbers || course < 1 ||
            course > pack->course_count || hole < 1 || hole > 8)
            return 0;
        result.has_pack = 1;
        result.pack = (unsigned)pidx;
        result.pack_course = course - 1;
        result.multiplayer = length == 7;
        result.course = result.multiplayer ? 16u : 0u;
        result.hole = hole - 1;
        result.map = result.course * 8 + result.hole;
    } else {
        if (length == 6 && !strncmp(text, "normal", length)) { }
        else if (length == 5 && !strncmp(text, "extra", length)) result.extra = 1;
        else if (length == 5 && !strncmp(text, "multi", length)) result.multiplayer = 1;
        else if (length == 11 && !strncmp(text, "multi-extra", length))
            result.multiplayer = result.extra = 1;
        else return 0;
        unsigned course, hole;
        if (!Number(&numbers, &course) || *numbers++ != ':' ||
            !Number(&numbers, &hole) || *numbers || course < 1 ||
            course > (result.multiplayer ? 4u : 8u) || hole < 1 || hole > 8)
            return 0;
        result.course = course - 1 + (result.multiplayer ? 16 : 0) +
                        (result.extra ? 8 : 0);
        result.hole = hole - 1;
        result.map = result.course * 8 + result.hole;
    }
    *out = result;
    return 1;
}

int KdcDevParseArgs(int *argc, char **argv)
{
    int write = 1, two_player = 0;
    target = (DevStart){0};
    /* Pass 1: apply any drop-in directory override and load explicit
     * --course-pack arguments first so --dev-hole pack:... can reference them
     * regardless of flag ordering. */
    for (int read = 1; read < *argc; ++read) {
        if (!strcmp(argv[read], "--course-pack-dir")) {
            if (++read == *argc || !argv[read][0] ||
                !KdcCoursePacksSetDropinDir(argv[read])) {
                fputs("Could not resolve --course-pack-dir path.\n", stderr);
                goto invalid;
            }
        } else if (!strcmp(argv[read], "--course-pack")) {
            if (++read == *argc || !KdcCoursePacksAddCliPath(argv[read]))
                goto invalid;
        }
    }
    /* Pass 2: parse remaining developer flags and compact argv. */
    for (int read = 1; read < *argc; ++read) {
        if (!strcmp(argv[read], "--dev-hole-list")) { Help(); return 1; }
        if (!strcmp(argv[read], "--course-pack") ||
            !strcmp(argv[read], "--course-pack-dir")) {
            ++read; /* already handled in pass 1 */
        } else if (!strcmp(argv[read], "--dev-two-player")) {
            if (two_player) goto invalid;
            two_player = 1;
        } else if (!strcmp(argv[read], "--dev-hole")) {
            if (target.enabled || ++read == *argc || !ParseTarget(argv[read], &target))
                goto invalid;
        } else {
            argv[write++] = argv[read];
        }
    }
    if (two_player && (!target.enabled || !target.raw)) goto invalid;
    if (two_player) {
        target.multiplayer = 1;
        target.course = 16;
    }
    argv[write] = NULL;
    *argc = write;
    pending = target.enabled;
    return 0;
invalid:
    fputs("Invalid developer hole selection. Use --dev-hole-list for syntax.\n", stderr);
    return -1;
}

static void Word(CpuState *cpu, unsigned address, unsigned value)
{
    cpu->ram[address] = (uint8_t)value;
    cpu->ram[address + 1] = (uint8_t)(value >> 8);
}

static void Observe(CpuState *cpu, uint32_t pc)
{
    if (!pending) return;
    switch (pc & 0x7fffff) {
    case 0x00f0ff:
        /* Reset, SRAM validation and hardware/APU initialization have run.
         * Enter new-game initialization on the same main-loop stack. */
        if (target.has_pack)
            (void)KdcCoursePackSelect(target.pack, target.pack_course);
        Word(cpu, 0x17ff, target.multiplayer);
        Word(cpu, 0xeab9, target.extra);
        interp_bridge_pre_opcode_redirect(0x80f1db);
        break;
    case 0x00f1df:
        /* C036 has initialized player health, lives and scores. Skip file,
         * name and course menus and the preview, retaining the hole loader. */
        Word(cpu, 0x6a5a, target.course);
        Word(cpu, 0x6a5c, target.course * 2);
        Word(cpu, 0x6a5e, target.hole);
        Word(cpu, 0x6a60, target.hole * 2);
        interp_bridge_pre_opcode_redirect(0x80f256);
        break;
    case 0x00c168:
        /* B0A4 has computed the map-table indices. Raw slots keep a valid
         * single/multiplayer course context for HUD and scoring tables. */
        if (target.raw) {
            Word(cpu, 0xabb2, target.map);
            Word(cpu, 0xabb4, target.map * 2);
            Word(cpu, 0xabb6, target.map * 3);
        }
        if (target.has_pack) {
            const KdcCoursePack *pack = KdcCoursePackGet(target.pack);
            fprintf(stderr, "Developer start: pack %s course %u, %s, hole %u\n",
                    pack ? pack->id : "?", target.pack_course + 1u,
                    target.multiplayer ? "two players" : "one player",
                    target.hole + 1u);
        } else {
            fprintf(stderr, "Developer start: map %u, %s, hole %u\n",
                    target.map, target.multiplayer ? "two players" : "one player",
                    target.hole + 1);
        }
        pending = 0;
        break;
    }
}

void KdcDevInitialize(void)
{
    static const uint32_t sites[] = {0x80f0ff, 0x80f1df, 0x80c168};
    for (unsigned i = 0; i < sizeof(sites) / sizeof(sites[0]); ++i) {
        if (!interp_bridge_add_pre_opcode_hook(sites[i], Observe)) {
            fputs("Unable to register developer hole start\n", stderr);
            abort();
        }
    }
}

void KdcDevReset(void) { pending = target.enabled; }
void KdcDevStateLoaded(void) { pending = 0; }

int KdcDevSelect(const char *selector)
{
    DevStart next = {0};
    if (selector && !ParseTarget(selector, &next)) return 0;
    if (target.has_pack && !next.has_pack) KdcCoursePackClearActive();
    target = next;
    pending = target.enabled;
    return 1;
}

int KdcDevEnabled(void) { return target.enabled; }
int KdcDevUsesCoursePack(void) { return target.enabled && target.has_pack; }
