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
    CHECK(argc == 5 || argc == 6 || argc == 7);
    if (argc == 7) {
        CHECK(KdcWidescreenAspect() == 2);
        CHECK(KdcHighRefreshEnabled() && KdcPresentationFps() == 144);
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
    CHECK(provider->feature_count(provider->ctx) == 3);
    CHECK(!KdcHighRefreshEnabled());
    CHECK(provider->feature_get(provider->ctx, 2, &feature));
    CHECK(!feature.enabled && !strcmp(feature.id, "high-refresh"));
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
    CHECK(!provider->feature_get(provider->ctx, 3, &feature));
    CHECK(!provider->feature_get(provider->ctx, 0, NULL));
    CHECK(!provider->feature_enable(provider->ctx, "foreign", "widescreen", 1));
    CHECK(!provider->feature_enable(provider->ctx, NULL, NULL, 1));

    /* Editing and dismissing a startup launcher must not apply a preview. */
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "widescreen", 1));
    CHECK(provider->feature_set_option(provider->ctx, "kdc.presentation", "widescreen", "aspect", "21:9"));
    CHECK(!widescreen);
    CHECK(KdcWidescreenAspect() == 0);
    provider = KdcLauncherSettings(&widescreen, hash);
    CHECK(provider->feature_get(provider->ctx, 1, &feature));
    CHECK(feature.enabled); /* dismiss discarded the camera change */
    CHECK(provider->feature_get(provider->ctx, 0, &feature));
    CHECK(!feature.enabled);
    CHECK(provider->feature_option_get(provider->ctx, "kdc.presentation", "widescreen", 0, &option));
    CHECK(!strcmp(option.value, "auto"));

    /* PLAY/RESUME applies the setting that the host will persist. */
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "widescreen", 1));
    CHECK(provider->feature_set_option(provider->ctx, "kdc.presentation", "widescreen", "aspect", "21:9"));
    CHECK(provider->feature_enable(provider->ctx, "kdc.presentation", "orbit", 0));
    CHECK(!provider->commit(provider->ctx, "missing.sfc"));
    CHECK(provider->last_error(provider->ctx));
    CHECK(!widescreen);
    CHECK(!provider->commit(provider->ctx, argv[3])); /* copier header */
    CHECK(!provider->commit(provider->ctx, argv[4])); /* same-size wrong hash */
    CHECK(!widescreen);
    CHECK(provider->commit(provider->ctx, rom));
    CHECK(widescreen);
    CHECK(!KdcOrbitEnabled());
    CHECK(KdcWidescreenAspect() == 2);
    CHECK(!provider->last_error(provider->ctx));
    provider = KdcLauncherSettings(&widescreen, hash);
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
    return 0;
}
