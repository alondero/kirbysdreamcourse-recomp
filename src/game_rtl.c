/*
 * Kirby Dream Course — per-game runtime glue.
 *
 * THIS FILE IS THE PORT. Everything else the scaffold produced is layout,
 * build wiring, and packaging; this is where the actual work happens.
 *
 * The framework does not, and cannot, drive an arbitrary SNES game on its
 * own. Two responsibilities always land on the game host:
 *
 *   1. Deciding what "one frame" means for THIS title, and returning from
 *      run_frame() at that boundary. A real ROM's reset vector never
 *      returns — it enters a main loop that waits on vblank — so somebody
 *      has to choose the yield point.
 *   2. Delivering NMI/IRQ at the hardware edge (see
 *      snesrecomp/docs/FRAME_MODEL_HOSTS.md).
 *
 * A new port starts on the framework's beam-aligned driver
 * (runner/src/beam_frame_driver.h): one frame is one PPU field, cut at the
 * beam's vblank edge; interrupts -- raster splits and a coprocessor's IRQ
 * line -- are taken where the beam latches them; parked time still runs the
 * beam, APU and coprocessors; and the field is rasterized from the raster
 * journal with real HDMA. It is the framework's, so a fix to it reaches this
 * port on a submodule pull.
 *
 * This file used to hold its own copy of a simpler driver: frames measured
 * from wherever the CPU last stopped, NMI delivered at an arbitrary beam
 * position, the field drawn with end-of-frame register state. A game that
 * uploads in vblank between INIDISP=$80 and $0F flickered black, and one
 * that chains raster splits fell apart. Replace these calls only for what
 * the driver does not do; MetalWarriorsSNESRecomp's src/mw_rtl.c and
 * GundamWingEndlessDuelSNESRecomp's src/game_rtl.c are developed examples of
 * title-specific drivers.
 */

#include "game_rtl.h"

#include "beam_frame_driver.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "snes/snes.h"
#include "snes/apu.h"
#ifdef KDC_EXPERIMENTAL_STATE
#include "snes/saveload.h"
#endif
#include "common_rtl.h"
#include "widescreen.h"
#include "snes/ws_shadow.h"
#include "course_view.h"
#include "course_sprites.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Optional local diagnostic: record actual interpreter-to-compiled calls. */
static FILE *bounce_log;
static FILE *frame_log;
static FILE *apu_state_log;
extern Snes *g_snes;
extern CpuState g_cpu;
extern int snes_frame_counter;
static void observe_bounce(uint32_t pc, int m, int x)
{
    fprintf(bounce_log, "%d %06x %d %d\n", snes_frame_counter, pc, m, x);
}

static void initialize_bounce_log(void)
{
    static int initialized;
    if (initialized) return;
    initialized = 1;
    KdcInitializeSprites();
    const char *apu_path = getenv("KDC_APU_STATE_LOG");
    if (apu_path && *apu_path) {
        apu_state_log = fopen(apu_path, "w");
        if (!apu_state_log) fprintf(stderr, "Unable to open APU state log: %s\n", apu_path);
    }
    const char *frame_path = getenv("KDC_FRAME_LOG");
    if (frame_path && *frame_path) {
        frame_log = fopen(frame_path, "w");
        if (!frame_log) fprintf(stderr, "Unable to open frame log: %s\n", frame_path);
    }
    const char *path = getenv("KDC_BOUNCE_LOG");
    if (!path || !*path) return;
    bounce_log = fopen(path, "w");
    if (!bounce_log) {
        fprintf(stderr, "Unable to open bounce log: %s\n", path);
        return;
    }
    g_interp_bridge_bounce_hook = observe_bounce;
}

