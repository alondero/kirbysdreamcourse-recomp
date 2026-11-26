#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct PpuHdTileQuery;

/* Optional HD interface texture pack.
 * Packs are authored separately from the original artwork and are not part
 * of this repository. Each entry stores, per 4x sample, up to three palette
 * indices of the queried character with weights, so the live palette
 * recolours the artwork. */

enum { kKdcHdPackPathMax = 1024 };
typedef struct {
    bool enabled;
    char path[kKdcHdPackPathMax];
} KdcHdPackSettings;

KdcHdPackSettings KdcHdPackReadSettings(void);
bool KdcHdPackSaveSettings(const KdcHdPackSettings *settings);
bool KdcHdPackOverridden(void);
/* Check a selection without changing the active pack. A NULL error and
 * available=false mean no file was selected or found by legacy auto-detection. */
const char *KdcHdPackInspect(const KdcHdPackSettings *settings, bool *available);
/* Replace or disable the active pack. Failures leave original art active. */
const char *KdcHdPackReload(const KdcHdPackSettings *settings);

/* Parse a pack image; the data is copied. False if it is malformed. */
bool KdcHdPackLoadMemory(const void *data, size_t size);
/* Load from a file; a missing file is not an error (returns false). */
bool KdcHdPackLoadFile(const char *path);
void KdcHdPackUnload(void);
/* Probe and load the configured pack once, before rendering. */
void KdcHdPackPrewarm(void);
bool KdcHdPackAvailable(void);
unsigned KdcHdPackCount(void);
/* Changes whenever the active replacement art changes. */
uint64_t KdcHdPackRevision(void);
/* Fill 32x32 straight-alpha 0xAARRGGBB samples for the unflipped character.
 * Entries recorded for a VRAM address win over address-independent ones. */
bool KdcHdPackTile(const struct PpuHdTileQuery *query, uint32_t *pixels);
