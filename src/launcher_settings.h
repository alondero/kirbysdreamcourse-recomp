#pragma once

#include <stdbool.h>

struct RecompLauncherCModProvider;
struct RecompLauncherCGameInfo;

void KdcConfigureLauncher(struct RecompLauncherCGameInfo *info);

/* NULL accepts the exact raw image; otherwise returns a player-facing reason. */
const char *KdcValidateRomPath(const char *path, const char *rom_sha256);

const struct RecompLauncherCModProvider *KdcLauncherSettings(bool *widescreen,
                                                          const char *rom_sha256);
