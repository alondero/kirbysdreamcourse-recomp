#ifndef KDC_COURSE_SPRITES_H
#define KDC_COURSE_SPRITES_H

#include <stdint.h>
#include <stdbool.h>
#include "course_sprite_view.h"

struct CpuState;
struct Ppu;

/* Observers never write guest state or perform bus reads. The cfg registers
 * the same instruction sites for generated code as Initialize does for LLE. */
void KdcObserveSprites(const struct CpuState *cpu, uint32_t pc);
void KdcObserveProjection(const struct CpuState *cpu, uint32_t pc);
void KdcInitializeSprites(void);
void KdcResetSprites(struct Ppu *ppu);
void KdcPrepareSprites(struct Ppu *ppu, const uint8_t *ram, int extra);
void KdcReportSprites(int frame);

/* Piece graphics retain their original OBJ attributes. World pieces are
 * camera-facing billboards at cell coordinates, with elevation in 16-pixel
 * terrain steps. Screen pieces use native-screen offsets. Unresolved pieces
 * remain visible, but are explicitly distinguished from known HUD pieces. */
typedef struct KdcOrbitSprite {
    double x, y, z;
    double offset_x, offset_y;
    uint16_t attributes;
    uint8_t large, bucket;
    bool world, unresolved, ground;
    uint32_t source_pc;
    /* Zero means unverified continuity. Allocation generations distinguish
     * task reuse; piece identity follows the layout address, not OBJ order. */
    uint64_t motion_id;
    uint32_t piece_id;
    /* Whole draw-request displacement from the world anchor, separate from
     * individual animation-piece offsets. Survives a change of layout. */
    double motion_x, motion_y;
    KdcSpriteView view;
    /* Optional immutable ROM tiles, in row-major order for an 8/16px piece.
     * Zero retains the original VRAM texel source. */
    uint32_t view_tiles[4];
    int8_t view_order; /* Transformed attachment: +1 before body, -1 after. */
} KdcOrbitSprite;
typedef void (*KdcOrbitSpriteVisitor)(const KdcOrbitSprite *sprite, void *context);
void KdcVisitOrbitSprites(const struct Ppu *ppu, const uint8_t *ram,
                         KdcOrbitSpriteVisitor visitor, void *context);

#endif
