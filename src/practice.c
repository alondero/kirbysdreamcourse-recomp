/* USA title-menu extension. Hardware and the hole loader remain framework/
 * guest owned; see docs/practice.md for the observed entry points. */
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
static bool ability_pending, picker_reset;
static const char *const modes[] = {"normal", "extra", "multi", "multi-extra"};

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
bool KdcPracticeVisible(void) { return view.screen || view.active; }
bool KdcPracticeSnapshotsAllowed(void) { return !view.screen && !view.active; }

static unsigned Word(const uint8_t *ram, unsigned a)
{
    return ram[a] | ((unsigned)ram[a + 1] << 8);
}

static void Observe(CpuState *cpu, uint32_t pc)
{
    uint8_t *ram = cpu->ram;
    switch (pc & 0x7fffff) {
    case 0x00f266:
        /* The loader has cleared abilities; seed both players before C342
         * creates their objects and the conditions portraits. */
        if (ability_pending) {
            ram[0xd852] = ram[0xd854] = (uint8_t)view.ability;
            ram[0xd853] = ram[0xd855] = 0;
            ability_pending = false;
        }
        break;
    case 0x00f48c:
        /* Both modes finish here, before score screens and hole progression.
         * F120 is the ordinary title entry on this same main-loop stack. */
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
    case 0x00f558:
        /* Multiplayer results normally return to the original two-controller
         * course/roulette setup. CPU matches return to their own picker. */
        if (!view.active || !view.cpu) return;
        view.screen = 3;
        view.row = 3;
        view.title_background = true;
        view.error = NULL;
        swallowed |= previous;
        KdcDevSelect(NULL);
        KdcCpuSelect(0);
        interp_bridge_pre_opcode_redirect(0x80f120);
        fputs("CPU match: course finished, returned to picker\n", stderr);
        break;
    case 0x03b64b:
        if (view.active && view.cpu && !view.title_background) {
            view.screen = 3;
            view.row = 3;
            view.title_background = true;
            KdcDevSelect(NULL);
            KdcCpuSelect(0);
        }
        /* Returning from a hole needs no second press of title-screen Start. */
        if (view.title_background) interp_bridge_pre_opcode_redirect(0x83b675);
        break;
    case 0x03bbd8:
        /* BB4A's caller is BB44, whose caller is the root menu at B742.
         * Do not extend the shared routine's course/sound/demo submenus. */
        if (cpu->S > 0x1ffb || Word(ram, cpu->S + 1) != 0xbb46 ||
            Word(ram, cpu->S + 3) != 0xb744) return;
        {
            unsigned count_address = (cpu->D + 0x4a) & 0xffff;
            unsigned n = Word(ram, count_address);
            if (n < 3 || n > 6) return;
            unsigned x = ram[0xea8e], y = ram[0xea97 + n - 1] + 2;
            if (x > 23 || y > 21) return;
            ram[0xea8e + n] = (uint8_t)x;
            ram[0xea97 + n] = (uint8_t)y;
            ram[0xeaa0 + n] = 6; /* New return value, intercepted below. */
            ram[count_address] = (uint8_t)(n + 2);
            /* The guest uploads this tilemap immediately after BBD8.
             * Its font uses uppercase ASCII minus $20, with space at $1F. */
            const char *label = "PRACTICE";
            for (unsigned i = 0; label[i]; ++i) {
                unsigned at = 0x39da + 2 * (y * 32 + x + i);
                ram[at] = (uint8_t)(label[i] - 0x20);
                ram[at + 1] = 0x20;
            }
            ram[0xea8e + n+1]=(uint8_t)x;
            ram[0xea97+n+1]=(uint8_t)(y+2);
            ram[0xeaa0+n+1]=7;
            label="1P VS CPU";
            for (unsigned i=0;label[i];++i) {
                unsigned at=0x39da+2*((y+2)*32+x+i);
                ram[at]=(uint8_t)(label[i]==' '?31:label[i]-0x20);
                ram[at+1]=0x20;
            }
            view.title = true;
        }
        break;
    case 0x03b74e:
        view.title = false;
        if (cpu->A != 6 && cpu->A != 7) return;
        view.cpu = cpu->A == 7 ? 1 : 0;
        view.screen = view.cpu ? 3 : 1;
        if (view.cpu) { view.mode=2; view.hole=0; view.course=0; view.ability=0; }
        view.title_background = true;
        view.row = 0;
        view.error = NULL;
        /* Rebuild the root menu on its original stack. Run it without guest
         * input underneath the picker, preserving animation and music. */
        interp_bridge_pre_opcode_redirect(0x83b720);
        break;
    }
}

