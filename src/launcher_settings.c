/* Game-owned presentation settings for the shared launcher's Mods page.
 * Keep the existing config.ini setting authoritative, including old installs.
 */
#include "launcher_settings.h"
#include "recomp_launcher.h"
#include "common/sha256.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool *widescreen;
    int pending_widescreen;
    int pending_aspect;
    int pending_hud;
    int pending_orbit;
    int pending_high_refresh, pending_rate;
    const char *expected_rom_hash;
    const char *last_error;
} KdcLauncherSettingsState;

static const char *const aspect_values[] = {"auto", "16:9", "21:9", "32:9"};
static int aspect = -1;
static int orbit = -1;
static int hud_edges = -1;

bool KdcHudEdges(void)
{
    if (hud_edges < 0) {
        char value[16] = {0};
        FILE *file = fopen("kdc-hud.cfg", "r");
        if (file) { (void)fgets(value, sizeof(value), file); fclose(file); }
        value[strcspn(value, "\r\n")] = 0;
        hud_edges = !strcmp(value, "edges");
    }
    return hud_edges != 0;
}

static int high_refresh = -1, rate;
static const int rates[] = {0, 60, 90, 120, 144, 165, 240, 360};
static const char *const rate_values[] = {"display", "60", "90", "120", "144", "165", "240", "360"};

bool KdcHighRefreshEnabled(void)
{
    if (high_refresh < 0) {
        FILE *file = fopen("kdc-presentation.cfg", "r");
        int enabled = 0, fps = 0;
        if (file) {
            if (fscanf(file, "%d %d", &enabled, &fps) != 2) enabled = 0;
            fclose(file);
        }
        high_refresh = 0;
        for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
            if (fps == rates[i]) { rate = (int)i; high_refresh = enabled == 1; break; }
        }
    }
    return high_refresh != 0;
}

int KdcPresentationFps(void)
{
    (void)KdcHighRefreshEnabled();
    return rates[rate];
}

static int IsHighRefresh(const char *package, const char *feature)
{
    return package && feature && !strcmp(package, "kdc.presentation") &&
           !strcmp(feature, "high-refresh");
}

bool KdcOrbitEnabled(void)
{
    if (orbit < 0) {
        FILE *file = fopen("kdc-camera.cfg", "r");
        int value = file ? fgetc(file) : '1';
        if (file) fclose(file);
        orbit = value != '0';
    }
    return orbit != 0;
}

int KdcWidescreenAspect(void)
{
    if (aspect < 0) {
        char value[32] = {0};
        FILE *file = fopen("kdc-widescreen.cfg", "r");
        if (file) { (void)fgets(value, sizeof(value), file); fclose(file); }
        value[strcspn(value, "\r\n")] = 0;
        aspect = 0;
        for (int i = 1; i < 4; ++i)
            if (!strcmp(value, aspect_values[i])) aspect = i;
    }
    return aspect;
}

void KdcConfigureLauncher(struct RecompLauncherCGameInfo *info)
{
    /* Kirby has not qualified the snapshot round trips needed by run-ahead
     * and mid-session renderer/audio-rate changes. Offer those controls only
     * before boot, and resume the same ROM after every settings visit. */
    info->has_run_ahead = 0;
    info->has_renderer = !info->in_session;
    info->audio_frequency_locked = info->in_session;
    info->disc_selection_locked = info->in_session;
}

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

static int FeatureCount(void *ctx)
{
    (void)ctx;
    return 3;
}

static int IsOrbit(const char *package, const char *feature)
{
    return package && feature && !strcmp(package, "kdc.presentation") &&
           !strcmp(feature, "orbit");
}

static int GetPackage(void *ctx, int index, RecompLauncherCModPackage *out)
{
    const KdcLauncherSettingsState *state = ctx;
    if (index != 0 || !out) return 0;
    *out = (RecompLauncherCModPackage){0};
    snprintf(out->id, sizeof(out->id), "kdc.presentation");
    snprintf(out->version, sizeof(out->version), "1.0.0");
    snprintf(out->name, sizeof(out->name), "Kirby's Dream Course presentation");
    snprintf(out->license, sizeof(out->license), "PolyForm Noncommercial 1.0.0");
    out->enabled = state->pending_widescreen || state->pending_orbit || state->pending_high_refresh;
    return 1;
}

