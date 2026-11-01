/* Game-owned presentation settings for the shared launcher's Mods page.
 * Keep the existing config.ini setting authoritative, including old installs.
 */
#include "launcher_settings.h"
#include "recomp_launcher.h"
#include "common/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pending_widescreen;
static const char *expected_rom_hash;
static const char *last_error;

static int IsFeature(const char *package, const char *feature)
{
    return package && feature && !strcmp(package, "kdc.presentation") &&
           !strcmp(feature, "widescreen");
}

static int Count(void *ctx)
{
    (void)ctx;
    return 1;
}

static int GetPackage(void *ctx, int index, RecompLauncherCModPackage *out)
{
    (void)ctx;
    if (index != 0 || !out) return 0;
    *out = (RecompLauncherCModPackage){0};
    snprintf(out->id, sizeof(out->id), "kdc.presentation");
    snprintf(out->version, sizeof(out->version), "1.0.0");
    snprintf(out->name, sizeof(out->name), "Kirby's Dream Course presentation");
    snprintf(out->license, sizeof(out->license), "PolyForm Noncommercial 1.0.0");
    out->enabled = pending_widescreen;
    return 1;
}

static int GetFeature(void *ctx, int index, RecompLauncherCModFeature *out)
{
    (void)ctx;
    if (index != 0 || !out) return 0;
    *out = (RecompLauncherCModFeature){0};
    snprintf(out->id, sizeof(out->id), "widescreen");
    snprintf(out->package_id, sizeof(out->package_id), "kdc.presentation");
    snprintf(out->package_version, sizeof(out->package_version), "1.0.0");
    snprintf(out->package_name, sizeof(out->package_name), "Kirby's Dream Course presentation");
    snprintf(out->name, sizeof(out->name), "Adaptive widescreen");
    snprintf(out->group, sizeof(out->group), "Display");
    snprintf(out->description, sizeof(out->description),
             "Show extra course terrain to fit the window or fullscreen display. "
             "Preserves pixel proportions and the centered HUD. Distant sprites "
             "still follow the original game's visibility rules.");
    out->enabled = pending_widescreen;
    out->channel = RECOMP_MOD_CHANNEL_EXPERIMENTAL;
    return 1;
}

static int EnableFeature(void *ctx, const char *package, const char *feature, int enabled)
{
    (void)ctx;
    if (!IsFeature(package, feature)) return 0;
    pending_widescreen = enabled != 0;
    return 1;
}

/* recomp-ui requires the complete feature callback group even when a
 * feature has no options. Missing callbacks select its legacy package UI. */
static int GetOption(void *ctx, const char *package, const char *feature,
                     int index, RecompLauncherCModOption *out)
{
    (void)ctx; (void)package; (void)feature; (void)index; (void)out;
    return 0;
}

static int SetOption(void *ctx, const char *package, const char *feature,
                     const char *option, const char *value)
{
    (void)ctx; (void)package; (void)feature; (void)option; (void)value;
    return 0;
}

static int Commit(void *ctx, const char *rom)
{
    /* The shared picker normalizes copier headers. Kirby's input contract
     * requires the exact raw image, so validate again before PLAY/RESUME. */
    const size_t rom_size = 1048576;
    FILE *file = rom && *rom ? fopen(rom, "rb") : NULL;
    unsigned char *bytes = file ? malloc(rom_size) : NULL;
    int valid_size = bytes && fread(bytes, 1, rom_size, file) == rom_size &&
                     fgetc(file) == EOF && !ferror(file);
    if (file) fclose(file);
    char hash_hex[65] = {0};
    if (valid_size) {
        uint8_t hash[32];
        recompui_sha256_compute(bytes, rom_size, hash);
        for (int i = 0; i < 32; ++i)
            snprintf(hash_hex + i * 2, 3, "%02x", hash[i]);
    }
    free(bytes);
    if (!valid_size || strcmp(hash_hex, expected_rom_hash)) {
        last_error = "Select the unmodified headerless 1 MiB Kirby's Dream Course "
                     "USA ROM. The selected file's size or SHA-256 does not match.";
        return 0;
    }
    last_error = NULL;
    /* The desktop host persists g_config after the launcher's commit and
     * PrepareWideFrame reads this on the next frame. No guest restart. */
    *(bool *)ctx = pending_widescreen != 0;
    return 1;
}

static const char *LastError(void *ctx)
{
    (void)ctx;
    return last_error;
}

const struct RecompLauncherCModProvider *KdcLauncherSettings(bool *widescreen,
                                                          const char *rom_sha256)
{
    static RecompLauncherCModProvider provider = {
        .package_count = Count,
        .package_get = GetPackage,
        .commit = Commit,
        .last_error = LastError,
        .feature_count = Count,
        .feature_get = GetFeature,
        .feature_enable = EnableFeature,
        .feature_option_get = GetOption,
        .feature_set_option = SetOption,
    };
    /* Reopening starts from the applied setting, discarding uncommitted edits. */
    provider.ctx = widescreen;
    expected_rom_hash = rom_sha256;
    last_error = NULL;
    pending_widescreen = *widescreen;
    return &provider;
}
