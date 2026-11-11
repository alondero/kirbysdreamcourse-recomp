#ifndef KDC_COURSE_SPRITE_VIEW_H
#define KDC_COURSE_SPRITE_VIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum KdcSpriteViewFamily {
    KDC_SPRITE_VIEW_NONE,
    KDC_SPRITE_VIEW_BALL,
    KDC_SPRITE_VIEW_PARASOL_UP,
    KDC_SPRITE_VIEW_PARASOL_TILT,
    KDC_SPRITE_VIEW_PARASOL_SIDE
};

/* Capture with the draw request, not from RAM during presentation. Direction
 * selects graphics; facing selects the attachment's projected displacement.
 * player is the guest byte offset (0 or 2). direction/facing are $D75E/$D75A
 * at the request; the shot code clears them to 0 while aiming, so for the
 * ball they are not authoritative. aim is the active player's $D9DE, valid
 * when aimed: among the views VRAM and the OBJ show, the aim is preferred,
 * then direction. */
typedef struct KdcSpriteView {
    uint8_t family, direction, facing, player;
    bool attachment;
    bool aimed;
    uint8_t aim;
} KdcSpriteView;

struct KdcOrbitSprite;

/* Verified USA-ROM drawing tables only. Never writes ROM, VRAM, or guest RAM.
 * A mismatched/unsupported animation returns false without changing sprite.
 * Accepted pieces carry immutable ROM tile offsets, safe in frame history. */
bool KdcApplySpriteView(const uint8_t *rom, size_t rom_size,
                        const uint16_t *vram, KdcSpriteView view, double yaw,
                        unsigned size, struct KdcOrbitSprite *sprite);

#endif
