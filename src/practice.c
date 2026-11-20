/* USA title-menu extension. Hardware and the hole loader remain framework/
 * guest owned; see docs/practice.md and docs/cpu-opponent.md for the
 * observed entry points. */
#include "practice.h"
#include "course_pack.h"
#include "dev_start.h"
#include "cpu_opponent.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "snes_overlay_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static KdcPracticeView view;
static uint32_t previous, swallowed;
static unsigned repeat;
static uint8_t *ordinary_sram;
static int ordinary_size;
static char ordinary_root[96];
static bool ability_pending, picker_reset, choosing, in_hole;
static bool custom_2p_boot, custom_2p_committed;
static const char *const modes[] = {"normal", "extra", "multi", "multi-extra"};
/* Root-menu and 2P-submenu return values this extension adds. */
enum {
    RESULT_TWO_PLAYER = 1,
    RESULT_MORE_COURSES = 2,
    RESULT_PRACTICE = 6,
    RESULT_CPU = 7,
    RESULT_EASY = 8,
};

const char *KdcPracticeAbilityName(unsigned ability)
{
    /* Guest ability IDs, shared by D852/D854 and the conditions portraits
     * at 849E07. Keep labels in this order rather than a separate UI order. */
    static const char *const names[KDC_PRACTICE_ABILITIES] = {
        "NONE", "STONE", "PARASOL", "FIREBALL", "NEEDLE", "TORNADO",
        "WHEEL", "SPARK", "HI-JUMP", "FREEZE", "UFO"
    };
    return ability < KDC_PRACTICE_ABILITIES ? names[ability] : "NONE";
}

unsigned KdcPracticeGuestGlyph(char c)
{
    /* The title-menu font sheet the picker decodes at runtime (BG3,
     * character base 5, VRAM word 0x5000) is not ASCII-ordered. Reading
     * it out of the live title VRAM gives: digits at 16..25, the hyphen
     * at 29, the space at 31, '<' at 10, '>' at 12 and letters at
     * 33..58. ASCII-32 happens to match the digits and the letters, but
     * it points at the question mark for the hyphen, at '+' for '<' and
     * at a solid block for '>', so the sheet is addressed by its own
     * indices. Tiles 0, 5, 8, 26, 30, 32 and 59..63 hold blanks, blocks
     * and icons this table does not name. */
    switch (c) {
    case ' ': return 31u;
    case '!': return 9u;
    case '#': return 1u;
    case '%': return 2u;
    case '(': return 3u;
    case ')': return 4u;
    case '/': return 6u;
    case ':': return 7u;
    case '<': return 10u;
    case '=': return 11u;
    case '>': return 12u;
    case '?': return 13u;
    case '[': return 14u;
    case ']': return 15u;
    case '+': return 28u;
    case '-': return 29u;
    default: break;
    }
    if (c >= '0' && c <= '9') return (unsigned)(c - '0') + 16u;
    if (c >= 'A' && c <= 'Z') return (unsigned)(c - 'A') + 33u;
    return KDC_PRACTICE_NO_GLYPH;
}

static const KdcCoursePack *PracticeExternalPack(int mode, bool *two_player)
{
    if (mode < 4 || !KdcCoursePacksMenuEnabled()) return NULL;
    unsigned target = (unsigned)(mode - 4) / 2u;
    if (two_player) *two_player = ((mode - 4) & 1) != 0;
    unsigned seen = 0u;
    unsigned total = KdcCoursePackCount();
    for (unsigned i = 0; i < total; ++i) {
        const KdcCoursePack *p = KdcCoursePackGet(i);
        if (p && p->external) {
            if (seen == target) return p;
            ++seen;
        }
    }
    return NULL;
}

static int PracticeModeCount(void)
{
    if (!KdcCoursePacksMenuEnabled()) return 4;
    return 4 + 2 * (int)KdcCoursePackExternalCount();
}

static int PracticeCourseCount(int mode)
{
    if (mode < 2) return 8;
    if (mode < 4) return 4;
    const KdcCoursePack *p = PracticeExternalPack(mode, NULL);
    return p && p->course_count ? (int)p->course_count : 1;
}

static void ClampPracticeSelection(void)
{
    int mcount = PracticeModeCount();
    if (view.mode < 0 || view.mode >= mcount) view.mode = 0;
    int ccount = PracticeCourseCount(view.mode);
    if (view.course < 0) view.course = 0;
    if (view.course >= ccount) view.course = ccount - 1;
    if (view.hole < 0 || view.hole >= 8) view.hole = 0;
}