void GameRunOneFrame(void)
{
    initialize_bounce_log();
    snes_beam_frame_driver_run_frame();
    if (apu_state_log && g_snes && g_snes->apu) {
        RtlApuLock();
        Apu *apu = g_snes->apu;
        fprintf(apu_state_log, "%d %llu %04x %02x %02x %02x %02x %02x %02x %02x %02x %u\n",
                snes_frame_counter, (unsigned long long)apu->portClock, apu->spc->pc,
                apu->spc->a, apu->spc->x, apu->spc->y, apu->spc->sp,
                apu->inPorts[0], apu->inPorts[1], apu->outPorts[0], apu->outPorts[1],
                apu->cpuCyclesLeft);
        RtlApuUnlock();
    }
    if (frame_log)
        fprintf(frame_log, "%d %llu %llu %06x %04x %04x %04x %04x %d %d\n",
                snes_frame_counter, (unsigned long long)g_cpu.master_cycles,
                (unsigned long long)g_cpu.cycles, interp_bridge_lle_resume_pc(),
                g_cpu.A, g_cpu.X, g_cpu.Y, g_cpu.S, g_cpu.m_flag, g_cpu.x_flag);
}

void GameDrawPpuFrame(void)
{
    /* The pinned desktop host narrows the margin to uint8_t. Reapply the
     * full buffer-supported budget here, including very wide displays. */
    PpuSetExtraSpace(g_snes->ppu, g_ws_extra);
    KdcPrepareCourseView(g_snes->ppu, g_ram, g_ws_extra);
    KdcPrepareSprites(g_snes->ppu, g_ws_extra);
    snes_beam_frame_driver_draw_ppu_frame();
    KdcReportSprites(snes_frame_counter);
}

void GameSessionReset(void)
{
    /* Rematch / soft-return: clear anything that must not survive a new
     * session. recomp-ai-rules/NETPLAY.md §3 — sticky state that "has always
     * been fine" is the usual desync culprit, because single-player never
     * re-enters the boot path twice in one process. */
    snes_beam_frame_driver_reset();
    WsShadowReset();
    KdcResetSprites(g_snes ? g_snes->ppu : NULL);
}

#ifdef KDC_EXPERIMENTAL_STATE
static uint32_t saved_beam_pc;
static bool saved_beam_wai;
static bool saved_beam_valid;
#ifdef KDC_EXPERIMENTAL_RASTER_STATE
static PpuRasterSnapshot saved_raster;
#endif
#ifdef KDC_EXPERIMENTAL_FIELD_STATE
static uint32_t saved_field_state[3];
#endif
#ifdef KDC_EXPERIMENTAL_BEAM_HDMA_STATE
static uint32_t saved_hdma_state[3];
#endif
#ifdef KDC_EXPERIMENTAL_LATE_NMI_STATE
static uint32_t saved_late_nmi_state[4];
#endif
#ifdef KDC_EXPERIMENTAL_PPU2_BUS_STATE
static uint32_t saved_ppu2_bus_state[5];
#endif

static const char *GameSnapshotIdentity(void)
{
#ifdef KDC_EXPERIMENTAL_APU_BOUNDARY_STATE
    return "kirby-apu-boundary-v1";
#elif defined(KDC_EXPERIMENTAL_SPC_D4_STATE)
    return "kirby-spc-d4-phase-v1";
#elif defined(KDC_EXPERIMENTAL_AUTOJOY_STATE)
    return "kirby-autojoy-phase-v1";
#elif defined(KDC_EXPERIMENTAL_SPC_C4_STATE)
    return "kirby-spc-c4-phase-v1";
#elif defined(KDC_EXPERIMENTAL_PPU2_BUS_STATE)
    return "kirby-ppu2-latch-v2";
#elif defined(KDC_EXPERIMENTAL_LATE_NMI_STATE)
    return "kirby-late-nmi-v1";
#elif defined(KDC_EXPERIMENTAL_BEAM_HDMA_STATE)
    return "kirby-beam-hdma-v1";
#elif defined(KDC_EXPERIMENTAL_DSP_STATE)
    return "kirby-dsp-field-v4";
#elif defined(KDC_EXPERIMENTAL_FIELD_STATE)
    return "kirby-execution-raster-field-v3";
#elif defined(KDC_EXPERIMENTAL_RASTER_STATE)
    return "kirby-execution-raster-v2";
#else
    return "kirby-execution-v1";
#endif
}

