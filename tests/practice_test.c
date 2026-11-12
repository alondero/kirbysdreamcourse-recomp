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

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK %d: %s\n", __LINE__, #x); exit(1); } } while (0)
uint8 *g_sram;
int g_sram_size = 8192;
static uint8_t ram[0x20000], battery[8192], ordinary_file[8192], practice_file[8192];
static char root[96] = "saves";
static bool write_ok = true;
static int resets;
static uint32_t redirect, sites[12];
static InterpPreOpcodeHook hooks[12];
static unsigned count;
static unsigned cpu_selected, cpu_selections, cpu_frames;
static bool planning, rollback_load;
void KdcCpuReset(void) {}
void KdcCpuSelect(unsigned d) { cpu_selected = d; ++cpu_selections; }
void KdcCpuFrame(uint32_t *inputs) { (void)inputs; ++cpu_frames; }
bool KdcCpuPlanning(void) { return planning; }
const char *KdcCpuName(unsigned d) { return d == 1 ? "EASY" : d == 2 ? "MEDIUM" : d == 3 ? "HARD" : "HUMAN"; }
bool RtlIsRollbackLoad(void) { return rollback_load; }


bool interp_bridge_add_pre_opcode_hook(uint32_t pc, InterpPreOpcodeHook hook)
{
    CHECK(count < 12); sites[count] = pc; hooks[count++] = hook; return true;
}
void interp_bridge_pre_opcode_redirect(uint32_t pc) { redirect = pc; }
const char *RtlSaveRoot(void) { return root; }
void RtlSetSaveRoot(const char *value) { snprintf(root, sizeof(root), "%s", value); }
void RtlSetQuickSaveRoot(const char *value) { CHECK(!value); }
int RtlWriteSram(void)
{
    if (!write_ok) return 0;
    memcpy(!strcmp(root, "saves") ? ordinary_file : practice_file, g_sram, 8192);
    return 1;
}
void RtlReadSram(void) { CHECK(!strcmp(root, "saves/practice") || !strcmp(root,"saves/cpu")); memcpy(g_sram, practice_file, 8192); }
void RtlReset(int mode) { CHECK(mode == 1); ++resets; KdcDevReset(); KdcPracticeReset(); }
static void Observe(CpuState *cpu, uint32_t pc)
{
    for (unsigned i = 0; i < count; ++i) if (sites[i] == pc) { hooks[i](cpu, pc); return; }
    CHECK(0);
}
static void Tap(uint32_t button)
{
    uint32_t input = 0; KdcPracticeFrame(&input);
    input = button; KdcPracticeFrame(&input);
}
static void ChoosePlay(void) { for (int i = 0; i < 4; ++i) Tap(SNES_PAD_DOWN); Tap(SNES_PAD_A); }
static void OpenPause(void) { Tap(SNES_PAD_START); CHECK(KdcPracticeCurrentView()->screen == 2); }

