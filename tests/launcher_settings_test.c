#include "launcher_settings.h"
#include "recomp_launcher.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

int main(int argc, char **argv)
{
    CHECK(argc == 5);
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
    bool widescreen = false;
    const RecompLauncherCModProvider *provider = KdcLauncherSettings(&widescreen, hash);
    /* The real launcher's feature UI requires all of these callbacks. */
    CHECK(provider->feature_get && provider->feature_count &&
           provider->feature_enable && provider->feature_option_get &&
           provider->feature_set_option);
    CHECK(!provider->feature_option_get(provider->ctx, "kdc.presentation", "widescreen", 0, NULL));
    CHECK(!provider->feature_set_option(provider->ctx, "kdc.presentation", "widescreen", "invalid", "1"));
    CHECK(provider->feature_count(provider->ctx) == 1);
    CHECK(provider->feature_get(provider->ctx, 0, &feature));
    CHECK(!feature.enabled);
    CHECK(feature.channel == RECOMP_MOD_CHANNEL_EXPERIMENTAL);
    CHECK(provider->package_get(provider->ctx, 0, &package));
    CHECK(!strcmp(feature.package_id, package.id));
    CHECK(!provider->feature_get(provider->ctx, 1, &feature));
    CHECK(!provider->feature_get(provider->ctx, 0, NULL));
    CHECK(!provider->feature_enable(provider->ctx, "foreign", "widescreen", 1));
    CHECK(!provider->feature_enable(provider->ctx, NULL, NULL, 1));

    /* Editing and dismissing a startup launcher must not apply a preview. */
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "widescreen", 1));
    CHECK(!widescreen);
    provider = KdcLauncherSettings(&widescreen, hash);
    CHECK(provider->feature_get(provider->ctx, 0, &feature));
    CHECK(!feature.enabled);

    /* PLAY/RESUME applies the setting that the host will persist. */
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "widescreen", 1));
    CHECK(!provider->commit(provider->ctx, "missing.sfc"));
    CHECK(provider->last_error(provider->ctx));
    CHECK(!widescreen);
    CHECK(!provider->commit(provider->ctx, argv[3])); /* copier header */
    CHECK(!provider->commit(provider->ctx, argv[4])); /* same-size wrong hash */
    CHECK(!widescreen);
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(widescreen);
    CHECK(!provider->last_error(provider->ctx));
    provider = KdcLauncherSettings(&widescreen, hash);
    CHECK(provider->feature_get(provider->ctx, 0, &feature));
    CHECK(feature.enabled);
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "widescreen", 0));
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(!widescreen);
    return 0;
}
