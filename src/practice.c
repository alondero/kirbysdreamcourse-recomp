/* USA title-menu extension. Hardware and the hole loader remain framework/
 * guest owned; see docs/practice.md and docs/cpu-opponent.md for the
 * observed entry points. */
#include "practice.h"
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
static const char *const modes[] = {"normal", "extra", "multi", "multi-extra"};
/* Root-menu return values this extension adds. 0..5 are the guest's own. */
enum { RESULT_PRACTICE = 6, RESULT_CPU = 7, RESULT_EASY = 8, RESULT_TWO_PLAYER = 1 };

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

const KdcPracticeView *KdcPracticeCurrentView(void) { return &view; }
bool KdcPracticeVisible(void) { return view.screen || (view.active && !view.cpu); }
bool KdcPracticeSnapshotsAllowed(void)
{
    /* The CPU's private planning snapshot is restored within the same frame. */
    return KdcCpuPlanning() || (!view.screen && !view.active);
}

static unsigned Word(const uint8_t *ram, unsigned a)
{
    return ram[a] | ((unsigned)ram[a + 1] << 8);
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
    view.active = false;
    view.cpu = 0;
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
    if (view.cpu) {
        /* Back at the title menu from a CPU match or its 2P menus. */
        if (view.active && !EndSession(true)) fprintf(stderr, "CPU match: %s\n", view.error);
        view.cpu = 0;
        view.error = NULL;
        KdcCpuSelect(0);
        fputs("CPU match: returned to title menu\n", stderr);
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

static void Observe(CpuState *cpu, uint32_t pc)
{
    uint8_t *ram = cpu->ram;
    switch (pc & 0x7fffff) {
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
         * matches carry on through the game's own results and course menus. */
        if (!view.active || view.title_background || view.cpu) return;
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
        /* BB4A's caller is BB44, whose caller is the root menu at B742.
         * Do not extend the shared routine's course/sound/demo submenus. */
        if (cpu->S > 0x1ffb || Word(ram, cpu->S + 1) != 0xbb46 ||
            Word(ram, cpu->S + 3) != 0xb744) return;
        RootMenu(cpu, ram);
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
        view.screen = 1;
        view.title_background = true;
        view.row = 0;
        view.error = NULL;
        /* Rebuild the root menu on its original stack. Run it without guest
         * input underneath the picker, preserving animation and music. */
        interp_bridge_pre_opcode_redirect(0x83b720);
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
    }
}

void KdcPracticeInitialize(void)
{
    static const uint32_t sites[] = {0x83bbd8, 0x83b74e, 0x80f266, 0x80f48c, 0x83b64b, 0x83b83e};
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
    if (view.cpu) {
        /* A CPU match restarts from the title, where the menu ends it. */
        KdcCpuSelect((unsigned)view.cpu);
        return;
    }
    /* Ctrl+R also replays the selected hole while viewing the returned picker. */
    if (view.active && !picker_reset && !KdcDevEnabled()) {
        char selector[40];
        snprintf(selector, sizeof(selector), "%s:%d:%d", modes[view.mode],
                 view.course + 1, view.hole + 1);
        KdcDevSelect(selector);
        KdcDevReset();
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
    char selector[40];
    snprintf(selector, sizeof(selector), "%s:%d:%d", modes[view.mode],
             view.course + 1, view.hole + 1);
    /* Retain the exact ordinary battery image across practice, including an
     * ordinary session with no save file. Never import practice SRAM. */
    if (!view.active && !BeginSession("saves/practice", true)) return false;
    KdcDevSelect(selector);
    RtlReset(1);
    KdcPracticeReset();
    fprintf(stderr, "Practice: %s\n", selector);
    return true;
}

static void ExitPractice(void)
{
    bool cpu = view.cpu != 0;
    if (!EndSession(false)) return;
    RtlReset(1);
    KdcPracticeReset();
    fputs(cpu ? "CPU match: returned to ordinary title\n" :
                "Practice: returned to ordinary title\n", stderr);
}

bool KdcPracticeFrame(uint32_t *inputs)
{
    uint32_t held = *inputs & 0xffffff, pressed = held & ~previous;
    previous = held;
    swallowed &= held;
    *inputs &= ~swallowed;
    if (!view.screen) {
        /* A CPU match's menus and results keep their own use of Start. */
        bool pause = view.active && (pressed & SNES_PAD_START) && (!view.cpu || in_hole);
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
        if (view.screen == 1 && view.active) { ExitPractice(); return false; }
        view.screen = 0;
        view.title_background = false;
        view.row = 0;
        view.error = NULL;
        return true;
    }
    int rows = view.screen == 1 ? 5 : view.cpu ? 2 : 4;
    if (pressed & SNES_PAD_UP) view.row = (view.row + rows - 1) % rows;
    if (pressed & SNES_PAD_DOWN) view.row = (view.row + 1) % rows;
    if (view.screen == 1) {
        int direction = !!(pressed & SNES_PAD_RIGHT) - !!(pressed & SNES_PAD_LEFT);
        if (direction) {
            view.error = NULL;
            if (view.row == 0) {
                view.mode = (view.mode + direction + 4) % 4;
                if (view.mode >= 2 && view.course >= 4) view.course = 3;
            } else if (view.row == 1) {
                int count = view.mode >= 2 ? 4 : 8;
                view.course = (view.course + direction + count) % count;
            } else if (view.row == 2) view.hole = (view.hole + direction + 8) % 8;
            else if (view.row == 3) view.ability =
                (view.ability + direction + KDC_PRACTICE_ABILITIES) % KDC_PRACTICE_ABILITIES;
        }
        if (pressed & (SNES_PAD_A | SNES_PAD_START)) {
            if (view.row == 4) { StartHole(); return false; }
            else ++view.row;
        }
    } else if (pressed & (SNES_PAD_A | SNES_PAD_START)) {
        /* A CPU match's pause offers only RESUME and EXIT MATCH. */
        switch (view.cpu && view.row == 1 ? 3 : view.row) {
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