static void ClampPackSelection(void)
{
    int pcount = (int)KdcCoursePackCount();
    if (pcount <= 0) { view.pack = view.pack_course = 0; return; }
    if (view.pack < 0 || view.pack >= pcount) view.pack = 0;
    const KdcCoursePack *p = KdcCoursePackGet((unsigned)view.pack);
    int ccount = p && p->course_count ? (int)p->course_count : 1;
    if (view.pack_course < 0) view.pack_course = 0;
    if (view.pack_course >= ccount) view.pack_course = ccount - 1;
}

static bool FormatPracticeSelector(char *out, size_t out_size)
{
    ClampPracticeSelection();
    if (view.mode < 4) {
        snprintf(out, out_size, "%s:%d:%d", modes[view.mode],
                 view.course + 1, view.hole + 1);
        return true;
    }
    bool two_player = false;
    const KdcCoursePack *p = PracticeExternalPack(view.mode, &two_player);
    if (!p) return false;
    snprintf(out, out_size, "%s:%s:%d:%d", two_player ? "pack-2p" : "pack",
             p->id, view.course + 1, view.hole + 1);
    return true;
}

const KdcPracticeView *KdcPracticeCurrentView(void) { return &view; }
bool KdcPracticeVisible(void)
{
    return view.screen || (view.active && !view.cpu && !view.custom_match);
}
bool KdcPracticeSnapshotsAllowed(void)
{
    /* The CPU's private planning snapshot is restored within the same frame.
     * Keep custom course packs (including --dev-hole pack:*) out of ordinary
     * save-state slots and rewind buffers. */
    return KdcCpuPlanning() ||
           (!view.screen && !view.active && !KdcCoursePackHasActive() &&
            !KdcDevUsesCoursePack());
}

static unsigned Word(const uint8_t *ram, unsigned a)
{
    return ram[a] | ((unsigned)ram[a + 1] << 8);
}

static void WriteWord(uint8_t *ram, unsigned a, unsigned value)
{
    ram[a] = (uint8_t)(value & 0xffu);
    ram[a + 1] = (uint8_t)((value >> 8) & 0xffu);
}

/* Write a label into the root menu's tilemap, which the guest uploads right
 * after BBD8. Missing glyphs become spaces. */
static void Label(uint8_t *ram, unsigned x, unsigned y, const char *text, unsigned width)
{
    size_t length = strlen(text);
    for (unsigned i = 0; i < width && x + i < 32; ++i) {
        unsigned glyph = i < length ? KdcPracticeGuestGlyph(text[i]) : 31u;
        unsigned at = 0x39da + 2 * (y * 32 + x + i);
        ram[at] = (uint8_t)(glyph == KDC_PRACTICE_NO_GLYPH ? 31u : glyph);
        ram[at + 1] = 0x20;
    }
}

/* Swap the battery image so the session's own writes land in `root`. The
 * session starts from a copy of the ordinary save, so the game sees the same
 * unlocks; the ordinary image is restored, byte for byte, when it ends. */
static bool BeginSession(const char *root, bool fresh)
{
    if (!g_sram || g_sram_size <= 0 || strcmp(RtlSaveRoot(), "saves")) {
        view.error = "SAVE SESSION UNAVAILABLE";
        return false;
    }
    uint8_t *copy = malloc((size_t)g_sram_size);
    if (!copy) { view.error = "NOT ENOUGH MEMORY"; return false; }
    memcpy(copy, g_sram, (size_t)g_sram_size);
    if (!RtlWriteSram()) {
        free(copy);
        view.error = "COULD NOT SAVE PROGRESS";
        return false;
    }
    ordinary_sram = copy;
    ordinary_size = g_sram_size;
    snprintf(ordinary_root, sizeof(ordinary_root), "%s", RtlSaveRoot());
    RtlSetSaveRoot(root);
    RtlSetQuickSaveRoot(NULL);
    if (fresh) {
        memset(g_sram, 0, (size_t)g_sram_size);
        RtlReadSram();
    }
    view.active = true;
    return true;
}

