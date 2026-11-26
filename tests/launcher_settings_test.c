#include "launcher_settings.h"
#include "hd_pack.h"
#include "hd_textures.h"
#include "recomp_launcher.h"
#include "common/sha256.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static unsigned rom_hashes;
static const char package_id[] = "kdc.presentation";
static const char pack_id[] = "hd-interface-pack";
static const char pack_path[] = "interface \xc3\xa9.pack";

void __real_recompui_sha256_compute(const uint8_t *data, size_t size, uint8_t out[32]);

void __wrap_recompui_sha256_compute(const uint8_t *data, size_t size, uint8_t out[32])
{
    ++rom_hashes;
    __real_recompui_sha256_compute(data, size, out);
}

bool KdcHdEnabled(void) { return KdcHdTexturesEnabled(); }

int main(int argc, char **argv)
{
    CHECK(argc == 5 || argc == 6 || argc == 7);
    if (argc == 7) {
        CHECK(KdcFasterTransitionsEnabled());
        CHECK(KdcExtraCoursesEnabled());
        CHECK(KdcWidescreenAspect() == 2);
        CHECK(KdcHudEdges());
        CHECK(KdcHighRefreshEnabled() && KdcPresentationFps() == 144);
        CHECK(!KdcHdTexturesEnabled());
        bool widescreen = false;
        const RecompLauncherCModProvider *provider = KdcLauncherSettings(&widescreen, argv[2]);
        RecompLauncherCModResource resource;
        CHECK(provider->feature_resource_get(provider->ctx, package_id, pack_id, 0, &resource));
        if (!strcmp(argv[5], "reload")) {
            CHECK(resource.verified && !strcmp(resource.path, pack_path));
            CHECK(!KdcHdPackAvailable());
            KdcHdPackPrewarm();
            CHECK(KdcHdPackAvailable());
        } else if (!strcmp(argv[5], "clear")) {
            CHECK(provider->feature_resource_set_path(provider->ctx, package_id, pack_id, "pack", ""));
            CHECK(provider->commit(provider->ctx, argv[1]));
            CHECK(provider->feature_resource_get(provider->ctx, package_id, pack_id, 0, &resource));
            CHECK(!resource.path[0] && !resource.verified);
        } else {
            CHECK(!strcmp(argv[5], "override"));
            CHECK(resource.verified && !resource.path[0]);
            CHECK(strstr(resource.status, "KDC_HD_PACK"));
            CHECK(provider->feature_enable(provider->ctx, package_id, pack_id, 0));
            CHECK(provider->feature_enable(provider->ctx, package_id, "hd_textures", 1));
            CHECK(provider->commit(provider->ctx, argv[1]));
            CHECK(KdcHdPackAvailable());
        }
        return 0;
    }
    RecompLauncherCGameInfo info = {.has_run_ahead = 1, .has_renderer = 1};
    KdcConfigureLauncher(&info);
    CHECK(!info.has_run_ahead && info.has_renderer && !info.audio_frequency_locked);
    CHECK(!info.disc_selection_locked);
    info.in_session = 1;
    KdcConfigureLauncher(&info);
    CHECK(!info.has_renderer && info.audio_frequency_locked && info.disc_selection_locked);
    const char *rom = argv[1], *hash = argv[2];
    RecompLauncherCModFeature feature;
    RecompLauncherCModPackage package;
    RecompLauncherCModOption option;
    RecompLauncherCModChoice choice;
    bool widescreen = false;
    const RecompLauncherCModProvider *provider = KdcLauncherSettings(&widescreen, hash);
    if (argc == 6) {
        /* Simulate a verified ROM disappearing after the game has started. */
        CHECK(provider->commit(provider->ctx, rom));
        CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "widescreen", 1));
        CHECK(remove(rom) == 0);
        CHECK(!provider->commit(provider->ctx, rom));
        CHECK(provider->last_error(provider->ctx));
        CHECK(!widescreen);
        return 0;
    }
    /* The real launcher's feature UI requires all of these callbacks. */
    CHECK(provider->feature_get && provider->feature_count &&
           provider->feature_enable && provider->feature_option_get &&
           provider->feature_set_option);
    CHECK(!provider->feature_option_get(provider->ctx, "kdc.presentation", "widescreen", 0, NULL));
    CHECK(!provider->feature_set_option(provider->ctx, "kdc.presentation", "widescreen", "invalid", "1"));
    CHECK(!KdcHudEdges());
    CHECK(provider->feature_option_get(provider->ctx, "kdc.presentation", "widescreen", 1, &option));
    CHECK(option.choice_count == 2 && !strcmp(option.value, "centered"));
    CHECK(!provider->feature_set_option(provider->ctx, "kdc.presentation", "widescreen", "hud", "unknown"));
    CHECK(provider->feature_set_option(provider->ctx, "kdc.presentation", "widescreen", "hud", "edges"));
    CHECK(!KdcHudEdges());
    CHECK(provider->feature_count(provider->ctx) == 7);
    CHECK(!KdcExtraCoursesEnabled());
    CHECK(provider->feature_get(provider->ctx, 5, &feature));
    CHECK(!strcmp(feature.id, "extra-courses") && !feature.enabled && !feature.option_count);
    CHECK(feature.channel == RECOMP_MOD_CHANNEL_EXPERIMENTAL);
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "extra-courses", 1));
    CHECK(!KdcExtraCoursesEnabled());
    CHECK(!KdcFasterTransitionsEnabled());
    CHECK(provider->feature_get(provider->ctx, 4, &feature));
    CHECK(!strcmp(feature.id, "faster-transitions") && !feature.enabled && !feature.option_count);
    CHECK(feature.channel == RECOMP_MOD_CHANNEL_EXPERIMENTAL);
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "faster-transitions", 1));
    CHECK(!KdcFasterTransitionsEnabled());
    CHECK(!KdcHighRefreshEnabled());
    CHECK(provider->feature_get(provider->ctx, 2, &feature));
    CHECK(!feature.enabled && !strcmp(feature.id, "high-refresh") && feature.option_count == 1);
    CHECK(provider->feature_option_get(provider->ctx, "kdc.presentation", "high-refresh", 0, &option));
    CHECK(option.choice_count == 8 && !strcmp(option.value, "display"));
    CHECK(!provider->feature_set_option(provider->ctx, "kdc.presentation", "high-refresh", "fps", "999"));
    CHECK(provider->feature_set_option(provider->ctx, "kdc.presentation", "high-refresh", "fps", "144"));
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "high-refresh", 1));
    CHECK(!KdcHighRefreshEnabled()); /* staging does not change the clock */
    CHECK(provider->feature_option_get(provider->ctx, "kdc.presentation", "widescreen", 0, &option));
    CHECK(option.choice_count == 4 && !strcmp(option.value, "auto"));
    CHECK(provider->feature_choice_get(provider->ctx, "kdc.presentation", "widescreen", "aspect", 2, &choice));
    CHECK(!strcmp(choice.value, "21:9"));
    CHECK(!provider->feature_choice_get(provider->ctx, "kdc.presentation", "widescreen", "aspect", 4, &choice));
    CHECK(provider->feature_set_option(provider->ctx, "kdc.presentation", "widescreen", "aspect", "21:9"));
    CHECK(KdcWidescreenAspect() == 0);
    CHECK(provider->feature_get(provider->ctx, 0, &feature));
    CHECK(!feature.enabled);
    CHECK(feature.channel == RECOMP_MOD_CHANNEL_EXPERIMENTAL);
    CHECK(provider->package_get(provider->ctx, 0, &package));
    CHECK(!strcmp(feature.package_id, package.id));
    CHECK(provider->feature_get(provider->ctx, 1, &feature));
    CHECK(!strcmp(feature.id, "orbit") && feature.enabled && !feature.option_count);
    CHECK(feature.channel == RECOMP_MOD_CHANNEL_EXPERIMENTAL);
    CHECK(KdcOrbitEnabled());
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "orbit", 0));
    CHECK(KdcOrbitEnabled()); /* staged, not applied */
    CHECK(provider->feature_get(provider->ctx, 3, &feature));
    CHECK(!strcmp(feature.id, "hd_textures") && !feature.enabled && !feature.option_count);
    CHECK(!strcmp(feature.package_id, package.id));
    CHECK(feature.channel == RECOMP_MOD_CHANNEL_EXPERIMENTAL);
    CHECK(!KdcHdTexturesEnabled()); /* off by default */
    CHECK(!provider->feature_option_get(provider->ctx, "kdc.presentation", "hd_textures", 0, &option));
    CHECK(!provider->feature_choice_get(provider->ctx, "kdc.presentation", "hd_textures", "ui", 0, &choice));
    CHECK(!provider->feature_set_option(provider->ctx, "kdc.presentation", "hd_textures", "ui", "original"));
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "hd_textures", 1));
    CHECK(!KdcHdTexturesEnabled()); /* staged, not applied */
    CHECK(!KdcHdPackAvailable());
    CHECK(provider->feature_get(provider->ctx, 6, &feature));
    CHECK(!strcmp(feature.id, pack_id) && feature.enabled);
    CHECK(provider->feature_resource_count(provider->ctx, package_id, pack_id) == 1);
    RecompLauncherCModResource resource;
    CHECK(provider->feature_resource_get(provider->ctx, package_id, pack_id, 0, &resource));
    CHECK(!strcmp(resource.file_patterns, "*.pack") && !resource.required && !resource.verified);
    CHECK(provider->feature_resource_set_path(provider->ctx, package_id, pack_id, "pack", pack_path));
    CHECK(provider->feature_resource_get(provider->ctx, package_id, pack_id, 0, &resource));
    CHECK(resource.verified && !strcmp(resource.path, pack_path));
    CHECK(!KdcHdPackAvailable());
    CHECK(!provider->feature_get(provider->ctx, 7, &feature));
    CHECK(!provider->feature_get(provider->ctx, 0, NULL));
    CHECK(!provider->feature_enable(provider->ctx, "foreign", "widescreen", 1));
    CHECK(!provider->feature_enable(provider->ctx, NULL, NULL, 1));

    /* Editing and dismissing a startup launcher must not apply a preview. */
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "widescreen", 1));
    CHECK(provider->feature_set_option(provider->ctx, "kdc.presentation", "widescreen", "aspect", "21:9"));
    CHECK(!widescreen);
    CHECK(KdcWidescreenAspect() == 0);
    provider = KdcLauncherSettings(&widescreen, hash);
    CHECK(provider->feature_resource_get(provider->ctx, package_id, pack_id, 0, &resource));
    CHECK(!resource.verified && !resource.path[0] && strstr(resource.status, "No default pack"));
    CHECK(provider->feature_get(provider->ctx, 5, &feature));
    CHECK(!feature.enabled); /* dismissed extra-courses edit */
    CHECK(provider->feature_get(provider->ctx, 4, &feature));
    CHECK(!feature.enabled); /* dismissed transition edit */
    CHECK(provider->feature_get(provider->ctx, 1, &feature));
    CHECK(feature.enabled); /* dismiss discarded the camera change */
    CHECK(provider->feature_get(provider->ctx, 3, &feature));
    CHECK(!feature.enabled); /* ... and the HD textures change */
    CHECK(provider->feature_get(provider->ctx, 0, &feature));
    CHECK(!feature.enabled);
    CHECK(provider->feature_option_get(provider->ctx, "kdc.presentation", "widescreen", 0, &option));
    CHECK(!strcmp(option.value, "auto"));

    CHECK(provider->feature_option_get(provider->ctx, "kdc.presentation", "widescreen", 1, &option));
    CHECK(!strcmp(option.value, "centered"));
    CHECK(provider->feature_set_option(provider->ctx, "kdc.presentation", "widescreen", "hud", "edges"));
    /* PLAY/RESUME applies the setting that the host will persist. */
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "widescreen", 1));
    CHECK(provider->feature_set_option(provider->ctx, "kdc.presentation", "widescreen", "aspect", "21:9"));
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "orbit", 0));
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "hd_textures", 1));
    CHECK(provider->feature_resource_set_path(provider->ctx, package_id, pack_id, "pack", pack_path));
    CHECK(!provider->commit(provider->ctx, "missing.sfc"));
    CHECK(!KdcFasterTransitionsEnabled());
    CHECK(!KdcHdTexturesEnabled());
    CHECK(provider->last_error(provider->ctx));
    CHECK(!widescreen);
    CHECK(!provider->commit(provider->ctx, argv[3])); /* copier header */
    CHECK(!provider->commit(provider->ctx, argv[4])); /* same-size wrong hash */
    CHECK(!widescreen);
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(KdcHdPackAvailable());
    const unsigned hashes_after_commit = rom_hashes;
    CHECK(!KdcValidateRomPath("./synthetic.sfc", hash));
    CHECK(KdcValidateRomPath("synthetic.sfc", "different expected hash"));
    CHECK(rom_hashes == hashes_after_commit);
    CHECK(widescreen);
    CHECK(KdcHudEdges());
    CHECK(!KdcOrbitEnabled());
    CHECK(KdcHdTexturesEnabled());
    CHECK(KdcWidescreenAspect() == 2);
    CHECK(!provider->last_error(provider->ctx));
    provider = KdcLauncherSettings(&widescreen, hash);
    const uint64_t first_revision = KdcHdPackRevision();
    CHECK(provider->feature_resource_set_path(provider->ctx, package_id, pack_id, "pack", pack_path));
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(KdcHdPackAvailable() && KdcHdPackRevision() != first_revision);
    CHECK(provider->feature_enable(provider->ctx, package_id, pack_id, 0));
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(!KdcHdPackAvailable() && KdcHdTexturesEnabled());
    CHECK(provider->feature_enable(provider->ctx, package_id, pack_id, 1));
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(KdcHdPackAvailable());
    CHECK(provider->feature_resource_set_path(provider->ctx, package_id, pack_id, "pack", "vanishing.pack"));
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(KdcHdPackAvailable());
    CHECK(remove("vanishing.pack") == 0);
    provider = KdcLauncherSettings(&widescreen, hash);
    CHECK(provider->feature_get(provider->ctx, 6, &feature) && feature.has_error);
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(!KdcHdPackAvailable());
    CHECK(provider->feature_resource_set_path(provider->ctx, package_id, pack_id, "pack", pack_path));
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(provider->feature_resource_set_path(provider->ctx, package_id, pack_id, "pack", "missing.pack"));
    CHECK(provider->feature_resource_get(provider->ctx, package_id, pack_id, 0, &resource));
    CHECK(!resource.verified && strstr(resource.status, "not found"));
    CHECK(provider->feature_resource_set_path(provider->ctx, package_id, pack_id, "pack", "unsupported.pack"));
    CHECK(provider->feature_resource_get(provider->ctx, package_id, pack_id, 0, &resource));
    CHECK(!resource.verified && strstr(resource.status, "Unsupported"));
    CHECK(provider->feature_resource_set_path(provider->ctx, package_id, pack_id, "pack", "malformed.pack"));
    CHECK(provider->feature_get(provider->ctx, 6, &feature) && feature.has_error);
    CHECK(KdcHdPackAvailable()); /* Staging a bad file leaves active tiles alone. */
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(!KdcHdPackAvailable() && KdcHdTexturesEnabled());
    CHECK(provider->feature_resource_set_path(provider->ctx, package_id, pack_id, "pack", pack_path));
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(KdcHdPackAvailable());
    CHECK(provider->feature_get(provider->ctx, 0, &feature));
    CHECK(feature.enabled);
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "widescreen", 0));
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(!widescreen);
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "orbit", 1));
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(KdcOrbitEnabled());
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "high-refresh", 1));
    CHECK(provider->feature_set_option(provider->ctx, "kdc.presentation", "high-refresh", "fps", "144"));
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(KdcHighRefreshEnabled() && KdcPresentationFps() == 144);
    CHECK(provider->feature_get(provider->ctx, 3, &feature));
    CHECK(feature.enabled);
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "hd_textures", 0));
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(!KdcHdTexturesEnabled());
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "faster-transitions", 1));
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(KdcFasterTransitionsEnabled());
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "extra-courses", 1));
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(KdcExtraCoursesEnabled());

    /* Replace the ROM with a different image of the same size and mtime. */
    CHECK(remove(rom) == 0);
    CHECK(rename(argv[4], rom) == 0);
    CHECK(KdcValidateRomPath(rom, hash));
    CHECK(rom_hashes == hashes_after_commit + 1);
    /* An in-place edit restores the expected bytes and updates the mtime. */
    FILE *file = fopen(rom, "r+b");
    CHECK(file && fseek(file, -1, SEEK_END) == 0 && fputc(0, file) == 0);
    CHECK(fclose(file) == 0);
    CHECK(!KdcValidateRomPath(rom, hash));
    CHECK(rom_hashes == hashes_after_commit + 2);
    file = fopen(rom, "ab");
    CHECK(file && fputc(0, file) == 0 && fclose(file) == 0);
    CHECK(KdcValidateRomPath(rom, hash));
    CHECK(rom_hashes == hashes_after_commit + 2);
    return 0;
}
