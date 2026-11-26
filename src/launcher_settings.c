/* Game-owned presentation settings for the shared launcher's Mods page.
 * Keep the existing config.ini setting authoritative, including old installs.
 */
#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#include "launcher_settings.h"
#include "course_pack.h"
#include "hd_pack.h"
#include "hd_textures.h"
#include "recomp_launcher.h"
#include "common/sha256.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <wchar.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
    bool *widescreen;
    int pending_widescreen;
    int pending_aspect;
    int pending_hud;
    int pending_orbit;
    int pending_high_refresh, pending_rate;
    int pending_hd;
    KdcHdPackSettings saved_pack, pending_pack;
    const char *pack_error;
    bool pack_available, pack_selected;
    int pending_transitions;
    int pending_extra_courses;
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

static int hd = -1;
static int transitions = -1;
static int extra_courses = -1;

bool KdcExtraCoursesEnabled(void)
{
    if (extra_courses < 0) {
        FILE *file = fopen("kdc-courses.cfg", "r");
        int value = file ? fgetc(file) : '0';
        if (file) fclose(file);
        extra_courses = value == '1';
        KdcCoursePacksSetEnabled(extra_courses != 0);
    }
    return extra_courses != 0;
}

static int IsExtraCourses(const char *package, const char *feature)
{
    return package && feature && !strcmp(package, "kdc.presentation") &&
           !strcmp(feature, "extra-courses");
}

bool KdcFasterTransitionsEnabled(void)
{
    if (transitions < 0) {
        FILE *file = fopen("kdc-transitions.cfg", "r");
        int value = file ? fgetc(file) : '0';
        if (file) fclose(file);
        transitions = value == '1';
    }
    return transitions != 0;
}

static int IsTransitions(const char *package, const char *feature)
{
    return package && feature && !strcmp(package, "kdc.presentation") &&
           !strcmp(feature, "faster-transitions");
}

bool KdcHdTexturesEnabled(void)
{
    if (hd < 0) {
        FILE *file = fopen("kdc-hd.cfg", "r");
        int value = file ? fgetc(file) : '0';
        if (file) fclose(file);
        hd = value == '1';
    }
    return hd != 0;
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
#ifdef __ANDROID__
    /* ROM selection belongs to the Android system document picker. */
    info->disc_selection_locked = 1;
#endif
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
#ifdef __ANDROID__
    return 6;
#else
    return 7;
#endif
}

static int IsOrbit(const char *package, const char *feature)
{
    return package && feature && !strcmp(package, "kdc.presentation") &&
           !strcmp(feature, "orbit");
}

static int IsHdTextures(const char *package, const char *feature)
{
    return package && feature && !strcmp(package, "kdc.presentation") &&
           !strcmp(feature, "hd_textures");
}

static int IsHdPack(const char *package, const char *feature)
{
#ifdef __ANDROID__
    (void)package;
    (void)feature;
    return 0;
#else
    return package && feature && !strcmp(package, "kdc.presentation") &&
           !strcmp(feature, "hd-interface-pack");
#endif
}

static void InspectHdPack(KdcLauncherSettingsState *state)
{
    state->pack_error = KdcHdPackInspect(&state->pending_pack, &state->pack_available);
}

static const char *HdPackStatus(const KdcLauncherSettingsState *state)
{
    if (state->pack_error) return state->pack_error;
    if (KdcHdPackOverridden()) return "Pack selected by KDC_HD_PACK.";
    if (!state->pending_pack.path[0]) return "No pack selected.";
    if (!state->pack_available) return "No default pack found in the working directory.";
    if (!state->pending_pack.enabled) return "Pack use is off.";
    return "Pack verified.";
}

static int GetResourceCount(void *ctx, const char *package, const char *feature)
{
    (void)ctx;
    return IsHdPack(package, feature);
}

