/* Synthetic projection/queue fixtures: no ROM or extracted graphics. */
#include "course_sprites.c"
#include "snes/ppu.c"
#include <assert.h>

Snes *g_snes;
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;
static uint8_t ram[0x20000], rom[0x100000];
bool interp_bridge_add_pre_opcode_hook(uint32_t pc, InterpPreOpcodeHook hook)
{ (void)pc; (void)hook; return true; }
static void Word(unsigned a, uint16_t v) { ram[a] = v; ram[a + 1] = v >> 8; }
static KdcOrbitSprite visited;
static unsigned visits;
static void Visit(const KdcOrbitSprite *s, void *context)
{ (void)context; visited = *s; ++visits; }
static void Queue(CpuState *cpu)
{
    Word(0xc0, 0x1000); Word(0xc2, 108);
    cpu->Y = 126; cpu->A = 2;
    KdcObserveSprites(cpu, 0x808b09);
    KdcObserveSprites(cpu, 0x808c12);
    KdcObserveSprites(cpu, 0x8082ca);
}
static void SpecialEnemies(Ppu *ppu)
{
    /* Kracko's specialized drawer uses the cell center, one pixel above
     * the registry origin. Its body and shadow bypass the generic animator. */
    KdcResetSprites(ppu);
    memset(ram, 0, sizeof(ram));
    CpuState cpu = {.ram = ram, .DB = 0x7e, .X = 384, .Y = 295, .A = 4};
    Word(0x8b9e, 232); Word(0x9e, 0x89); Word(0xc0, 0x1000);
    Word(0xb4b8, 0xae); Word(0xb4ba, 0xffff); Word(0xb4f8, 0);
    Word(0xb538, 8); Word(0xb578, 3); Word(0xb5b8, 23170);
    Word(0xb638, 352); Word(0xb678, 296);
    Word(0x4e1c, 0x53); Word(0x519c, 0xe217); Word(0x521c, 0x84);
    Word(0x5f1c, 384); Word(0x5f9c, 295); Word(0x5e1c, 0xccd3);
    KdcObserveSprites(&cpu, 0x80e60b);
    assert(pending_anchor.world && !pending_anchor.unresolved);
    assert(pending_anchor.x == 8.5 && pending_anchor.y == 3.5 && pending_anchor.z == 1);
    assert(pending_anchor.origin_x == 384 && pending_anchor.origin_y == 296);
    assert(!pending_anchor.ground);
    cpu.A = 8;
    KdcObserveSprites(&cpu, 0x80e60b);
    assert(pending_anchor.world && pending_anchor.ground);

    /* Lightning and its moving sparks retain the verified cloud parent,
     * including recovery without spawn history. They remain billboards. */
    Word(0x4e1e, 0x54); Word(0x519e, 0xe743); Word(0x521e, 0x84);
    Word(0x579e, 0); Word(0xd2, 2); Word(0x5f1e, 384); Word(0x5f9e, 295);
    KdcObserveSprites(&cpu, 0x80e60b);
    assert(pending_anchor.world && !pending_anchor.ground);
    assert(pending_anchor.x == 8.5 && pending_anchor.z == 1);
    KdcResetSprites(ppu);
    Word(0x4e1e, 0x55); Word(0x509e, 0xe875); Word(0x511e, 0x84);
    Word(0x519e, 0xe55a); Word(0x521e, 0x80);
    Word(0x5f1e, 391); Word(0x5f9e, 281);
    assert(RecoverEffectAnchor(&cpu, 2).world);
    Word(0x4e1c, 0); KdcResetSprites(ppu);
    assert(!RecoverEffectAnchor(&cpu, 2).world); /* parent slot reused */

    /* Gordo's current fractional coordinates remain authoritative after
     * metadata reset and while moving away from its original map cell. */
    KdcResetSprites(ppu);
    Word(0xd2, 0);
    Word(0xb4b8, 0x80); Word(0xb638, 392); Word(0xb678, 300);
    Word(0x4e1c, 0x4c); Word(0x519c, 0xdf2f); Word(0x5e1c, 0xcca7);
    Word(0x581c, 0xc000); Word(0x589c, 8);
    Word(0x591c, 0x8000); Word(0x599c, 3); Word(0x5a1c, 23170); Word(0x5a9c, 0);
    Word(0x5f1c, 392); Word(0x5f9c, 300); Word(0x561c, 392); Word(0x569c, 300);
    cpu.X = 392; cpu.Y = 300; cpu.A = 4;
    KdcObserveSprites(&cpu, 0x80e60b);
    assert(pending_anchor.world && !pending_anchor.ground);
    assert(pending_anchor.x == 8.75 && pending_anchor.y == 3.5 && pending_anchor.z == 1);
    cpu.A = 8;
    KdcObserveSprites(&cpu, 0x80e60b);
    assert(pending_anchor.world && pending_anchor.ground);
    assert(pending_anchor.x == 8.75 && pending_anchor.y == 3.5);
    /* Neither a coincident cached projector output nor invalid task data
     * can replace the verified moving anchor. */
    projections[0] = (OrbitAnchor){.x = 99, .world = true, .origin_x = 392, .origin_y = 300};
    task_anchors[0] = (OrbitAnchor){.x = 99, .world = true};
    projection_count = projection_next = 1; cpu.A = 4;
    pending_anchor_valid = false;
    KdcObserveSprites(&cpu, 0x80e60b);
    assert(pending_anchor.world && pending_anchor.x == 8.75);
    Word(0xb4f8, 2);
    assert(!SpecialEnemyAnchor(&cpu, 0, 392, 300, false).world);
    Word(0xb4f8, 0); Word(0x589c, 99);
    assert(!SpecialEnemyAnchor(&cpu, 0, 392, 300, false).world);
    Word(0x589c, 8);
    assert(!SpecialEnemyAnchor(&cpu, 0, 393, 300, false).world);
    assert(!SpecialEnemyAnchor(&cpu, 1, 392, 300, false).world);
    assert(!SpecialEnemyAnchor(&cpu, 128, 392, 300, false).world);
}
int main(void)
{
    Ppu *ppu = ppu_init();
    Cart cart = {.rom = rom, .romImageSize = sizeof(rom)};
    Snes snes = {.ppu = ppu, .cart = &cart};
    CpuState cpu = {.ram = ram, .DB = 0x7e};
    g_snes = &snes;
    ppu->bgmode = 1; ppu->screenEnabled[0] = 2;
    ppu->bgXsc[0] = 0x51; ppu->bgXsc[1] = 0x59;
    ppu->hScroll[0] = 20; ppu->vScroll[0] = 30;
    Word(0x86, 20); Word(0x88, 30); Word(0x9e, 0x89);
    ram[0x1000] = (uint8_t)-8; Word(0x1001, 0x3384);
    ram[0x1003] = (uint8_t)-3; ram[0x1004] = 0x81;
    Word(0x40, 0x8000); Word(0x42, 2);
    Word(0x44, 0x8000); Word(0x46, 1);
    Word(0x48, 23170); Word(0x4a, 0);
    Word(0x6a82, 128); Word(0x6a86, 300); Word(0x8b9a, 456);
    uint8_t before[sizeof(ram)]; memcpy(before, ram, sizeof(ram));
    CpuState before_cpu = cpu;
    KdcObserveProjection(&cpu, 0x81a5cc);
    assert(!memcmp(before, ram, sizeof(ram)) && !memcmp(&before_cpu, &cpu, sizeof(cpu)));
    Queue(&cpu);
    KdcVisitOrbitSprites(ppu, ram, Visit, NULL);
    assert(visits == 1 && visited.world && !visited.unresolved);
    assert(visited.x == 2.5 && visited.y == 1.5 && visited.z == 1);
    assert(visited.offset_x == -3 && visited.offset_y == -8);
    assert(visited.attributes == 0x3384 && visited.large && visited.bucket == 2);

    /* The guide sample was projected earlier; its current source arrays,
     * not an origin cache, select its 3D position. */
    KdcObserveSprites(&cpu, 0x808823);
    Word(2, 2); Word(0xc2, 108); cpu.Y = 126;
    Word(0xb746 + 4, 0); Word(0xbb46 + 4, 7);
    Word(0xbf46 + 4, 0); Word(0xc346 + 4, 3);
    Word(0xc746 + 4, 46340); Word(0xcb46 + 4, 0);
    KdcObserveProjection(&cpu, 0x81af2d);
    Queue(&cpu); visits = 0;
    KdcVisitOrbitSprites(ppu, ram, Visit, NULL);
    assert(visits == 1 && visited.world && visited.x == 7 && visited.y == 3 && visited.z == 2);
    assert(visited.source_pc == 0x81af2d);

    /* Screen-space paths cannot accidentally acquire a coincident world
     * anchor. Unclassified requests survive and identify their limitation. */
    KdcObserveSprites(&cpu, 0x808823);
    cpu.X = 108; cpu.Y = 126;
    KdcObserveSprites(&cpu, 0x80e635);
    Queue(&cpu); visits = 0;
    KdcVisitOrbitSprites(ppu, ram, Visit, NULL);
    assert(visits == 1 && !visited.world && !visited.unresolved);
    assert(visited.offset_x == 105 && visited.offset_y == 118);
    KdcResetSprites(ppu);
    Queue(&cpu); visits = 0;
    KdcVisitOrbitSprites(ppu, ram, Visit, NULL);
    assert(visits == 1 && !visited.world && visited.unresolved);

    task_anchors[2] = (OrbitAnchor){.x = 8, .world = true};
    cpu.X = 4; cpu.Y = 6; KdcObserveSprites(&cpu, 0x80e776);
    assert(task_anchors[3].world && task_anchors[3].x == 8);
    KdcObserveSprites(&cpu, 0x80dfc0);
    assert(!task_anchors[3].world);

    /* Map graphics and their shadow live at the cell center, despite the
     * original registry retaining the corner projection. */
    KdcResetSprites(ppu);
    Word(0x8b9e, 232); Word(0xb4b8, 76); Word(0xb4ba, 0xffff);
    Word(0xb4f8, 0); Word(0xb538, 8); Word(0xb578, 3);
    Word(0xb5b8, 23170); Word(0xb5f8, 0);
    Word(0xb638, 352); Word(0xb678, 296);
    Word(0x4e1c, 0x49); Word(0x5f1c, 352); Word(0x5f9c, 296);
    Word(0xd2, 0); Word(0xc0, 0x1000);
    OrbitAnchor center = ResolveAnchor(&cpu, 352, 296);
    assert(center.world && center.x == 8.5 && center.y == 3.5 && center.origin_x == 384);
    cpu.X = 384; cpu.Y = 296; cpu.A = 8;
    KdcObserveProjection(&cpu, 0x85977a);
    KdcObserveSprites(&cpu, 0x80e60b);
    assert(request_count == 1 && requests[0].anchor.ground && requests[0].anchor.world);
    assert(requests[0].anchor.z == 1 && requests[0].anchor.source_pc == 0x85977a);

    /* Airborne/bobbing type49 enemies can change their draw origin before
     * the first observer draw. Their retained, registry-verified base still
     * anchors both original body animation and the separate ground shadow. */
    KdcResetSprites(ppu);
    Word(0x5a1c, 352); Word(0x5a9c, 296); Word(0x5f9c, 232);
    Word(0x5d9c, 0); Word(0x5e1c, 0x1100); Word(0x1100, 0x1000);
    Word(0x5e9c, 4);
    KdcObserveSprites(&cpu, 0x80e55e);
    assert(request_count == 1 && requests[0].anchor.world && !requests[0].anchor.ground);
    assert(requests[0].anchor.origin_x == 384 && requests[0].anchor.origin_y == 296);
    cpu.X = 384; cpu.Y = 296; cpu.A = 8;
    KdcObserveProjection(&cpu, 0x85977a);
    KdcObserveSprites(&cpu, 0x80e60b);
    assert(request_count == 2 && requests[1].anchor.world && requests[1].anchor.ground);
    assert(requests[1].anchor.z == 1 && requests[1].anchor.origin_y == 296);

    /* The converted enemy's cup interior and its separate rim task are
     * horizontal surfaces. Treating either as an upright body lets it
     * cover a ball touching the far edge of the cup. All three cup
     * variants use the same producer contract. */
    static const uint16_t cup_tables[] = {0xcbcb, 0xcbd5, 0xcbdf};
    for (unsigned table = 0; table < 3; ++table) {
        for (unsigned rim = 0; rim < 2; ++rim) {
            KdcResetSprites(ppu);
            Word(0x4e1c, rim ? 0x4a : 0x49);
            Word(0x5f9c, 296); Word(0x5d9c, rim);
            Word(0x5e1c, cup_tables[table]); Word(0x5e9c, rim ? 7 : 9);
            unsigned pointer = 0x48000 + cup_tables[table] - 0x8000 + rim * 2;
            rom[pointer] = 0; rom[pointer + 1] = 0x10;
            KdcObserveSprites(&cpu, 0x80e55e);
            assert(request_count == 1 && requests[0].anchor.world);
            assert(requests[0].anchor.ground);
            assert(requests[0].anchor.x == 8.5 && requests[0].anchor.z == 1);
        }
    }
    KdcResetSprites(ppu);
    Word(0x4e1c, 0x49); Word(0x5d9c, 1);
    KdcObserveSprites(&cpu, 0x80e55e);
    assert(request_count == 1 && !requests[0].anchor.ground);
    KdcResetSprites(ppu);
    Word(0x4e1c, 0x4a); Word(0x5d9c, 0);
    KdcObserveSprites(&cpu, 0x80e55e);
    assert(request_count == 1 && !requests[0].anchor.ground);

    /* The tee stays at the original start after the ball has moved. */
    KdcResetSprites(ppu);
    Word(0x4e1c, 0x6c); Word(0x6924, 320); Word(0x6926, 263);
    Word(0x6b64, 6); Word(0x6b66, 3); Word(0x6b68, 1);
    Word(0x5f1c, 320); Word(0x5f9c, 263); Word(0x5d9c, 0);
    Word(0x5e1c, 0x1100); Word(0x1100, 0x1000); Word(0x5e9c, 8);
    KdcObserveSprites(&cpu, 0x80e55e);
    assert(request_count == 1 && requests[0].anchor.world && requests[0].anchor.ground);
    assert(requests[0].anchor.x == 6.5 && requests[0].anchor.y == 3.5);
    assert(requests[0].anchor.origin_y == 264 && requests[0].anchor.z == 1);

    /* Duplicate native/fallback queue observations must retain a guide's
     * producer anchor even if an older projection shares its screen origin. */
    KdcResetSprites(ppu);
    Word(0x4e1c, 0); Word(2, 2); Word(0xc2, 108); cpu.Y = 126;
    KdcObserveProjection(&cpu, 0x81a5cc); /* old, coincident 2.5/1.5 point */
    KdcObserveProjection(&cpu, 0x81af2d);
    source_pc = 0x81af2d;
    Capture(&cpu, 0x89, 0x1000, 108, 126, 2, false, false);
    Capture(&cpu, 0x89, 0x1000, 108, 126, 2, false, false);
    assert(request_count == 2 && requests[0].anchor.x == 7 && requests[1].anchor.x == 7);
    assert(requests[1].anchor.z == 2 && !requests[1].anchor.unresolved);

    /* Marker height uses its retained world projection, not a screen origin
     * formed using the pending camera from another phase of the frame. */
    Word(0, 0); Word(0x6b0c, 0); Word(0x6b12, 7);
    Word(0x6b18, 0); Word(0x6b1e, 3); Word(0x6b00, 176);
    KdcObserveProjection(&cpu, 0x81aea1);
    assert(pending_anchor.world && pending_anchor.z == 1);

    /* Floating shot number and pre-shot Kirby proxy share the actual ball
     * anchor; the fixed shot-meter/crosshair remains screen-space. */
    KdcResetSprites(ppu);
    Word(0xd8aa, 0x8000); Word(0xd8ae, 6);
    Word(0xd8b2, 0x8000); Word(0xd8b6, 3);
    Word(0xd8ba, 23170); Word(0xd8be, 0);
    Word(0x91e, 320); Word(0x93e, 264);
    Word(0x4e1c, 0x12); Word(0x6b4c, 0); Word(0xd2, 0);
    source_pc = 0x8497fc;
    Capture(&cpu, 0x89, 0x1000, 300, 201, 0, false, false);
    assert(requests[0].anchor.world && requests[0].anchor.x == 6.5);
    assert(requests[0].anchor.y == 3.5 && requests[0].anchor.origin_y == 264);
    Word(0x4e1c, 0); source_pc = 0x849192;
    Capture(&cpu, 0x89, 0x1000, 300, 201, 0, false, false);
    assert(!requests[1].anchor.world && !requests[1].anchor.unresolved);
    Word(0x4e1c, 0x12); Word(0x1803, 2);
    Word(0xd8ac, 0x8000); Word(0xd8b0, 4);
    Word(0xd8b4, 0x8000); Word(0xd8b8, 2);
    Word(0xd8bc, 23170); Word(0xd8c0, 0);
    Word(0x920, 224); Word(0x940, 248);
    source_pc = 0x8497fc;
    Capture(&cpu, 0x89, 0x1000, 300, 201, 0, false, false);
    assert(requests[2].anchor.world && requests[2].anchor.x == 4.5);
    assert(requests[2].anchor.y == 2.5 && requests[2].anchor.origin_x == 224);
    KdcResetSprites(ppu);
    Word(0x4e1c, 0x1a); Word(0x6b4e, 0); Word(0x1803, 0);
    Word(0x5f1c, 320); Word(0x5f9c, 260);
    KdcObserveSprites(&cpu, 0x80e55e);
    assert(request_count == 1 && requests[0].anchor.world);
    assert(requests[0].anchor.origin_y == 264 && requests[0].anchor.z == 1);
    /* A hidden ball's radial effect keeps its spawn center after the ball
     * moves, and cloned particles inherit that exact same center. */
    KdcResetSprites(ppu);
    Word(0x4e1c + 2, 0xbe); Word(0x5f1c + 2, 320); Word(0x5f9c + 2, 264);
    Word(0x87e, 0xffff); cpu.Y = 2;
    memcpy(before, ram, sizeof(ram)); before_cpu = cpu;
    KdcObserveSprites(&cpu, 0x80e525);
    assert(!memcmp(before, ram, sizeof(ram)) && !memcmp(&before_cpu, &cpu, sizeof(cpu)));
    assert(task_anchors[1].world && task_anchors[1].x == 6.5);
    cpu.X = 2; cpu.Y = 4; KdcObserveSprites(&cpu, 0x80e776);
    Word(0x91e, 400); Word(0x93e, 300);
    assert(task_anchors[2].world && task_anchors[2].origin_x == 320);

    /* Enemy reward draw bypasses E55E. Retain its creation-time anchor
     * through E60B as the original animation rises away from its parent. */
    Word(0x4e1c, 0x49); Word(0x5a1c, 352); Word(0x5a9c, 296);
    Word(0x531c + 6, 0); cpu.Y = 6;
    KdcObserveSprites(&cpu, 0x84efc1);
    assert(task_anchors[3].world && task_anchors[3].x == 8.5);
    Word(0xd2, 6); Word(0x5f1c + 6, 385); Word(0x5f9c + 6, 262);
    cpu.X = 385; cpu.Y = 262; cpu.A = 0;
    KdcObserveSprites(&cpu, 0x80e60b);
    assert(request_count == 1 && requests[0].anchor.world);
    assert(requests[0].anchor.origin_x == 384 && requests[0].anchor.origin_y == 296);
    Word(0x4e1c + 6, 0x8d); Word(0x509c + 6, 0xd296); Word(0x511c + 6, 0x83);
    source_pc = 0x80e31d;
    Capture(&cpu, 0x89, 0x1000, 365, 232, 0, false, false);
    assert(!requests[1].anchor.world && !requests[1].anchor.unresolved);
    Word(0x509c + 6, 0xd295);
    assert(!ScorecardTask(&cpu, 6)); /* type alone never classifies HUD */

    /* State loading clears host history. The live BE root pins ball zero
     * until its completion flag, and the reward retains its enemy parent. */
    KdcResetSprites(ppu);
    Word(0x4e1c + 2, 0xbe); Word(0x589c + 2, 0);
    Word(0x501c + 2, 0x8585); Word(0x4f9c + 2, 0xe105); Word(0xd798, 0);
    Word(0x4e1c + 4, 0xbe); Word(0x589c + 4, 1); Word(0x581c + 4, 2);
    OrbitAnchor restored = RecoverEffectAnchor(&cpu, 4);
    assert(restored.world && restored.x == 6.5 && restored.origin_x == 400);
    KdcResetSprites(ppu); Word(0xd798, 1);
    assert(!RecoverEffectAnchor(&cpu, 4).world);
    Word(0x4e1c + 6, 0x5d); Word(0x519c + 6, 0xf04b); Word(0x521c + 6, 0x84);
    restored = RecoverEffectAnchor(&cpu, 6);
    assert(restored.world && restored.x == 8.5 && restored.origin_y == 296);
    assert(restored.motion_id == TaskMotion(6));
    uint64_t old_identity = restored.motion_id;
    cpu.Y = 6;
    memcpy(before, ram, sizeof(ram)); before_cpu = cpu;
    KdcObserveSprites(&cpu, 0x80dfc0);
    assert(TaskMotion(6) != old_identity);
    assert(!memcmp(before, ram, sizeof(ram)) && !memcmp(&before_cpu, &cpu, sizeof(cpu)));
    restored = RecoverEffectAnchor(&cpu, 6);
    assert(restored.world && restored.motion_id == TaskMotion(6));
    cpu.X = 6; cpu.Y = 8;
    KdcObserveSprites(&cpu, 0x80e776);
    OrbitAnchor clone = RecoverEffectAnchor(&cpu, 8);
    assert(clone.world && clone.x == restored.x && clone.motion_id != restored.motion_id);
    /* The parasol is queued at a displaced screen origin by $81:A0B6.
     * It must retain the ball anchor and the draw-time view, even after
     * the next simulation changes the player's direction. */
    KdcResetSprites(ppu);
    memset(ram, 0, sizeof(ram));
    cpu = (CpuState){.ram = ram, .DB = 0x7e};
    Word(0x91e, 80); Word(0x93e, 100);
    Word(0xd8ae, 1); Word(0xd8b6, 2); Word(0xd8ba, 23170);
    Word(0xd75a, 3); Word(0xd75e, 4); Word(0xd85a, 2);
    Word(0x4e1c, 0x1a); Word(0x509c, 0xb503); Word(0x511c, 0x85);
    Word(0x6a9c, 84); Word(0x6aa0, 96); Word(0x6aa4, 0x1000);
    ram[0x1000] = (uint8_t)-8; Word(0x1001, 0x31c9);
    ram[0x1003] = (uint8_t)-4; ram[0x1004] = 0x80;
    source_pc = 0x81a0b6;
    Capture(&cpu, 0x89, 0x1000, 84, 96, 0, false, false);
    KdcObserveSprites(&cpu, 0x808c12);
    KdcObserveSprites(&cpu, 0x8082ca);
    Word(0xd75a, 9); Word(0xd75e, 10);
    visits = 0;
    KdcVisitOrbitSprites(ppu, ram, Visit, NULL);
    assert(visits == 1 && visited.world && !visited.unresolved);
    assert(visited.x == 1 && visited.y == 2 && visited.z == 1);
    assert(visited.motion_x == 4 && visited.motion_y == -4);
    assert(visited.view.family == KDC_SPRITE_VIEW_PARASOL_TILT && visited.view.attachment);
    assert(visited.view.direction == 4 && visited.view.facing == 3);
    /* The in-viewport ball reaches the queue after the actor hook. Its
     * producer identity, not the queue helper's PC, selects the view. */
    KdcResetSprites(ppu);
    Word(0xd85a, 0); Word(0x9e, 0x89); Word(0x8de, 80);
    Word(0xc0, 0x1000); Word(0xc2, 80);
    cpu.X = 0; cpu.Y = 100; cpu.A = 0;
    KdcObserveSprites(&cpu, 0x809eec);
    KdcObserveSprites(&cpu, 0x808b09);
    KdcObserveSprites(&cpu, 0x808c12);
    KdcObserveSprites(&cpu, 0x8082ca);
    visits = 0;
    KdcVisitOrbitSprites(ppu, ram, Visit, NULL);
    assert(visits == 1 && visited.view.family == KDC_SPRITE_VIEW_BALL);
    assert(visited.view.direction == 10 && !visited.view.attachment);
    SpecialEnemies(ppu);
    ppu_free(ppu);
    return 0;
}