static int GetFeature(void *ctx, int index, RecompLauncherCModFeature *out)
{
    const KdcLauncherSettingsState *state = ctx;
    if (index < 0 || index >= 3 || !out) return 0;
    *out = (RecompLauncherCModFeature){0};
    snprintf(out->id, sizeof(out->id), "widescreen");
    snprintf(out->package_id, sizeof(out->package_id), "kdc.presentation");
    snprintf(out->package_version, sizeof(out->package_version), "1.0.0");
    snprintf(out->package_name, sizeof(out->package_name), "Kirby's Dream Course presentation");
    snprintf(out->name, sizeof(out->name), "Widescreen");
    snprintf(out->group, sizeof(out->group), "Display");
    snprintf(out->description, sizeof(out->description),
             "Show extra course terrain using the selected view aspect. "
             "Preserves pixel proportions. Choose centered or edge HUD placement. Distant sprites "
             "still follow the original game's visibility rules.");
    out->enabled = state->pending_widescreen;
    out->option_count = 2;
    out->channel = RECOMP_MOD_CHANNEL_EXPERIMENTAL;
    if (index == 1) {
        snprintf(out->id, sizeof(out->id), "orbit");
        snprintf(out->name, sizeof(out->name), "Orbit camera");
        snprintf(out->description, sizeof(out->description),
                 "On a hole: right stick left/right orbits; up/down zooms; "
                 "stick click restores the original view. Keyboard: keypad "
                 "4/6, 8/2 and 5. The camera starts in the original view.");
        out->enabled = state->pending_orbit;
        out->option_count = 0;
    }
    if (index == 2) {
        snprintf(out->id, sizeof(out->id), "high-refresh");
        snprintf(out->name, sizeof(out->name), "Smooth presentation");
        snprintf(out->description, sizeof(out->description),
                 "Interpolate course camera and object motion between game ticks. "
                 "Uses the reconstructed course view and adds one tick of visual delay. "
                 "Menus and transitions retain their original presentation.");
        out->enabled = state->pending_high_refresh;
        out->option_count = 1;
    }
    return 1;
}

static int EnableFeature(void *ctx, const char *package, const char *feature, int enabled)
{
    KdcLauncherSettingsState *state = ctx;
    if (IsHighRefresh(package, feature)) {
        state->pending_high_refresh = enabled != 0;
        return 1;
    }
    if (IsOrbit(package, feature)) {
        state->pending_orbit = enabled != 0;
        return 1;
    }
    if (!IsFeature(package, feature)) return 0;
    state->pending_widescreen = enabled != 0;
    return 1;
}

