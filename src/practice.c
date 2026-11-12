/* USA title-menu extension. Hardware and the hole loader remain framework/
 * guest owned; see docs/practice.md for the observed entry points. */
#include "practice.h"
#include "dev_start.h"
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
static const char *const modes[] = {"normal", "extra", "multi", "multi-extra"};

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
            if (x > 23 || y > 23) return;
            ram[0xea8e + n] = (uint8_t)x;
            ram[0xea97 + n] = (uint8_t)y;
            ram[0xeaa0 + n] = 6; /* New return value, intercepted below. */
            ram[count_address] = (uint8_t)(n + 1);
            /* The guest uploads this tilemap immediately after BBD8.
             * Its font uses uppercase ASCII minus $20, with space at $1F. */
            const char *label = "PRACTICE";
            for (unsigned i = 0; label[i]; ++i) {
                unsigned at = 0x39da + 2 * (y * 32 + x + i);
                ram[at] = (uint8_t)(label[i] - 0x20);
                ram[at + 1] = 0x20;
            }
            view.title = true;
        }
        break;
    case 0x03b74e:
        view.title = false;
        if (cpu->A != 6) return;
        view.screen = 1;
        view.row = 0;
        view.error = NULL;
        /* Rebuild the root menu on its original stack, then freeze at the
         * next frame boundary. Back resumes that exact menu. */
        interp_bridge_pre_opcode_redirect(0x83b720);
        break;
    }
}

void KdcPracticeInitialize(void)
{
    if (!interp_bridge_add_pre_opcode_hook(0x83bbd8, Observe) ||
        !interp_bridge_add_pre_opcode_hook(0x83b74e, Observe)) {
        fputs("Unable to register practice menu\n", stderr);
        abort();
    }
}

void KdcPracticeReset(void)
{
    if (view.active || view.screen) swallowed |= previous;
    view.screen = 0;
    view.title = false;
    view.error = NULL;
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
        RtlSetSaveRoot("saves/practice");
        RtlSetQuickSaveRoot(NULL);
        memset(g_sram, 0, (size_t)g_sram_size);
        RtlReadSram();
        view.active = true;
    }
    KdcDevSelect(selector);
    RtlReset(1);
    KdcPracticeReset();
    fprintf(stderr, "Practice: %s\n", selector);
    return true;
}

static void ExitPractice(void)
{
    if (!RtlWriteSram()) {
        view.error = "COULD NOT SAVE PRACTICE";
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
    RtlReset(1);
    KdcPracticeReset();
    fputs("Practice: returned to ordinary title\n", stderr);
}

bool KdcPracticeFrame(uint32_t *inputs)
{
    uint32_t held = *inputs & 0xfff, pressed = held & ~previous;
    previous = held;
    swallowed &= held;
    *inputs &= ~swallowed;
    if (!view.screen) {
        if (!view.active || !(pressed & SNES_PAD_START)) return true;
        view.screen = 2;
        view.row = 0;
        view.error = NULL;
        swallowed |= held;
        return false;
    }
    swallowed |= held;
    uint32_t directions = held & (SNES_PAD_UP | SNES_PAD_DOWN | SNES_PAD_LEFT | SNES_PAD_RIGHT);
    if (!directions || (pressed & directions)) repeat = 0;
    else if (++repeat >= 20 && (repeat - 20) % 6 == 0) pressed |= directions;
    if (pressed & SNES_PAD_B) {
        view.screen = view.screen == 1 && view.active ? 2 : 0;
        view.row = 0;
        view.error = NULL;
        return false;
    }
    if (pressed & SNES_PAD_UP) view.row = (view.row + 3) % 4;
    if (pressed & SNES_PAD_DOWN) view.row = (view.row + 1) % 4;
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
        }
        if (pressed & (SNES_PAD_A | SNES_PAD_START)) {
            if (view.row == 3) StartHole();
            else ++view.row;
        }
    } else if (pressed & (SNES_PAD_A | SNES_PAD_START)) {
        switch (view.row) {
        case 0: view.screen = 0; break;
        case 1: RtlReset(1); KdcPracticeReset(); break;
        case 2: view.screen = 1; view.row = 0; break;
        case 3: ExitPractice(); break;
        }
    }
    return false;
}
