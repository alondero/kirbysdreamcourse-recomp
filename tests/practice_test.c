#include "practice.h"
#include "dev_start.h"
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
static uint32_t redirect, sites[5];
static InterpPreOpcodeHook hooks[5];
static unsigned count;

bool interp_bridge_add_pre_opcode_hook(uint32_t pc, InterpPreOpcodeHook hook)
{
    CHECK(count < 5); sites[count] = pc; hooks[count++] = hook; return true;
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
void RtlReadSram(void) { CHECK(!strcmp(root, "saves/practice")); memcpy(g_sram, practice_file, 8192); }
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
static void ChoosePlay(void) { Tap(SNES_PAD_DOWN); Tap(SNES_PAD_DOWN); Tap(SNES_PAD_DOWN); Tap(SNES_PAD_A); }
static void OpenPause(void) { Tap(SNES_PAD_START); CHECK(KdcPracticeCurrentView()->screen == 2); }

int main(void)
{
    g_sram = battery;
    memset(battery, 0xa5, sizeof(battery));
    memset(practice_file, 0x33, sizeof(practice_file));
    KdcDevInitialize(); KdcPracticeInitialize();
    CHECK(count == 5 && KdcPracticeSnapshotsAllowed());
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
    CHECK(ram[0x1e4a] == 4 && ram[0xea91] == 11 && ram[0xea9a] == 15 && ram[0xeaa3] == 6);
    CHECK(ram[0x39da + 2 * (15 * 32 + 11)] == 'P' - 32);
    ram[0x1e4a] = 6; ram[0xea9c] = 17;
    Observe(&cpu, 0x83bbd8);
    CHECK(ram[0x1e4a] == 7 && ram[0xea94] == 11 && ram[0xea9d] == 19 && ram[0xeaa6] == 6);
    cpu.A = 0; Observe(&cpu, 0x83b74e); CHECK(!redirect);
    cpu.A = 6; Observe(&cpu, 0x83b74e);
    CHECK(redirect == 0x83b720 && !KdcPracticeSnapshotsAllowed());
    Tap(SNES_PAD_B); CHECK(KdcPracticeSnapshotsAllowed());
    Observe(&cpu, 0x83b74e);
    write_ok = false; ChoosePlay();
    CHECK(!KdcPracticeCurrentView()->active && !strcmp(root, "saves") && battery[0] == 0xa5);
    CHECK(KdcPracticeCurrentView()->error && resets == 0);
    write_ok = true; Tap(SNES_PAD_A);
    CHECK(KdcPracticeCurrentView()->active && !strcmp(root, "saves/practice"));
    CHECK(battery[0] == 0x33 && ordinary_file[0] == 0xa5 && resets == 1);
    CHECK(!KdcPracticeSnapshotsAllowed());
    /* A held confirm cannot leak into the first playable frame. */
    uint32_t input = SNES_PAD_A; CHECK(KdcPracticeFrame(&input) && input == 0);
    OpenPause(); Tap(SNES_PAD_DOWN); Tap(SNES_PAD_A); CHECK(resets == 2);
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
    memset(battery, 0x77, sizeof(battery));
    OpenPause(); Tap(SNES_PAD_DOWN); Tap(SNES_PAD_DOWN); Tap(SNES_PAD_DOWN);
    write_ok = false; Tap(SNES_PAD_A);
    CHECK(KdcPracticeCurrentView()->active && !strcmp(root, "saves/practice"));
    write_ok = true; Tap(SNES_PAD_A);
    CHECK(!KdcPracticeCurrentView()->active && !strcmp(root, "saves"));
    CHECK(battery[0] == 0xa5 && ordinary_file[0] == 0xa5 && practice_file[0] == 0x77);
    CHECK(KdcPracticeSnapshotsAllowed() && !KdcDevEnabled());
    redirect = 0; Observe(&cpu, 0x80f0ff); CHECK(!redirect);
    return 0;
}