static int GetResource(void *ctx, const char *package, const char *feature,
                       int index, RecompLauncherCModResource *out)
{
    const KdcLauncherSettingsState *state = ctx;
    if (!IsHdPack(package, feature) || index != 0 || !out) return 0;
    *out = (RecompLauncherCModResource){0};
    snprintf(out->id, sizeof(out->id), "pack");
    snprintf(out->label, sizeof(out->label), "Interface pack file");
    snprintf(out->file_patterns, sizeof(out->file_patterns), "*.pack");
    snprintf(out->file_description, sizeof(out->file_description), "HD interface packs");
    _Static_assert(sizeof(out->path) == kKdcHdPackPathMax, "Launcher resource path capacity");
    if (!KdcHdPackOverridden() && (state->pack_available || state->pack_error))
        snprintf(out->path, sizeof(out->path), "%s", state->pending_pack.path);
    snprintf(out->status, sizeof(out->status), "%s%s", HdPackStatus(state),
             state->pack_error ? " Original interface art will be used." : "");
    out->verified = state->pack_available;
    return 1;
}

static int SetResourcePath(void *ctx, const char *package, const char *feature,
                           const char *resource, const char *path)
{
    KdcLauncherSettingsState *state = ctx;
    if (!IsHdPack(package, feature) || !resource || strcmp(resource, "pack") || !path) return 0;
    if (strlen(path) >= sizeof(state->pending_pack.path)) {
        state->last_error = "The HD interface pack path is too long.";
        return 0;
    }
    strcpy(state->pending_pack.path, path);
    state->pack_selected = true;
    InspectHdPack(state);
    state->last_error = NULL;
    return 1;
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
    out->enabled = state->pending_widescreen || state->pending_orbit ||
                   state->pending_high_refresh || state->pending_hd ||
                   state->pending_transitions || state->pending_extra_courses ||
                   (state->pending_pack.enabled && state->pack_available);
    return 1;
}

static int GetFeature(void *ctx, int index, RecompLauncherCModFeature *out)
{
    const KdcLauncherSettingsState *state = ctx;
    if (index < 0 || index >= FeatureCount(ctx) || !out) return 0;
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
    } else if (index == 4) {
        snprintf(out->id, sizeof(out->id), "faster-transitions");
        snprintf(out->name, sizeof(out->name), "Faster hole transitions");
        snprintf(out->group, sizeof(out->group), "Gameplay");
        snprintf(out->description, sizeof(out->description),
                 "Run solo between-hole results at up to 3x speed, muted. "
                 "Confirm scorecards yourself. Normal courses, holes 1-7 only; "
                 "new games only. Gameplay retains its original speed.");
        out->enabled = state->pending_transitions;
        out->option_count = 0;
    } else if (index == 5) {
        snprintf(out->id, sizeof(out->id), "extra-courses");
        snprintf(out->name, sizeof(out->name), "Extra 2P courses");
        snprintf(out->group, sizeof(out->group), "Gameplay");
        KdcCoursePacksDescribeStatus(out->description, sizeof(out->description));
        out->enabled = state->pending_extra_courses;
        out->option_count = 0;
    } else if (index == 3) {
        snprintf(out->id, sizeof(out->id), "hd_textures");
        snprintf(out->name, sizeof(out->name), "HD textures");
        snprintf(out->description, sizeof(out->description),
                 "Redraws course terrain, scenery and sprites at 4x with smoothed "
                 "edges. Checkerboard edges are reconstructed geometrically.");
        out->enabled = state->pending_hd;
        out->option_count = 0;
    } else if (index == 6) {
        snprintf(out->id, sizeof(out->id), "hd-interface-pack");
        snprintf(out->name, sizeof(out->name), "HD interface pack");
        snprintf(out->description, sizeof(out->description),
                 "Use an external .pack file for HUD, menu and font art. "
                 "Requires HD textures. Changes apply on Play or Resume.%s",
                 KdcHdPackOverridden() ? " KDC_HD_PACK overrides the file and pack toggle." : "");
        snprintf(out->status, sizeof(out->status), "%s", HdPackStatus(state));
        out->enabled = state->pending_pack.enabled;
        out->has_error = state->pack_error != NULL;
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
    if (IsExtraCourses(package, feature)) {
        state->pending_extra_courses = enabled != 0;
        return 1;
    }
    if (IsTransitions(package, feature)) {
        state->pending_transitions = enabled != 0;
        return 1;
    }
    if (IsHighRefresh(package, feature)) {
        state->pending_high_refresh = enabled != 0;
        return 1;
    }
    if (IsOrbit(package, feature)) {
        state->pending_orbit = enabled != 0;
        return 1;
    }
    if (IsHdTextures(package, feature)) {
        state->pending_hd = enabled != 0;
        return 1;
    }
    if (IsHdPack(package, feature)) {
        state->pending_pack.enabled = enabled != 0;
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

typedef struct {
    uint64_t size, volume, file_id;
    int64_t modified, changed;
#ifndef _WIN32
    long modified_ns, changed_ns;
#endif
} RomFileStamp;

static bool ReadRomFileStamp(FILE *file, RomFileStamp *stamp)
{
#ifdef _WIN32
    HANDLE handle = (HANDLE)_get_osfhandle(_fileno(file));
    BY_HANDLE_FILE_INFORMATION info;
    FILE_BASIC_INFO times;
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) ||
        !GetFileInformationByHandleEx(handle, FileBasicInfo, &times, sizeof(times))) return false;
    *stamp = (RomFileStamp){
        .size = (uint64_t)info.nFileSizeHigh << 32 | info.nFileSizeLow,
        .volume = info.dwVolumeSerialNumber,
        .file_id = (uint64_t)info.nFileIndexHigh << 32 | info.nFileIndexLow,
        .modified = times.LastWriteTime.QuadPart,
        .changed = times.ChangeTime.QuadPart,
    };
#else
    struct stat info;
    if (fstat(fileno(file), &info) != 0 || !S_ISREG(info.st_mode)) return false;
    *stamp = (RomFileStamp){
        .size = info.st_size,
        .volume = info.st_dev,
        .file_id = info.st_ino,
        .modified = info.st_mtim.tv_sec,
        .changed = info.st_ctim.tv_sec,
        .modified_ns = info.st_mtim.tv_nsec,
        .changed_ns = info.st_ctim.tv_nsec,
    };
#endif
    return true;
}