int main(void)
{
    g_sram = battery;
    memset(battery, 0xa5, sizeof(battery));
    memset(practice_file, 0x33, sizeof(practice_file));
    KdcDevInitialize(); KdcPracticeInitialize();
    CHECK(count == 9 && KdcPracticeSnapshotsAllowed());
    uint32_t ordinary_input = SNES_PAD_A;
    CHECK(KdcPracticeFrame(&ordinary_input));
    KdcPracticeReset();
    ordinary_input = SNES_PAD_A;
    CHECK(KdcPracticeFrame(&ordinary_input) && ordinary_input == SNES_PAD_A);
    KdcPracticeStateLoaded();
    ordinary_input = SNES_PAD_A;
    CHECK(KdcPracticeFrame(&ordinary_input) && ordinary_input == SNES_PAD_A);
    CpuState cpu = {0}; cpu.ram = ram; cpu.S = 0x1f00; cpu.D = 0x1e00;
    ram[0x1e4a] = 3; ram[0x4b] = 1; /* Real menu uses a nonzero direct page. */
    ram[0xea8e] = 11; ram[0xea99] = 13;
    Observe(&cpu, 0x83bbd8); CHECK(ram[0x1e4a] == 3); /* not root menu */
    ram[cpu.S + 1] = 0x46; ram[cpu.S + 2] = 0xbb;
    ram[cpu.S + 3] = 0x44; ram[cpu.S + 4] = 0xb7;
    Observe(&cpu, 0x83bbd8);
    CHECK(ram[0x1e4a] == 5 && ram[0xea91] == 11 && ram[0xea9a] == 15 && ram[0xeaa3] == 6);
    CHECK(ram[0x39da + 2 * (15 * 32 + 11)] == 'P' - 32);
    ram[0x1e4a] = 6; ram[0xea9c] = 17;
    Observe(&cpu, 0x83bbd8);
    CHECK(ram[0x1e4a] == 8 && ram[0xea94] == 11 && ram[0xea9d] == 19 && ram[0xeaa6] == 6);
    cpu.A = 0; Observe(&cpu, 0x83b74e); CHECK(!redirect);
    cpu.A = 6; Observe(&cpu, 0x83b74e);
    CHECK(redirect == 0x83b720 && !KdcPracticeSnapshotsAllowed());
    uint32_t menu_input = (SNES_PAD_A << 12);
    CHECK(KdcPracticeFrame(&menu_input) && menu_input == 0);
    /* Ability carousel wraps through all eleven entries. */
    for (int i = 0; i < 3; ++i) Tap(SNES_PAD_DOWN);
    Tap(SNES_PAD_LEFT); CHECK(KdcPracticeCurrentView()->ability == 10);
    Tap(SNES_PAD_RIGHT); CHECK(KdcPracticeCurrentView()->ability == 0);
    /* Verified USA conditions portraits, in guest ability-ID order. */
    static const char *const names[] = {
        "NONE", "STONE", "PARASOL", "FIREBALL", "NEEDLE", "TORNADO",
        "WHEEL", "SPARK", "HI-JUMP", "FREEZE", "UFO"
    };
    CHECK(sizeof(names) / sizeof(names[0]) == KDC_PRACTICE_ABILITIES);
    for (unsigned i = 0; i < KDC_PRACTICE_ABILITIES; ++i) {
        CHECK(KdcPracticeCurrentView()->ability == (int)i);
        const char *actual = KdcPracticeAbilityName(KdcPracticeCurrentView()->ability);
        if (strcmp(actual, names[i])) {
            fprintf(stderr, "Ability %u: expected %s, got %s\n", i, names[i], actual);
            return 1;
        }
        if (i + 1 < KDC_PRACTICE_ABILITIES) Tap(SNES_PAD_RIGHT);
    }
    CHECK(!strcmp(KdcPracticeAbilityName(KdcPracticeCurrentView()->ability), "UFO"));
    /* The title-menu font sheet is not ASCII-ordered: it carries the
     * hyphen at 29, the space at 31, '<' at 10, '>' at 12, digits at
     * 16..25 and letters at 33..58. ASCII-32 would draw the question
     * mark for the hyphen, '+' for '<' and a solid block for '>', which
     * is how the ability row read "HI?JUMP". */
    CHECK(KdcPracticeGuestGlyph(' ') == 31);
    CHECK(KdcPracticeGuestGlyph('-') == 29);
    CHECK(KdcPracticeGuestGlyph('?') == 13);
    CHECK(KdcPracticeGuestGlyph('<') == 10);
    CHECK(KdcPracticeGuestGlyph('>') == 12);
    CHECK(KdcPracticeGuestGlyph('=') == 11);
    CHECK(KdcPracticeGuestGlyph('+') == 28);
    CHECK(KdcPracticeGuestGlyph('0') == 16);
    CHECK(KdcPracticeGuestGlyph('9') == 25);
    CHECK(KdcPracticeGuestGlyph('A') == 33);
    CHECK(KdcPracticeGuestGlyph('Z') == 58);
    /* Characters the sheet does not carry are reported, not guessed. The
     * sheet's letters are uppercase only; the unnamed tiles hold blanks,
     * blocks and icons, so nothing else is claimed here. */
    CHECK(KdcPracticeGuestGlyph('a') == KDC_PRACTICE_NO_GLYPH);
    CHECK(KdcPracticeGuestGlyph('z') == KDC_PRACTICE_NO_GLYPH);
    for (int i = 0; i < 3; ++i) Tap(SNES_PAD_UP);
    Tap(SNES_PAD_B); CHECK(KdcPracticeSnapshotsAllowed());
    Observe(&cpu, 0x83b74e);
    write_ok = false; ChoosePlay();
    CHECK(!KdcPracticeCurrentView()->active && !strcmp(root, "saves") && battery[0] == 0xa5);
    CHECK(KdcPracticeCurrentView()->error && resets == 0);
    write_ok = true; Tap(SNES_PAD_A);
    CHECK(KdcPracticeCurrentView()->active && !strcmp(root, "saves/practice"));
    CHECK(battery[0] == 0x33 && ordinary_file[0] == 0xa5 && resets == 1);
    CHECK(!KdcPracticeSnapshotsAllowed());
    Observe(&cpu, 0x80f266);
    CHECK(ram[0xd852] == 10 && ram[0xd854] == 10 && ram[0xd853] == 0);
    ram[0xd852] = 0; Observe(&cpu, 0x80f266); CHECK(ram[0xd852] == 0);
    /* A held confirm cannot leak into the first playable frame. */
    uint32_t input = SNES_PAD_A; CHECK(KdcPracticeFrame(&input) && input == 0);
    OpenPause(); Tap(SNES_PAD_DOWN); Tap(SNES_PAD_A); CHECK(resets == 2);
    Observe(&cpu, 0x80f266); CHECK(ram[0xd852] == 10);
    /* All four known course families use the same developer hook. */
    for (int mode = 0; mode < 4; ++mode) {
        OpenPause(); Tap(SNES_PAD_DOWN); Tap(SNES_PAD_DOWN); Tap(SNES_PAD_A);
        CHECK(KdcPracticeCurrentView()->screen == 1);
        if (mode) Tap(SNES_PAD_RIGHT);
        CHECK(KdcPracticeCurrentView()->mode == mode);
        ChoosePlay();
        Observe(&cpu, 0x80f0ff);
        CHECK(ram[0x17ff] == (mode >= 2) && ram[0xeab9] == (mode & 1));
        Observe(&cpu, 0x80f1df);
        CHECK(ram[0x6a5a] == (mode >= 2 ? 16 : 0) + (mode & 1) * 8);
        Observe(&cpu, 0x80c168);
    }
    /* The shared completion entry returns even for hole eight/multiplayer,
     * without restoring ordinary SRAM or allowing B to resume a finished hole. */
    Observe(&cpu, 0x80f48c);
    CHECK(redirect == 0x80f120 && KdcPracticeCurrentView()->screen == 1);
    CHECK(KdcPracticeCurrentView()->active && !KdcDevEnabled());
    menu_input = SNES_PAD_A << 12;
    CHECK(KdcPracticeFrame(&menu_input) && !menu_input);
    Observe(&cpu, 0x83b64b); CHECK(redirect == 0x83b675);
    menu_input = SNES_PAD_A | (SNES_PAD_A << 12);
    CHECK(!KdcPracticeFrame(&menu_input));
    CHECK(KdcDevEnabled() && KdcPracticeCurrentView()->screen == 0);
    menu_input = SNES_PAD_A | (SNES_PAD_A << 12);
    CHECK(KdcPracticeFrame(&menu_input) && !menu_input);
    Observe(&cpu, 0x80f48c);
    CHECK(!KdcDevEnabled());
    RtlReset(1); /* Ctrl+R from the returned picker restores the target too. */
    CHECK(KdcDevEnabled() && KdcPracticeCurrentView()->screen == 0);
    Observe(&cpu, 0x80f266); CHECK(ram[0xd852] == 10);
    memset(battery, 0x77, sizeof(battery));
    OpenPause(); Tap(SNES_PAD_DOWN); Tap(SNES_PAD_DOWN); Tap(SNES_PAD_DOWN);
    write_ok = false; Tap(SNES_PAD_A);
    CHECK(KdcPracticeCurrentView()->active && !strcmp(root, "saves/practice"));
    write_ok = true; Tap(SNES_PAD_A);
    CHECK(!KdcPracticeCurrentView()->active && !strcmp(root, "saves"));
    CHECK(battery[0] == 0xa5 && ordinary_file[0] == 0xa5 && practice_file[0] == 0x77);
    CHECK(KdcPracticeSnapshotsAllowed() && !KdcDevEnabled());
    redirect = 0; Observe(&cpu, 0x80f0ff); CHECK(!redirect);
    /* 1P VS CPU turns the guest's own root menu into a difficulty choice. */
    for (unsigned i = 0; i < 5; ++i) { ram[0xea8e + i] = 11; ram[0xea97 + i] = (uint8_t)(9 + 2 * i); }
    redirect = 0; cpu.A = 7; ram[0xea86] = 4; Observe(&cpu, 0x83b74e);
    CHECK(redirect == 0x83b720 && !ram[0xea86] && KdcPracticeCurrentView()->screen == 0);
    ram[0x1e4a] = 5; ram[0xea8a] = 0;
    Observe(&cpu, 0x83bbd8);
    CHECK(ram[0x1e4a] == 3 && ram[0xeaa0] == 8 && ram[0xeaa1] == 9 && ram[0xeaa2] == 10);
    CHECK(ram[0xea8a] == 1); /* B can cancel, as in the guest's submenus. */
    CHECK(ram[0x39da + 2 * (9 * 32 + 11)] == KdcPracticeGuestGlyph('E'));
    CHECK(ram[0x39da + 2 * (11 * 32 + 11)] == KdcPracticeGuestGlyph('M'));
    CHECK(ram[0x39da + 2 * (13 * 32 + 11)] == KdcPracticeGuestGlyph('H'));
    CHECK(ram[0x39da + 2 * (15 * 32 + 11)] == 31 && ram[0x39da + 2 * (17 * 32 + 15)] == 31);
    /* B returns to the ordinary title menu. */
    redirect = 0; cpu.A = 0xffff; Observe(&cpu, 0x83b74e);
    CHECK(redirect == 0x83b720 && !cpu_selected && !KdcPracticeCurrentView()->cpu);
    ram[0x1e4a] = 3; Observe(&cpu, 0x83bbd8);
    CHECK(ram[0x1e4a] == 5 && ram[0xeaa3] == 6 && ram[0xeaa4] == 7);
    /* A difficulty continues into the original two-player menus. */
    cpu.A = 7; Observe(&cpu, 0x83b74e);
    ram[0x1e4a] = 5; Observe(&cpu, 0x83bbd8);
    redirect = 0; cpu.A = 9; Observe(&cpu, 0x83b74e);
    CHECK(!redirect && cpu.A == 1 && cpu_selected == 2 && KdcPracticeCurrentView()->cpu == 2);
    CHECK(!KdcPracticeCurrentView()->active && KdcPracticeSnapshotsAllowed());
    /* The game's own menus keep their use of Start; the CPU owns seat 2. */
    unsigned frames = cpu_frames;
    input = 0; KdcPracticeFrame(&input);
    input = SNES_PAD_START; CHECK(KdcPracticeFrame(&input) && input == SNES_PAD_START);
    CHECK(cpu_frames == frames + 2 && !KdcPracticeVisible());
    /* Committing to a match keeps playing, on the CPU's own save. */
    int resets_before = resets;
    Observe(&cpu, 0x83b83e);
    CHECK(KdcPracticeCurrentView()->active && !strcmp(root, "saves/cpu"));
    CHECK(resets == resets_before && battery[0] == 0xa5 && ordinary_file[0] == 0xa5);
    CHECK(!KdcPracticeSnapshotsAllowed() && !KdcPracticeVisible());
    planning = true; CHECK(KdcPracticeSnapshotsAllowed()); planning = false;
    /* The planner's own rollbacks do not restart the session. */
    unsigned selections = cpu_selections;
    rollback_load = true; KdcPracticeStateLoaded(); rollback_load = false;
    CHECK(cpu_selections == selections && KdcPracticeCurrentView()->active);
    /* Start pauses only during a hole, with RESUME and EXIT MATCH. */
    Observe(&cpu, 0x80f266);
    OpenPause(); Tap(SNES_PAD_DOWN); Tap(SNES_PAD_DOWN);
    CHECK(KdcPracticeCurrentView()->row == 0);
    Tap(SNES_PAD_A); CHECK(KdcPracticeCurrentView()->screen == 0);
    redirect = 0; Observe(&cpu, 0x80f48c); CHECK(!redirect);
    input = 0; KdcPracticeFrame(&input);
    input = SNES_PAD_START; CHECK(KdcPracticeFrame(&input) && input == SNES_PAD_START);
    /* Back at the title menu the session ends: ordinary save restored. */
    memset(battery, 0x55, sizeof(battery));
    ram[0x1e4a] = 3; Observe(&cpu, 0x83bbd8);
    CHECK(!KdcPracticeCurrentView()->active && !KdcPracticeCurrentView()->cpu && !cpu_selected);
    CHECK(!strcmp(root, "saves") && battery[0] == 0xa5 && practice_file[0] == 0x55);
    CHECK(resets == resets_before && ram[0x1e4a] == 5);
    /* EXIT MATCH from the pause leaves through a reset. */
    cpu.A = 7; Observe(&cpu, 0x83b74e);
    ram[0x1e4a] = 5; Observe(&cpu, 0x83bbd8);
    cpu.A = 10; Observe(&cpu, 0x83b74e); CHECK(cpu_selected == 3);
    Observe(&cpu, 0x83b83e); Observe(&cpu, 0x80f266);
    memset(battery, 0x66, sizeof(battery));
    OpenPause(); Tap(SNES_PAD_DOWN); Tap(SNES_PAD_A);
    CHECK(!KdcPracticeCurrentView()->active && resets == resets_before + 1 && !cpu_selected);
    CHECK(battery[0] == 0xa5 && practice_file[0] == 0x66 && KdcPracticeSnapshotsAllowed());
    return 0;
}