static int GetOption(void *ctx, const char *package, const char *feature,
                     int index, RecompLauncherCModOption *out)
{
    const KdcLauncherSettingsState *state = ctx;
    if (IsHighRefresh(package, feature) && index == 0 && out) {
        *out = (RecompLauncherCModOption){0};
        snprintf(out->id, sizeof(out->id), "fps");
        snprintf(out->label, sizeof(out->label), "Presentation rate");
        snprintf(out->description, sizeof(out->description), "Game speed stays unchanged. Display follows your monitor's refresh rate.");
        snprintf(out->value, sizeof(out->value), "%s", rate_values[state->pending_rate]);
        snprintf(out->default_value, sizeof(out->default_value), "display");
        out->type = RECOMP_MOD_OPTION_CHOICE;
        out->choice_count = sizeof(rates) / sizeof(rates[0]);
        return 1;
    }
    if (!IsFeature(package, feature) || index < 0 || index > 1 || !out) return 0;
    if (index == 1) {
        *out = (RecompLauncherCModOption){0};
        snprintf(out->id, sizeof(out->id), "hud");
        snprintf(out->label, sizeof(out->label), "HUD placement");
        snprintf(out->description, sizeof(out->description), "Edges keeps ability portraits and energy at the sides, with other status separated. Shot controls stay in their original position. Unsupported scenes stay centered.");
        snprintf(out->value, sizeof(out->value), "%s", state->pending_hud ? "edges" : "centered");
        snprintf(out->default_value, sizeof(out->default_value), "centered");
        out->type = RECOMP_MOD_OPTION_CHOICE; out->choice_count = 2;
        return 1;
    }
    *out = (RecompLauncherCModOption){0};
    snprintf(out->id, sizeof(out->id), "aspect");
    snprintf(out->label, sizeof(out->label), "View aspect");
    snprintf(out->description, sizeof(out->description), "Automatic follows the window; a fixed ratio letterboxes when needed. Pixel aspect remains independent.");
    snprintf(out->value, sizeof(out->value), "%s", aspect_values[state->pending_aspect]);
    snprintf(out->default_value, sizeof(out->default_value), "auto");
    out->type = RECOMP_MOD_OPTION_CHOICE;
    out->choice_count = 4;
    return 1;
}

static int GetChoice(void *ctx, const char *package, const char *feature,
                     const char *option, int index, RecompLauncherCModChoice *out)
{
    (void)ctx;
    if (IsFeature(package, feature) && option && !strcmp(option, "hud") && out && index >= 0 && index < 2) {
        *out = (RecompLauncherCModChoice){0};
        snprintf(out->value, sizeof(out->value), "%s", index ? "edges" : "centered");
        snprintf(out->label, sizeof(out->label), "%s", index ? "Edges" : "Centered (original)");
        return 1;
    }
    if (IsHighRefresh(package, feature) && option && !strcmp(option, "fps") &&
        index >= 0 && index < (int)(sizeof(rates) / sizeof(rates[0])) && out) {
        *out = (RecompLauncherCModChoice){0};
        snprintf(out->value, sizeof(out->value), "%s", rate_values[index]);
        snprintf(out->label, sizeof(out->label), "%s", index ? rate_values[index] : "Display");
        return 1;
    }
    if (!IsFeature(package, feature) || !option || strcmp(option, "aspect") ||
        index < 0 || index >= 4 || !out) return 0;
    *out = (RecompLauncherCModChoice){0};
    snprintf(out->value, sizeof(out->value), "%s", aspect_values[index]);
    snprintf(out->label, sizeof(out->label), "%s", index ? aspect_values[index] : "Automatic");
    return 1;
}

static int SetOption(void *ctx, const char *package, const char *feature,
                     const char *option, const char *value)
{
    KdcLauncherSettingsState *state = ctx;
    if (IsFeature(package, feature) && option && value && !strcmp(option, "hud")) {
        if (strcmp(value, "centered") && strcmp(value, "edges")) return 0;
        state->pending_hud = !strcmp(value, "edges");
        return 1;
    }
    if (IsHighRefresh(package, feature) && option && !strcmp(option, "fps") && value) {
        for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
            if (!strcmp(value, rate_values[i])) { state->pending_rate = (int)i; return 1; }
        }
        return 0;
    }
    if (!IsFeature(package, feature) || !option || strcmp(option, "aspect") || !value) return 0;
    for (int i = 0; i < 4; ++i) {
        if (!strcmp(value, aspect_values[i])) { state->pending_aspect = i; return 1; }
    }
    return 0;
}

const char *KdcValidateRomPath(const char *rom, const char *expected_rom_hash)
{
    /* Require the exact raw image at both launcher commit and host startup,
     * including paths that bypass the shared picker's normalized checks. */
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
        return "The ROM must be the unmodified headerless 1 MiB Kirby's Dream Course "
               "USA image. The file is unavailable, or its size or SHA-256 does not match.";
    }
    return NULL;
}