static bool SameRomFileStamp(const RomFileStamp *a, const RomFileStamp *b)
{
#ifndef _WIN32
    if (a->modified_ns != b->modified_ns || a->changed_ns != b->changed_ns) return false;
#endif
    return a->size == b->size && a->volume == b->volume && a->file_id == b->file_id &&
           a->modified == b->modified && a->changed == b->changed;
}

#ifdef _WIN32
typedef wchar_t RomPathChar;

static RomPathChar *CanonicalRomPath(FILE *file)
{
    HANDLE handle = (HANDLE)_get_osfhandle(_fileno(file));
    DWORD capacity = GetFinalPathNameByHandleW(handle, NULL, 0, FILE_NAME_NORMALIZED);
    RomPathChar *path = capacity ? malloc((size_t)capacity * sizeof(*path)) : NULL;
    if (!path) return NULL;
    DWORD length = GetFinalPathNameByHandleW(handle, path, capacity, FILE_NAME_NORMALIZED);
    if (length && length < capacity) return path;
    free(path);
    return NULL;
}
#else
typedef char RomPathChar;
#endif

static bool SameRomPath(const RomPathChar *a, const RomPathChar *b)
{
#ifdef _WIN32
    return wcscmp(a, b) == 0;
#else
    return strcmp(a, b) == 0;
#endif
}