/* `force` gives up the session's own unsaved writes rather than stay in it. */
static bool EndSession(bool force)
{
    if (!RtlWriteSram()) {
        view.error = "COULD NOT SAVE SESSION";
        if (!force) return false;
    }
    /* SRAM shape cannot change while the supported ROM is running. */
    if (g_sram_size != ordinary_size || !ordinary_sram) {
        view.error = "SAVE SESSION UNAVAILABLE";
        return false;
    }
    memcpy(g_sram, ordinary_sram, (size_t)ordinary_size);
    free(ordinary_sram);
    ordinary_sram = NULL;
    RtlSetSaveRoot(ordinary_root);
    KdcDevSelect(NULL);
    KdcCoursePackClearActive();
    view.active = false;
    view.cpu = 0;
    view.custom_match = false;
    custom_2p_boot = false;
    custom_2p_committed = false;
    in_hole = false;
    KdcCpuSelect(0);
    return true;
}

/* BBD8 runs after the root menu's entries are laid out and before the
 * tilemap upload. Either append this extension's entries, or (after 1P VS
 * CPU) turn the same menu into the difficulty choice. */
static void RootMenu(CpuState *cpu, uint8_t *ram)
{
    unsigned count_address = (cpu->D + 0x4a) & 0xffff;
    unsigned n = Word(ram, count_address);
    if (n < 3 || n > 6) return;
    unsigned x = ram[0xea8e], y = ram[0xea97 + n - 1] + 2;
    if (x > 23 || y > 21) return;
    if (choosing) {
        static const char *const names[] = {"EASY", "MEDIUM", "HARD"};
        for (unsigned i = 0; i < n; ++i)
            Label(ram, ram[0xea8e + i], ram[0xea97 + i], i < 3 ? names[i] : "", 12);
        for (unsigned i = 0; i < 3; ++i) ram[0xeaa0 + i] = (uint8_t)(RESULT_EASY + i);
        ram[count_address] = 3;
        ram[0xea8a] = 1; /* Let B cancel, as the guest's submenus do. */
        return;
    }
    if ((view.cpu || view.custom_match) && !view.title_background) {
        /* Back at the title menu from a CPU or custom 2P match. */
        bool was_cpu = view.cpu != 0;
        if (view.active && !EndSession(true))
            fprintf(stderr, "%s: %s\n", was_cpu ? "CPU match" : "Custom 2P", view.error);
        view.cpu = 0;
        view.custom_match = false;
        view.error = NULL;
        KdcCoursePackClearActive();
        KdcCpuSelect(0);
        fputs(was_cpu ? "CPU match: returned to title menu\n"
                      : "Custom 2P: returned to title menu\n", stderr);
    }
    ram[0xea8e + n] = (uint8_t)x;
    ram[0xea97 + n] = (uint8_t)y;
    ram[0xeaa0 + n] = RESULT_PRACTICE;
    Label(ram, x, y, "PRACTICE", 8);
    ram[0xea8e + n + 1] = (uint8_t)x;
    ram[0xea97 + n + 1] = (uint8_t)(y + 2);
    ram[0xeaa0 + n + 1] = RESULT_CPU;
    Label(ram, x, y + 2, "1P VS CPU", 9);
    ram[count_address] = (uint8_t)(n + 2);
    view.title = true;
}

/* Append MORE COURSES to the 2P submenu ($83:B824) when extra courses are
 * enabled. PLAY COURSE (0) and EXTRA COURSE (1) remain unchanged. */
static void TwoPlayerSubmenu(CpuState *cpu, uint8_t *ram)
{
    if (!KdcCoursePacksMenuEnabled() || !KdcCoursePackCount()) return;
    unsigned count_address = (cpu->D + 0x4a) & 0xffff;
    unsigned n = Word(ram, count_address);
    if (n < 1 || n > 4) return;
    unsigned x = ram[0xea8e], y = ram[0xea97 + n - 1] + 2;
    if (x > 20 || y > 21) return;
    ram[0xea8e + n] = (uint8_t)x;
    ram[0xea97 + n] = (uint8_t)y;
    ram[0xeaa0 + n] = RESULT_MORE_COURSES;
    Label(ram, x, y, "MORE COURSES", 12);
    ram[count_address] = (uint8_t)(n + 1);
    view.title = true;
}