static int Commit(void *ctx, const char *rom)
{
    KdcLauncherSettingsState *state = ctx;
    state->last_error = KdcValidateRomPath(rom, state->expected_rom_hash);
    if (state->last_error) {
        /* The launcher's close path resumes even when this commit refuses;
         * its transient UI status is no longer visible after the close. */
        fprintf(stderr, "Launcher feature settings were not applied: %s\n", state->last_error);
        return 0;
    }
    if (state->pending_aspect != KdcWidescreenAspect()) {
        FILE *file = fopen("kdc-widescreen.cfg", "w");
        if (!file) {
            state->last_error = "Could not save widescreen settings.";
            fprintf(stderr, "Launcher feature settings were not applied: %s\n", state->last_error);
            return 0;
        }
        int ok = fprintf(file, "%s\n", aspect_values[state->pending_aspect]) > 0;
        if (fclose(file) != 0) ok = 0;
        if (!ok) {
            state->last_error = "Could not save widescreen settings.";
            fprintf(stderr, "Launcher feature settings were not applied: %s\n", state->last_error);
            return 0;
        }
        aspect = state->pending_aspect;
    }
    if (state->pending_orbit != KdcOrbitEnabled()) {
        FILE *file = fopen("kdc-camera.cfg", "w");
        int ok = file && fprintf(file, "%d\n", state->pending_orbit) > 0;
        if (file && fclose(file) != 0) ok = 0;
        if (!ok) {
            state->last_error = "Could not save camera settings.";
            return 0;
        }
        orbit = state->pending_orbit;
    }
    if (state->pending_hud != KdcHudEdges()) {
        FILE *file = fopen("kdc-hud.cfg", "w");
        int ok = file && fprintf(file, "%s\n", state->pending_hud ? "edges" : "centered") > 0;
        if (file && fclose(file) != 0) ok = 0;
        if (!ok) { state->last_error = "Could not save HUD settings."; return 0; }
        hud_edges = state->pending_hud;
    }
    /* The desktop host persists g_config after the launcher's commit and
     * PrepareWideFrame reads this on the next frame. No guest restart. */
    if (state->pending_high_refresh != KdcHighRefreshEnabled() || state->pending_rate != rate) {
        FILE *file = fopen("kdc-presentation.cfg", "w");
        int ok = file && fprintf(file, "%d %d\n", state->pending_high_refresh, rates[state->pending_rate]) > 0;
        if (file && fclose(file) != 0) ok = 0;
        if (!ok) { state->last_error = "Could not save presentation settings."; return 0; }
        high_refresh = state->pending_high_refresh;
        rate = state->pending_rate;
    }
    *state->widescreen = state->pending_widescreen != 0;
    return 1;
}

static const char *LastError(void *ctx)
{
    const KdcLauncherSettingsState *state = ctx;
    return state->last_error;
}

const struct RecompLauncherCModProvider *KdcLauncherSettings(bool *widescreen,
                                                          const char *rom_sha256)
{
    static KdcLauncherSettingsState state;
    static const RecompLauncherCModProvider provider = {
        .ctx = &state,
        .package_count = Count,
        .package_get = GetPackage,
        .commit = Commit,
        .last_error = LastError,
        .feature_count = FeatureCount,
        .feature_get = GetFeature,
        .feature_enable = EnableFeature,
        .feature_option_get = GetOption,
        .feature_choice_get = GetChoice,
        .feature_set_option = SetOption,
    };
    /* One modal launcher at a time: reset this singleton's staged state on
     * every open, discarding uncommitted edits from the previous visit. */
    (void)KdcHighRefreshEnabled(); /* Initialize rate before copying staged fields. */
    state = (KdcLauncherSettingsState){
        .widescreen = widescreen,
        .pending_widescreen = *widescreen,
        .pending_aspect = KdcWidescreenAspect(),
        .pending_hud = KdcHudEdges(),
        .pending_orbit = KdcOrbitEnabled(),
        .pending_high_refresh = KdcHighRefreshEnabled(),
        .pending_rate = rate,
        .expected_rom_hash = rom_sha256,
    };
    return &provider;
}
