#pragma once

#include <stdbool.h>

struct RecompLauncherCModProvider;
struct RecompLauncherCGameInfo;

void KdcConfigureLauncher(struct RecompLauncherCGameInfo *info);

/* Zero follows the drawable; other values select a fixed display ratio. */
int KdcWidescreenAspect(void);
bool KdcOrbitEnabled(void);
bool KdcHudEdges(void);
bool KdcHighRefreshEnabled(void);
int KdcPresentationFps(void);
/* The saved HD textures choice (kdc-hd.cfg); off unless enabled. */
bool KdcHdTexturesEnabled(void);
/* The saved HD UI replacement choice (kdc-hd-ui.cfg); modernized by default. */
bool KdcHdUiOptionEnabled(void);
bool KdcFasterTransitionsEnabled(void);
bool KdcExtraCoursesEnabled(void);

/* NULL accepts the exact raw image; otherwise returns a player-facing reason. */
const char *KdcValidateRomPath(const char *path, const char *rom_sha256);

const struct RecompLauncherCModProvider *KdcLauncherSettings(bool *widescreen,
                                                          const char *rom_sha256);