static void Observe(CpuState *cpu, uint32_t pc)
{
    uint8_t *ram = cpu->ram;
    switch (pc & 0x7fffff) {
    case 0x00f0ff:
        if (!custom_2p_boot) return;
        custom_2p_boot = false;
        WriteWord(ram, 0x17ff, 1u);
        WriteWord(ram, 0xeab9, 0u);
        interp_bridge_pre_opcode_redirect(0x80f1db);
        break;
    case 0x00f266:
        in_hole = true;
        /* The loader has cleared abilities; seed both players before C342
         * creates their objects and the conditions portraits. */
        if (ability_pending) {
            ram[0xd852] = ram[0xd854] = (uint8_t)view.ability;
            ram[0xd853] = ram[0xd855] = 0;
            ability_pending = false;
        }
        break;
    case 0x00f48c:
        in_hole = false;
        /* Both modes finish here, before score screens and hole progression.
         * F120 is the ordinary title entry on this same main-loop stack. CPU
         * and custom 2P matches carry on through results and course menus. */
        if (!view.active || view.title_background || view.cpu || view.custom_match) return;
        view.screen = 1;
        view.row = 4;
        view.title_background = true;
        view.error = NULL;
        swallowed |= previous;
        KdcDevSelect(NULL);
        interp_bridge_pre_opcode_redirect(0x80f120);
        fputs("Practice: hole finished, returned to picker\n", stderr);
        break;
    case 0x03b64b:
        /* Returning from a hole needs no second press of title-screen Start. */
        if (view.title_background) interp_bridge_pre_opcode_redirect(0x83b675);
        break;
    case 0x03bbd8:
        /* BB4A's caller is BB44, whose caller identifies the specific menu:
         * B744 is the root mode menu; B835 is the 2P PLAY/EXTRA submenu. */
        if (cpu->S > 0x1ffb || Word(ram, cpu->S + 1) != 0xbb46) return;
        if (Word(ram, cpu->S + 3) == 0xb744) {
            RootMenu(cpu, ram);
        } else if (Word(ram, cpu->S + 3) == 0xb835) {
            TwoPlayerSubmenu(cpu, ram);
        }
        break;
    case 0x03b74e:
        view.title = false;
        if (choosing) {
            choosing = false;
            if (cpu->A >= RESULT_EASY && cpu->A < RESULT_EASY + 3) {
                /* Continue into the original two-player menus. The CPU owns
                 * the second controller from here on. */
                view.cpu = (int)(cpu->A - RESULT_EASY) + 1;
                KdcCpuSelect((unsigned)view.cpu);
                cpu->A = RESULT_TWO_PLAYER;
                fprintf(stderr, "CPU match: %s\n", KdcCpuName((unsigned)view.cpu));
            } else {
                /* B: rebuild the ordinary title menu. */
                interp_bridge_pre_opcode_redirect(0x83b720);
            }
            return;
        }
        if (cpu->A == RESULT_CPU) {
            /* Rebuild the root menu on its original stack as the choice of
             * CPU difficulty, with the cursor on the first entry. */
            choosing = true;
            ram[0xea86] = ram[0xea87] = 0;
            interp_bridge_pre_opcode_redirect(0x83b720);
            return;
        }
        if (cpu->A != RESULT_PRACTICE) return;
        view.cpu = 0;
        view.custom_match = false;
        ClampPracticeSelection();
        view.screen = 1;
        view.title_background = true;
        view.row = 0;
        view.error = NULL;
        /* Rebuild the root menu on its original stack. Run it without guest
         * input underneath the picker, preserving animation and music. */
        interp_bridge_pre_opcode_redirect(0x83b720);
        break;
    case 0x03b836:
        if (cpu->A == RESULT_MORE_COURSES) {
            ClampPackSelection();
            view.screen = 3;
            view.row = 0;
            view.title_background = true;
            view.error = NULL;
            interp_bridge_pre_opcode_redirect(0x83b824);
            return;
        }
        /* Regular PLAY COURSE (0), EXTRA COURSE (1), or B (< 0). */
        KdcCoursePackClearActive();
        view.custom_match = false;
        break;
    case 0x03b83e:
        /* The 2P menu has committed to a match ($17FF := 1). A CPU match
         * keeps its records in its own save from here on. */
        if (!view.cpu || view.active) return;
        if (BeginSession("saves/cpu", false))
            fprintf(stderr, "CPU match: started (%s)\n", KdcCpuName((unsigned)view.cpu));
        else
            fprintf(stderr, "CPU match: %s; results will use the ordinary save\n", view.error);
        break;
    case 0x03d518:
        /* Two-player course select screen entry. */
        if (!view.custom_match || !KdcCoursePackHasActive()) return;
        if (custom_2p_committed) {
            custom_2p_committed = false;
            WriteWord(ram, 0xd7a4, 2u);
            WriteWord(ram, 0xd852, 0u);
            WriteWord(ram, 0xd854, 0u);
            WriteWord(ram, 0x6a5a, 16u);
            WriteWord(ram, 0x6a5c, 32u);
            WriteWord(ram, 0xd7a2, 0u);
            interp_bridge_pre_opcode_redirect(0x83d6d6);
            return;
        }
        /* Returned here after Hole 8 finale or pressing B on Handicap Grid:
         * reopen the extra-course picker over the animated title backdrop. */
        in_hole = false;
        ClampPackSelection();
        view.screen = 3;
        view.row = 1;
        view.title_background = true;
        view.error = NULL;
        swallowed |= previous;
        interp_bridge_pre_opcode_redirect(0x80f120);
        fputs("Custom 2P: returned to course picker\n", stderr);
        break;
    }
}

