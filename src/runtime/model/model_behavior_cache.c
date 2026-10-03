#include "model_behavior_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool cacheable(const BongoCatModelEntry *entry) {
    /* Installed Mver trees keep a stable content digest. Nearby models are
       managed by their source directory, so do not retain stale data. */
    return entry && (entry->preset || (!entry->managed &&
        entry->content_digest[0]));
}

bool bongo_cat_model_behavior_cache_matches(const BongoCatApp *app,
    const BongoCatModelEntry *entry) {
    return app && app->behavior_cache && app->behavior_cache_valid &&
        cacheable(entry) &&
        strcmp(app->behavior_cache_model_id, entry->id) == 0 &&
        (entry->preset || strcmp(app->behavior_cache_digest,
            entry->content_digest) == 0);
}

void bongo_cat_model_behavior_cache_store(BongoCatApp *app,
    const BongoCatModelEntry *entry) {
    if (!app || !cacheable(entry)) {
        if (app) app->behavior_cache_valid = false;
        return;
    }
    if (!app->behavior_cache)
        app->behavior_cache = calloc(1, sizeof(*app->behavior_cache));
    if (!app->behavior_cache) {
        app->behavior_cache_valid = false;
        return;
    }
    if (!bongo_cat_behaviors_copy(app->behavior_cache, &app->behaviors, NULL)) {
        app->behavior_cache_valid = false;
        return;
    }
    snprintf(app->behavior_cache_model_id,
        sizeof(app->behavior_cache_model_id), "%s", entry->id);
    snprintf(app->behavior_cache_digest, sizeof(app->behavior_cache_digest),
        "%s", entry->content_digest);
    app->behavior_cache_valid = true;
}
