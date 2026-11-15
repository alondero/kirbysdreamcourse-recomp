#include "transitions.h"
#include "launcher_settings.h"
#include "practice.h"
#include "dev_start.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include <stdio.h>
#include <stdlib.h>

static bool session;
static bool guarded_timing;
static unsigned remaining;

static unsigned Word(const uint8_t *ram, unsigned address)
{
    return ram[address] | (unsigned)ram[address + 1] << 8;
}

void KdcTransitionsReset(void)
{
    session = false;
    remaining = 0;
}

static bool OrdinarySession(void)
{
    const KdcPracticeView *view = KdcPracticeCurrentView();
    return !view->active && !view->screen && !view->cpu && !KdcDevEnabled();
}

static void Observe(CpuState *cpu, uint32_t pc)
{
    switch (pc & 0x7fffff) {
    case 0x00f120: /* Return to title. */
        KdcTransitionsReset();
        break;
    case 0x00f1df: /* Ordinary new-game initialization has completed. */
        session = OrdinarySession();
        remaining = 0;
        break;
    case 0x00f256: /* Stop before the next hole's loader. */
        remaining = 0;
        break;
    case 0x00f48c: /* End of hole, before tallying and progression. */
        remaining = 0;
        if (guarded_timing && session && OrdinarySession() && KdcFasterTransitionsEnabled() &&
            Word(cpu->ram, 0x17ff) == 0 && Word(cpu->ram, 0xeab9) == 0 &&
            Word(cpu->ram, 0x6a5a) < 8 && Word(cpu->ram, 0x6a5e) < 7)
            remaining = 1800; /* Fail closed after 30 guest seconds, even at a prompt. */
        break;
    }
}

void KdcTransitionsInitialize(void)
{
    /* Legacy APU scheduling can depend on host audio consumption/wall time.
     * Qualify only the guest-clock profile used by releases and play_experimental. */
    static const char *const timing[] = {
        "SNESRECOMP_APU_BEAM_ELAPSED", "SNESRECOMP_APU_HANDOFF",
        "SNESRECOMP_APU_ABSOLUTE_PREANCHOR"
    };
    guarded_timing = true;
    for (unsigned i = 0; i < sizeof(timing) / sizeof(timing[0]); ++i) {
        const char *value = getenv(timing[i]);
        if (!value || !*value || *value == '0') guarded_timing = false;
    }
    static const uint32_t sites[] = {0x80f120, 0x80f1df, 0x80f256, 0x80f48c};
    for (unsigned i = 0; i < sizeof(sites) / sizeof(sites[0]); ++i) {
        if (!interp_bridge_add_pre_opcode_hook(sites[i], Observe)) {
            fputs("Unable to register transition pacing\n", stderr);
            abort();
        }
    }
}

void KdcTransitionsFrame(void)
{
    if (!KdcFasterTransitionsEnabled() || !OrdinarySession()) remaining = 0;
    if (remaining) --remaining;
}

int KdcTransitionsSpeed(void)
{
    return remaining && KdcFasterTransitionsEnabled() && OrdinarySession() ? 3 : 1;
}
