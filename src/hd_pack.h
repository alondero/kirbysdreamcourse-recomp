#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct PpuHdTileQuery;

/* Optional HD interface texture pack (kdc-hd-ui.pack, or KDC_HD_PACK).
 * Packs are authored separately from the original artwork and are not part
 * of this repository. Each entry stores, per 4x sample, up to three palette
 * indices of the queried character with weights, so the live palette
 * recolours the artwork. */

/* Parse a pack image; the data is copied. False if it is malformed. */
bool KdcHdPackLoadMemory(const void *data, size_t size);
/* Load from a file; a missing file is not an error (returns false). */
bool KdcHdPackLoadFile(const char *path);
void KdcHdPackUnload(void);
/* Loads the default pack once, on first use. */
bool KdcHdPackAvailable(void);
unsigned KdcHdPackCount(void);
/* Fill 32x32 straight-alpha 0xAARRGGBB samples for the unflipped character.
 * Entries recorded for a VRAM address win over address-independent ones. */
bool KdcHdPackTile(const struct PpuHdTileQuery *query, uint32_t *pixels);
