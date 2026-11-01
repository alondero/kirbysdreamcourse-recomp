#pragma once

#include <stdbool.h>

struct RecompLauncherCModProvider;

const struct RecompLauncherCModProvider *KdcLauncherSettings(bool *widescreen,
                                                          const char *rom_sha256);