static bool GameSnapshotPreflight(const void *data, size_t size)
{
    uint32_t current_version;
    size_t guest_size = RtlSnapshotGuestSize(&current_version);
    if (!data || !guest_size || size < guest_size || size - guest_size < 12u)
        return false;
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t version;
    memcpy(&version, bytes + sizeof(uint32_t), sizeof(version));
    if (version != current_version)
        return false;
    size_t remaining = size - guest_size;
    size_t execution = RtlValidateExecutionState(bytes + guest_size, remaining);
    if (!execution)
        return false;
    size_t offset = guest_size + execution;
#ifdef KDC_EXPERIMENTAL_RASTER_STATE
#ifdef KDC_EXPERIMENTAL_FIELD_STATE
#ifdef KDC_EXPERIMENTAL_PPU2_BUS_STATE
    if (remaining - execution != sizeof(PpuRasterSnapshot) + 72u)
#elif defined(KDC_EXPERIMENTAL_LATE_NMI_STATE)
    if (remaining - execution != sizeof(PpuRasterSnapshot) + 52u)
#elif defined(KDC_EXPERIMENTAL_BEAM_HDMA_STATE)
    if (remaining - execution != sizeof(PpuRasterSnapshot) + 36u)
#else
    if (remaining - execution != sizeof(PpuRasterSnapshot) + 24u)
#endif
#else
    if (remaining - execution != sizeof(PpuRasterSnapshot) + 12u)
#endif
        return false;
    PpuRasterSnapshot raster;
    memcpy(&raster, bytes + offset, sizeof(raster));
    if (!ppu_rasterSnapshotValidate(&raster))
        return false;
    offset += sizeof(raster);
#else
    if (remaining - execution != 12u)
        return false;
#endif
#ifdef KDC_EXPERIMENTAL_FIELD_STATE
    uint32_t field[3];
    memcpy(field, bytes + offset, sizeof(field));
    if (field[0] != 0x4b444346u || field[1] > 1u || field[2] > 1u)
        return false;
    offset += sizeof(field);
#endif
#ifdef KDC_EXPERIMENTAL_BEAM_HDMA_STATE
    uint32_t hdma[3];
    memcpy(hdma, bytes + offset, sizeof(hdma));
    if (hdma[0] != 0x4b444348u)
        return false;
    offset += sizeof(hdma);
#endif
#ifdef KDC_EXPERIMENTAL_LATE_NMI_STATE
    uint32_t late[4];
    memcpy(late, bytes + offset, sizeof(late));
    if (late[0] != 0x4b44434eu || late[1] > 1u ||
        (!late[1] && (late[2] || late[3])))
        return false;
    offset += sizeof(late);
#endif
#ifdef KDC_EXPERIMENTAL_PPU2_BUS_STATE
    uint32_t ppu2[5];
    memcpy(ppu2, bytes + offset, sizeof(ppu2));
    if (ppu2[0] != 0x4b444350u || ppu2[1] > 0xffu ||
        ppu2[2] > 0x1ffu || ppu2[3] > 0x1ffu || ppu2[4] > 7u)
        return false;
    offset += sizeof(ppu2);
#endif
    uint32_t state[3];
    memcpy(state, bytes + offset, sizeof(state));
    return state[0] == 0x4b444353u && state[1] <= 0xffffffu &&
           (state[1] == 0 || cpu_pc24_resumable(state[1])) &&
           state[2] <= 1u && (state[1] != 0 || state[2] == 0);
}