const char *KdcValidateRomPath(const char *rom, const char *expected_rom_hash)
{
    /* Require the exact raw image at both launcher commit and host startup,
     * including paths that bypass the shared picker's normalized checks. */
    const size_t rom_size = 1048576;
    const char *refusal = "The ROM must be the unmodified headerless 1 MiB Kirby's Dream Course "
                          "USA image. The file is unavailable, or its size or SHA-256 does not match.";
    FILE *file = rom && *rom ? fopen(rom, "rb") : NULL;
    if (!file) return refusal;

    /* The modal launcher and startup validate on the host thread. Keep the
     * last digest in memory and stat the open file to detect edits and replacements. */
    static struct {
        RomPathChar *path;
        RomFileStamp stamp;
        char hash[65];
    } cache;
    RomFileStamp stamp;
    RomPathChar *canonical = NULL;
    if (ReadRomFileStamp(file, &stamp) && stamp.size == rom_size) {
#ifdef _WIN32
        canonical = CanonicalRomPath(file);
#else
        canonical = realpath(rom, NULL);
#endif
    }
    char hash_hex[65] = {0};
    if (canonical && cache.path && SameRomPath(cache.path, canonical) &&
        SameRomFileStamp(&cache.stamp, &stamp)) {
        memcpy(hash_hex, cache.hash, sizeof(hash_hex));
    } else {
        unsigned char *bytes = malloc(rom_size);
        if (bytes && fread(bytes, 1, rom_size, file) == rom_size &&
            fgetc(file) == EOF && !ferror(file)) {
            uint8_t hash[32];
            recompui_sha256_compute(bytes, rom_size, hash);
            for (int i = 0; i < 32; ++i)
                snprintf(hash_hex + i * 2, 3, "%02x", hash[i]);
            RomFileStamp after;
            if (canonical && ReadRomFileStamp(file, &after) && SameRomFileStamp(&stamp, &after)) {
                free(cache.path);
                cache.path = canonical;
                canonical = NULL;
                cache.stamp = stamp;
                memcpy(cache.hash, hash_hex, sizeof(cache.hash));
            }
        }
        free(bytes);
    }
    free(canonical);
    fclose(file);
    return *hash_hex && expected_rom_hash && !strcmp(hash_hex, expected_rom_hash) ? NULL : refusal;
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
    if (state->pending_transitions != KdcFasterTransitionsEnabled()) {
        FILE *file = fopen("kdc-transitions.cfg", "w");
        int ok = file && fprintf(file, "%d\n", state->pending_transitions) > 0;
        if (file && fclose(file) != 0) ok = 0;
        if (!ok) { state->last_error = "Could not save transition settings."; return 0; }
        transitions = state->pending_transitions;
    }
    if (state->pending_extra_courses != KdcExtraCoursesEnabled()) {
        FILE *file = fopen("kdc-courses.cfg", "w");
        int ok = file && fprintf(file, "%d\n", state->pending_extra_courses) > 0;
        if (file && fclose(file) != 0) ok = 0;
        if (!ok) { state->last_error = "Could not save extra course settings."; return 0; }
        extra_courses = state->pending_extra_courses;
        KdcCoursePacksSetEnabled(extra_courses != 0);
    }
    const bool was_hd = KdcHdEnabled();
    if (state->pending_hd != KdcHdTexturesEnabled()) {
        FILE *file = fopen("kdc-hd.cfg", "w");
        int ok = file && fprintf(file, "%d\n", state->pending_hd) > 0;
        if (file && fclose(file) != 0) ok = 0;
        if (!ok) {
            state->last_error = "Could not save HD texture settings.";
            return 0;
        }
        hd = state->pending_hd;
    }
    const bool pack_changed = state->saved_pack.enabled != state->pending_pack.enabled ||
                              strcmp(state->saved_pack.path, state->pending_pack.path);
    const bool pack_saved = !pack_changed || KdcHdPackSaveSettings(&state->pending_pack);
    if (pack_saved) state->saved_pack = state->pending_pack;
    if (KdcHdEnabled()) {
        const bool pack_allowed = state->saved_pack.enabled || KdcHdPackOverridden();
        bool reload_pack = pack_changed || state->pack_selected;
#ifndef __ANDROID__
        /* A file may have disappeared or become available between visits. */
        reload_pack |= KdcHdPackAvailable() != (pack_allowed && state->pack_available);
#endif
        if (pack_saved && reload_pack) {
            const char *error = KdcHdPackReload(&state->saved_pack);
            if (pack_allowed) {
                state->pack_error = error;
                state->pack_available = KdcHdPackAvailable();
            }
        } else if (!was_hd) {
            KdcHdPackPrewarm();
        }
    } else if (was_hd) {
        KdcHdPackUnload();
    }
    if (!pack_saved) {
        state->last_error = "Could not save HD interface pack settings.";
        fprintf(stderr, "[hd] %s\n", state->last_error);
        return 0;
    }
    state->pack_selected = false;
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
        .feature_resource_count = GetResourceCount,
        .feature_resource_get = GetResource,
        .feature_resource_set_path = SetResourcePath,
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
        .pending_hd = KdcHdTexturesEnabled(),
        .pending_transitions = KdcFasterTransitionsEnabled(),
        .pending_extra_courses = KdcExtraCoursesEnabled(),
        .expected_rom_hash = rom_sha256,
    };
#ifndef __ANDROID__
    state.saved_pack = KdcHdPackReadSettings();
    state.pending_pack = state.saved_pack;
    InspectHdPack(&state);
#endif
    return &provider;
}