void KdcPracticeInitialize(void)
{
    static const uint32_t sites[] = {
        0x80f0ff, 0x83bbd8, 0x83b74e, 0x83b836, 0x83b83e, 0x83d518,
        0x80f266, 0x80f48c, 0x83b64b,
    };
    for (unsigned i = 0; i < sizeof(sites) / sizeof(sites[0]); ++i)
        if (!interp_bridge_add_pre_opcode_hook(sites[i], Observe)) {
            fputs("Unable to register practice menu\n", stderr);
            abort();
        }
}

void KdcPracticeReset(void)
{
    if (view.active || view.screen) swallowed |= previous;
    view.screen = 0;
    view.title = false;
    view.title_background = false;
    view.error = NULL;
    choosing = in_hole = false;
    if (custom_2p_boot) {
        /* Intentional reset entering a custom 2P / CPU course. */
        KdcCpuSelect((unsigned)view.cpu);
        return;
    }
    if (view.cpu || view.custom_match) {
        /* A CPU or custom 2P match restarts from the title, where RootMenu ends it. */
        KdcCpuSelect((unsigned)view.cpu);
        return;
    }
    /* Ctrl+R also replays the selected hole while viewing the returned picker. */
    if (view.active && !picker_reset && !KdcDevEnabled()) {
        char selector[64];
        if (FormatPracticeSelector(selector, sizeof(selector))) {
            KdcDevSelect(selector);
            KdcDevReset();
        }
    }
    ability_pending = view.active && KdcDevEnabled();
    KdcCpuSelect(0);
}

void KdcPracticeStateLoaded(void)
{
    /* The CPU planner's own rollbacks are not a change of session. */
    if (RtlIsRollbackLoad()) return;
    KdcPracticeReset();
}

static bool StartHole(void)
{
    char selector[64];
    if (!FormatPracticeSelector(selector, sizeof(selector))) {
        view.error = "COURSE PACK UNAVAILABLE";
        return false;
    }
    /* Retain the exact ordinary battery image across practice, including an
     * ordinary session with no save file. Never import practice SRAM. */
    if (!view.active && !BeginSession("saves/practice", true)) return false;
    if (view.mode < 4) KdcCoursePackClearActive();
    KdcDevSelect(selector);
    RtlReset(1);
    KdcPracticeReset();
    fprintf(stderr, "Practice: %s\n", selector);
    return true;
}

static bool StartCustomCourse(void)
{
    ClampPackSelection();
    const KdcCoursePack *p = KdcCoursePackGet((unsigned)view.pack);
    if (!p || (unsigned)view.pack_course >= p->course_count) {
        view.error = "COURSE PACK UNAVAILABLE";
        return false;
    }
    if (!view.active && !BeginSession(view.cpu ? "saves/cpu" : "saves/custom", false))
        return false;
    if (!KdcCoursePackSelect((unsigned)view.pack, (unsigned)view.pack_course)) {
        view.error = "COURSE PACK UNAVAILABLE";
        return false;
    }
    KdcDevSelect(NULL);
    view.custom_match = true;
    custom_2p_boot = true;
    custom_2p_committed = true;
    RtlReset(1);
    KdcPracticeReset();
    fprintf(stderr, "Custom 2P%s: %s / %s\n",
            view.cpu ? " (CPU)" : "", p->name, p->courses[view.pack_course].name);
    return true;
}

