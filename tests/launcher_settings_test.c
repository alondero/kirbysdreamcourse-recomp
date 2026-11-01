#include "launcher_settings.h"
#include "recomp_launcher.h"

#include <assert.h>
#include <string.h>

int main(int argc, char **argv)
{
    assert(argc == 5);
    const char *rom = argv[1], *hash = argv[2];
    RecompLauncherCModFeature feature;
    RecompLauncherCModPackage package;
    bool widescreen = false;
    const RecompLauncherCModProvider *provider = KdcLauncherSettings(&widescreen, hash);
    /* The real launcher's feature UI requires all of these callbacks. */
    assert(provider->feature_get && provider->feature_count &&
           provider->feature_enable && provider->feature_option_get &&
           provider->feature_set_option);
    assert(!provider->feature_option_get(provider->ctx, "kdc.presentation", "widescreen", 0, NULL));
    assert(!provider->feature_set_option(provider->ctx, "kdc.presentation", "widescreen", "invalid", "1"));
    assert(provider->feature_count(provider->ctx) == 1);
    assert(provider->feature_get(provider->ctx, 0, &feature));
    assert(!feature.enabled);
    assert(feature.channel == RECOMP_MOD_CHANNEL_EXPERIMENTAL);
    assert(provider->package_get(provider->ctx, 0, &package));
    assert(!strcmp(feature.package_id, package.id));
    assert(!provider->feature_get(provider->ctx, 1, &feature));
    assert(!provider->feature_get(provider->ctx, 0, NULL));
    assert(!provider->feature_enable(provider->ctx, "foreign", "widescreen", 1));
    assert(!provider->feature_enable(provider->ctx, NULL, NULL, 1));

    /* Editing and dismissing a startup launcher must not apply a preview. */
    assert(provider->feature_enable(provider->ctx, "kdc.presentation", "widescreen", 1));
    assert(!widescreen);
    provider = KdcLauncherSettings(&widescreen, hash);
    assert(provider->feature_get(provider->ctx, 0, &feature));
    assert(!feature.enabled);

    /* PLAY/RESUME applies the setting that the host will persist. */
    assert(provider->feature_enable(provider->ctx, "kdc.presentation", "widescreen", 1));
    assert(!provider->commit(provider->ctx, "missing.sfc"));
    assert(provider->last_error(provider->ctx));
    assert(!widescreen);
    assert(!provider->commit(provider->ctx, argv[3])); /* copier header */
    assert(!provider->commit(provider->ctx, argv[4])); /* same-size wrong hash */
    assert(!widescreen);
    assert(provider->commit(provider->ctx, rom));
    assert(widescreen);
    assert(!provider->last_error(provider->ctx));
    provider = KdcLauncherSettings(&widescreen, hash);
    assert(provider->feature_get(provider->ctx, 0, &feature));
    assert(feature.enabled);
    assert(provider->feature_enable(provider->ctx, "kdc.presentation", "widescreen", 0));
    assert(provider->commit(provider->ctx, rom));
    assert(!widescreen);
    return 0;
}
