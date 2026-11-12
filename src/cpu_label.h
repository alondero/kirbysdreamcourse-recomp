#ifndef KDC_CPU_LABEL_H
#define KDC_CPU_LABEL_H
#include <stdbool.h>
#include <stdint.h>

/* Two-player HUD name boxes in the USA ROM: BG3 2bpp tiles, four across and
 * two down. Player 1's box uses tiles $100-$107 and player 2's $108-$10F. */
#define KDC_CPU_LABEL_P1_TILE 0x100u
#define KDC_CPU_LABEL_P2_TILE 0x108u
#define KDC_CPU_LABEL_TILES 8u

/* Draw "CPU" above `name` in the box format the game uses: colour 1 fill,
 * colour 0 lettering and outline. Eight tiles of eight VRAM words each, in
 * tile order (top row left to right, then bottom row). Uses a host font. */
void KdcCpuLabelRender(const char *name, uint16_t tiles[KDC_CPU_LABEL_TILES][8]);

/* `vram` holds 32K words; `base` is BG3's character base word address.
 * When the game has just uploaded its own "PLAYER" artwork into player 2's
 * box (its top row then matches player 1's word for word), replace that box
 * with the CPU label. Returns true when VRAM changed. */
bool KdcCpuLabelPatch(uint16_t *vram, unsigned base, const char *name);
#endif