static void ExitPractice(void)
{
    bool cpu = view.cpu != 0;
    bool custom = view.custom_match;
    if (!EndSession(false)) return;
    RtlReset(1);
    KdcPracticeReset();
    fputs(cpu ? "CPU match: returned to ordinary title\n" :
          custom ? "Custom 2P: returned to ordinary title\n" :
                   "Practice: returned to ordinary title\n", stderr);
}

bool KdcPracticeFrame(uint32_t *inputs)
{
    uint32_t held = *inputs & 0xffffff, pressed = held & ~previous;
    previous = held;
    swallowed &= held;
    *inputs &= ~swallowed;
    if (!view.screen) {
        /* CPU and custom 2P menus/results keep their own use of Start. */
        bool match = view.cpu || view.custom_match;
        bool pause = view.active && (pressed & SNES_PAD_START) && (!match || in_hole);
        if (!pause) { KdcCpuFrame(inputs); return true; }
        view.screen = 2;
        view.row = 0;
        view.error = NULL;
        swallowed |= held;
        return false;
    }
    swallowed |= held;
    /* Both controller seats belong to the overlay while the title runs. */
    *inputs = 0;
    uint32_t directions = held & (SNES_PAD_UP | SNES_PAD_DOWN | SNES_PAD_LEFT | SNES_PAD_RIGHT);
    if (!directions || (pressed & directions)) repeat = 0;
    else if (++repeat >= 20 && (repeat - 20) % 6 == 0) pressed |= directions;
    if (pressed & SNES_PAD_B) {
        if ((view.screen == 1 || view.screen == 3) && view.active) {
            ExitPractice();
            return false;
        }
        view.screen = 0;
        view.title_background = false;
        view.row = 0;
        view.error = NULL;
        return true;
    }
    bool match = view.cpu || view.custom_match;
    int rows = view.screen == 1 ? 5 : view.screen == 3 ? 3 : match ? 2 : 4;
    if (pressed & SNES_PAD_UP) view.row = (view.row + rows - 1) % rows;
    if (pressed & SNES_PAD_DOWN) view.row = (view.row + 1) % rows;
    if (view.screen == 1) {
        int direction = !!(pressed & SNES_PAD_RIGHT) - !!(pressed & SNES_PAD_LEFT);
        if (direction) {
            view.error = NULL;
            if (view.row == 0) {
                int mcount = PracticeModeCount();
                view.mode = (view.mode + direction + mcount) % mcount;
                ClampPracticeSelection();
            } else if (view.row == 1) {
                int count = PracticeCourseCount(view.mode);
                view.course = (view.course + direction + count) % count;
            } else if (view.row == 2) view.hole = (view.hole + direction + 8) % 8;
            else if (view.row == 3) view.ability =
                (view.ability + direction + KDC_PRACTICE_ABILITIES) % KDC_PRACTICE_ABILITIES;
        }
        if (pressed & (SNES_PAD_A | SNES_PAD_START)) {
            if (view.row == 4) { StartHole(); return false; }
            else ++view.row;
        }
    } else if (view.screen == 3) {
        int direction = !!(pressed & SNES_PAD_RIGHT) - !!(pressed & SNES_PAD_LEFT);
        if (direction) {
            view.error = NULL;
            int pcount = (int)KdcCoursePackCount();
            if (view.row == 0 && pcount > 0) {
                view.pack = (view.pack + direction + pcount) % pcount;
                view.pack_course = 0;
                ClampPackSelection();
            } else if (view.row == 1 && pcount > 0) {
                const KdcCoursePack *p = KdcCoursePackGet((unsigned)view.pack);
                int ccount = p && p->course_count ? (int)p->course_count : 1;
                view.pack_course = (view.pack_course + direction + ccount) % ccount;
            }
        }
        if (pressed & (SNES_PAD_A | SNES_PAD_START)) {
            if (view.row == 2) { StartCustomCourse(); return false; }
            else ++view.row;
        }
    } else if (pressed & (SNES_PAD_A | SNES_PAD_START)) {
        /* A CPU or custom 2P match's pause offers only RESUME and EXIT MATCH. */
        switch (match && view.row == 1 ? 3 : view.row) {
        case 0: view.screen = 0; break;
        case 1: RtlReset(1); KdcPracticeReset(); break;
        case 2:
            KdcDevSelect(NULL);
            picker_reset = true;
            RtlReset(1);
            picker_reset = false;
            view.screen = 1;
            view.title_background = true;
            view.row = 0;
            break;
        case 3: ExitPractice(); break;
        }
    }
    return view.title_background;
}