static void GameSaveExtra(struct SaveLoadInfo *sli)
{
    RtlSaveExecutionState(sli);
#ifdef KDC_EXPERIMENTAL_RASTER_STATE
    PpuRasterSnapshot raster;
    if (!ppu_rasterSnapshotCapture(&raster)) {
        RtlStateStreamFail(sli);
        return;
    }
    sli->func(sli, &raster, sizeof(raster));
#endif
#ifdef KDC_EXPERIMENTAL_FIELD_STATE
    uint32_t field[3] = {0x4b444346u, g_ppu->evenFrame ? 1u : 0u,
                         g_ppu->frameInterlace ? 1u : 0u};
    sli->func(sli, field, sizeof(field));
#endif
#ifdef KDC_EXPERIMENTAL_BEAM_HDMA_STATE
    uint64_t event = snes_beam_frame_driver_get_hdma_event();
    uint32_t hdma[3] = {0x4b444348u, (uint32_t)event, (uint32_t)(event >> 32)};
    sli->func(sli, hdma, sizeof(hdma));
#endif
#ifdef KDC_EXPERIMENTAL_LATE_NMI_STATE
    uint64_t due = g_snes->lateNmiQueued ? g_snes->lateNmiDue : 0;
    uint32_t late[4] = {0x4b44434eu, g_snes->lateNmiQueued ? 1u : 0u,
                        (uint32_t)due, (uint32_t)(due >> 32)};
    sli->func(sli, late, sizeof(late));
#endif
#ifdef KDC_EXPERIMENTAL_PPU2_BUS_STATE
    uint32_t ppu2[5] = {0x4b444350u, g_ppu->ppu2OpenBus,
                        g_ppu->hCount, g_ppu->vCount,
                        (g_ppu->hCountSecond ? 1u : 0u) |
                        (g_ppu->vCountSecond ? 2u : 0u) |
                        (g_ppu->countersLatched ? 4u : 0u)};
    sli->func(sli, ppu2, sizeof(ppu2));
#endif
    uint32_t pc;
    bool wai;
    snes_beam_frame_driver_get_state(&pc, &wai);
    uint32_t state[3] = {0x4b444353u, pc, wai ? 1u : 0u};
    sli->func(sli, state, sizeof(state));
}

static void GameLoadExtra(struct SaveLoadInfo *sli, uint32_t version)
{
    (void)version;
    saved_beam_valid = false;
    if (!RtlLoadExecutionState(sli)) {
        RtlStateStreamFail(sli);
        return;
    }
#ifdef KDC_EXPERIMENTAL_RASTER_STATE
    sli->func(sli, &saved_raster, sizeof(saved_raster));
    if (!ppu_rasterSnapshotValidate(&saved_raster)) {
        RtlStateStreamFail(sli);
        return;
    }
#endif
#ifdef KDC_EXPERIMENTAL_FIELD_STATE
    sli->func(sli, saved_field_state, sizeof(saved_field_state));
    if (saved_field_state[0] != 0x4b444346u || saved_field_state[1] > 1u ||
        saved_field_state[2] > 1u) {
        RtlStateStreamFail(sli);
        return;
    }
#endif
#ifdef KDC_EXPERIMENTAL_BEAM_HDMA_STATE
    sli->func(sli, saved_hdma_state, sizeof(saved_hdma_state));
    if (saved_hdma_state[0] != 0x4b444348u) {
        RtlStateStreamFail(sli);
        return;
    }
#endif
#ifdef KDC_EXPERIMENTAL_LATE_NMI_STATE
    sli->func(sli, saved_late_nmi_state, sizeof(saved_late_nmi_state));
    if (saved_late_nmi_state[0] != 0x4b44434eu || saved_late_nmi_state[1] > 1u ||
        (!saved_late_nmi_state[1] && (saved_late_nmi_state[2] || saved_late_nmi_state[3]))) {
        RtlStateStreamFail(sli);
        return;
    }
#endif
#ifdef KDC_EXPERIMENTAL_PPU2_BUS_STATE
    sli->func(sli, saved_ppu2_bus_state, sizeof(saved_ppu2_bus_state));
    if (saved_ppu2_bus_state[0] != 0x4b444350u || saved_ppu2_bus_state[1] > 0xffu ||
        saved_ppu2_bus_state[2] > 0x1ffu || saved_ppu2_bus_state[3] > 0x1ffu ||
        saved_ppu2_bus_state[4] > 7u) {
        RtlStateStreamFail(sli);
        return;
    }
#endif
    uint32_t state[3] = {0};
    sli->func(sli, state, sizeof(state));
    if (state[0] != 0x4b444353u || state[1] > 0xffffffu || state[2] > 1u) {
        RtlStateStreamFail(sli);
        return;
    }
    saved_beam_pc = state[1];
    saved_beam_wai = state[2] != 0;
    saved_beam_valid = true;
}