void KdcPracticeInitialize(void)
{
    if (!interp_bridge_add_pre_opcode_hook(0x83bbd8, Observe) ||
        !interp_bridge_add_pre_opcode_hook(0x83b74e, Observe) ||
        !interp_bridge_add_pre_opcode_hook(0x80f266, Observe) ||
        !interp_bridge_add_pre_opcode_hook(0x80f48c, Observe) ||
        !interp_bridge_add_pre_opcode_hook(0x80f558, Observe) ||
        !interp_bridge_add_pre_opcode_hook(0x83b64b, Observe)) {
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
    /* Ctrl+R also replays the selected hole while viewing the returned picker. */
    if (view.active && !picker_reset && !KdcDevEnabled()) {
        char selector[40];
        snprintf(selector, sizeof(selector), "%s:%d:%d", modes[view.mode],
                 view.course + 1, view.hole + 1);
        KdcDevSelect(selector);
        KdcDevReset();
    }
    ability_pending = view.active && KdcDevEnabled();
    KdcCpuSelect(view.active && KdcDevEnabled() ? (unsigned)view.cpu : 0);
}

void KdcPracticeStateLoaded(void) { KdcPracticeReset(); }

static bool StartHole(void)
{
    char selector[40];
    snprintf(selector, sizeof(selector), "%s:%d:%d", modes[view.mode],
             view.course + 1, view.hole + 1);
    if (!view.active) {
        /* Retain the exact ordinary battery image across practice, including
         * an ordinary session with no save file. Never import practice SRAM. */
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
        RtlSetSaveRoot(view.cpu ? "saves/cpu" : "saves/practice");
        RtlSetQuickSaveRoot(NULL);
        memset(g_sram, 0, (size_t)g_sram_size);
        RtlReadSram();
        view.active = true;
    }
    KdcDevSelect(selector);
    RtlReset(1);
    KdcPracticeReset();
    fprintf(stderr, "%s: %s\n", view.cpu ? "CPU match" : "Practice", selector);
    return true;
}

static void ExitPractice(void)
{
    if (!RtlWriteSram()) {
        view.error = "COULD NOT SAVE SESSION";
        return;
    }
    /* SRAM shape cannot change while the supported ROM is running. */
    if (g_sram_size != ordinary_size || !ordinary_sram) {
        view.error = "SAVE SESSION UNAVAILABLE";
        return;
    }
    memcpy(g_sram, ordinary_sram, (size_t)ordinary_size);
    free(ordinary_sram);
    ordinary_sram = NULL;
    RtlSetSaveRoot(ordinary_root);
    KdcDevSelect(NULL);
    view.active = false;
    view.cpu = 0;
    KdcCpuSelect(0);
    RtlReset(1);
    KdcPracticeReset();
    fputs("Practice: returned to ordinary title\n", stderr);
}

bool KdcPracticeFrame(uint32_t *inputs)
{
    uint32_t held = *inputs & 0xffffff, pressed = held & ~previous;
    previous = held;
    swallowed &= held;
    *inputs &= ~swallowed;
    if (!view.screen) {
        if (!view.active || !(pressed & SNES_PAD_START)) { KdcCpuFrame(inputs); return true; }
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
        if ((view.screen == 1 || view.screen == 3) && view.active) { ExitPractice(); return false; }
        view.screen = 0;
        view.title_background = false;
        view.row = 0;
        view.error = NULL;
        return true;
    }
    int rows = view.screen == 1 ? 5 : 4;
    if (pressed & SNES_PAD_UP) view.row = (view.row + rows - 1) % rows;
    if (pressed & SNES_PAD_DOWN) view.row = (view.row + 1) % rows;
    if (view.screen == 3) {
        int direction = !!(pressed & SNES_PAD_RIGHT) - !!(pressed & SNES_PAD_LEFT);
        if (direction) {
            if (view.row==0) view.cpu=1+(view.cpu-1+direction+3)%3;
            if (view.row==1) view.course=(view.course+direction+4)%4;
            if (view.row==2) view.mode=view.mode==2?3:2;
        }
        if (pressed & (SNES_PAD_A | SNES_PAD_START)) {
            if (view.row==3) { StartHole(); return false; }
            ++view.row;
        }
    } else if (view.screen == 1) {
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
        switch (view.row) {
        case 0: view.screen = 0; break;
        case 1: RtlReset(1); KdcPracticeReset(); break;
        case 2:
            KdcDevSelect(NULL);
            picker_reset = true;
            RtlReset(1);
            picker_reset = false;
            view.screen = view.cpu ? 3 : 1;
            view.title_background = true;
            view.row = 0;
            break;
        case 3: ExitPractice(); break;
        }
    }
    return view.title_background;
}