static void GameStateLoaded(uint32_t version)
{
    (void)version;
    KdcResetSprites(g_snes->ppu);
    RtlApplyExecutionState();
#ifdef KDC_EXPERIMENTAL_RASTER_STATE
    if (saved_beam_valid)
        ppu_rasterSnapshotRestore(&saved_raster);
#endif
#ifdef KDC_EXPERIMENTAL_FIELD_STATE
    if (saved_beam_valid) {
        g_ppu->evenFrame = saved_field_state[1] != 0;
        g_ppu->frameInterlace = saved_field_state[2] != 0;
    }
#endif
    if (saved_beam_valid)
        snes_beam_frame_driver_set_state(saved_beam_pc, saved_beam_wai);
#ifdef KDC_EXPERIMENTAL_BEAM_HDMA_STATE
    if (saved_beam_valid)
        snes_beam_frame_driver_set_hdma_event((uint64_t)saved_hdma_state[1] |
                                               ((uint64_t)saved_hdma_state[2] << 32));
#endif
#ifdef KDC_EXPERIMENTAL_LATE_NMI_STATE
    if (saved_beam_valid) {
        g_snes->lateNmiQueued = saved_late_nmi_state[1] != 0;
        g_snes->lateNmiDue = (uint64_t)saved_late_nmi_state[2] |
                             ((uint64_t)saved_late_nmi_state[3] << 32);
    }
#endif
#ifdef KDC_EXPERIMENTAL_PPU2_BUS_STATE
    if (saved_beam_valid) {
        g_ppu->ppu2OpenBus = (uint8_t)saved_ppu2_bus_state[1];
        g_ppu->hCount = (uint16_t)saved_ppu2_bus_state[2];
        g_ppu->vCount = (uint16_t)saved_ppu2_bus_state[3];
        g_ppu->hCountSecond = (saved_ppu2_bus_state[4] & 1u) != 0;
        g_ppu->vCountSecond = (saved_ppu2_bus_state[4] & 2u) != 0;
        g_ppu->countersLatched = (saved_ppu2_bus_state[4] & 4u) != 0;
    }
#endif
    saved_beam_valid = false;
}
#else
static void GameStateLoaded(uint32_t version)
{
    (void)version;
    KdcResetSprites(g_snes->ppu);
}
#endif

const RtlGameInfo kGameInfo = {
    .title = "kirbydreamcourse",
    .initialize = NULL,
    .run_frame = &GameRunOneFrame,
    .draw_ppu_frame = &GameDrawPpuFrame,
    .save_name_prefix = "save",
#ifdef KDC_EXPERIMENTAL_STATE
    .state_save_extra = &GameSaveExtra,
    .state_load_extra = &GameLoadExtra,
    .snapshot_guard_identity = &GameSnapshotIdentity,
    .snapshot_preflight = &GameSnapshotPreflight,
#endif
    .on_state_loaded = &GameStateLoaded,
    .session_reset = &GameSessionReset,
    .hardware_reset = &GameSessionReset,
};
