#include "runtime.h"
#include "model_catalog_selection.h"
#include "model_import.h"
#include "model_import_lock.h"
#include "model_storage.h"
#include "bongo_cat/path.h"
#include "bongo_cat/preferences.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BongoCatModelCatalog *model_catalog_snapshot(
    const BongoCatModelCatalog *models) {
    if (!models) return NULL;
    BongoCatModelCatalog *snapshot = malloc(sizeof(*snapshot));
    if (snapshot) *snapshot = *models;
    return snapshot;
}

static void restore_model_catalog(BongoCatApp *app,
    const BongoCatModelCatalog *snapshot, const char *active_model_id) {
    if (!app || !snapshot) return;
    app->models = *snapshot;
    if (active_model_id)
        snprintf(app->session.active_model_id,
            sizeof(app->session.active_model_id), "%s", active_model_id);
}

static BongoCatResult scan_nearby_root(BongoCatApp *app, const char *root) {
    if (!root || !root[0] || !bongo_cat_path_is_dir(root))
        return BONGO_CAT_OK;
    size_t before = app->models.count;
    BongoCatError error = {0};
    BongoCatResult result = bongo_cat_import_nearby_scan(app, root,
        &error);
    size_t added = app->models.count - before;
    if (added) SDL_Log("Nearby model scan added %llu models from %s",
        (unsigned long long)added, root);
    if (result != BONGO_CAT_OK && error.message[0])
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
    return result;
}

bool bongo_cat_model_catalog_add_bundled(BongoCatApp *app, bool replace) {
    char root[BONGO_CAT_PATH_CAP];
    if (!app || !bongo_cat_path_join(root, sizeof(root), app->asset_root,
            "models")) return false;
    BongoCatModelCatalog *bundled = calloc(1, sizeof(*bundled));
    if (!bundled) return false;
    BongoCatError error = {0};
    BongoCatResult result = bongo_cat_models_scan(bundled, root, true, &error);
    for (size_t i = 0; i < bundled->count; ++i) {
        const BongoCatModelEntry *entry = &bundled->entries[i];
        size_t index = 0;
        while (index < app->models.count &&
            strcmp(app->models.entries[index].id, entry->id)) index++;
        if (index < app->models.count && !replace) continue;
        if (index >= BONGO_CAT_MODEL_CAP) continue;
        app->models.entries[index] = *entry;
        if (index == app->models.count) app->models.count++;
    }
    bool available = bundled->count > 0 && result == BONGO_CAT_OK;
    if (!available) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Bundled model scan failed: path=%s error=%s", root,
        error.message[0] ? error.message : "no bundled models found");
    free(bundled);
    return available;
}

static BongoCatResult scan_owned_models(BongoCatApp *app, bool cleanup) {
    BongoCatError error = {0};
    bongo_cat_models_init(&app->models);
    bongo_cat_import_storage_lock();
    BongoCatResult result = bongo_cat_model_install_builtins(app->asset_root,
        app->models_root, &error);
    if (result != BONGO_CAT_OK)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Built-in model installation failed; using available models: "
            "path=%s error=%s", app->models_root, error.message);
    error = (BongoCatError){0};
    if (cleanup && !bongo_cat_model_cleanup_imports(app->models_root, &error))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Model cleanup failed; continuing scan: path=%s error=%s",
            app->models_root, error.message);
    error = (BongoCatError){0};
    result = bongo_cat_models_scan(&app->models, app->models_root, false,
        &error);
    bongo_cat_import_storage_unlock();
    if (result != BONGO_CAT_OK) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Stored built-in model scan incomplete: path=%s error=%s",
        app->models_root, error.message);
    /* Establish fallback entries before optional discovery can fail or fill
       the catalog. Bundled assets do not depend on writable model storage. */
    bongo_cat_model_catalog_add_bundled(app, false);
    error = (BongoCatError){0};
    result = bongo_cat_import_installed_models(app, app->models_root, &error);
    if (result != BONGO_CAT_OK)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Installed model scan incomplete: path=%s result=%d error=%s",
            app->models_root, (int)result,
            error.message[0] ? error.message : "scan unavailable");
    return app->models.count ? BONGO_CAT_OK :
        (result != BONGO_CAT_OK ? result : BONGO_CAT_ERROR_IO);
}

void bongo_cat_model_catalog_finish(BongoCatApp *app) {
    if (!app) return;
    /* Absence from a partial scan does not mean the user deleted a model. */
    bool selection_changed = bongo_cat_model_catalog_reconcile(app);
    for (size_t i = 0; i < app->models.count; ++i)
        bongo_cat_import_apply_metadata(app, app->models.entries[i].id,
            app->models.entries[i].adapter_directory);
    if (selection_changed && app->preferences) bongo_cat_preferences_models_changed(app->preferences);
}

BongoCatResult bongo_cat_model_catalog_scan(BongoCatApp *app, bool cleanup,
    const char *nearby_root) {
    if (!app) return BONGO_CAT_ERROR_ARGUMENT;
    BongoCatResult result = scan_owned_models(app, cleanup);
    if (nearby_root && !SDL_getenv("BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN")) {
        /* Nearby folders are an optional convenience (the default root is
           often the folder from which the portable executable was launched).
           A malformed third-party model must not make the built-in catalog
           disappear: the scan can add valid entries before it reports the
           malformed one. Keep those entries and let startup continue. */
        BongoCatResult nearby = scan_nearby_root(app, nearby_root);
        if (nearby != BONGO_CAT_OK)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Nearby model scan was incomplete; continuing with %llu "
                "usable model(s)",
                (unsigned long long)app->models.count);
    }
    return app->models.count ? BONGO_CAT_OK : result;
}

void bongo_cat_app_rescan_models(BongoCatApp *app) {
    if (!app) return;
    bongo_cat_model_refresh_invalidate(app);
    BongoCatModelCatalog *previous = model_catalog_snapshot(&app->models);
    if (!previous) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Model catalog rescan skipped: cannot allocate catalog snapshot");
        return;
    }
    char active_model_id[BONGO_CAT_ID_CAP];
    snprintf(active_model_id, sizeof(active_model_id), "%s",
        app->session.active_model_id);
    BongoCatResult result = bongo_cat_model_catalog_scan(app, true,
        app->nearby_root);
    if (result != BONGO_CAT_OK) {
        restore_model_catalog(app, previous, active_model_id);
        free(previous);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Model catalog rescan failed; keeping previous catalog");
        return;
    }
    free(previous);
    bongo_cat_model_catalog_finish(app);
}

void bongo_cat_app_refresh_installed_models(BongoCatApp *app) {
    if (!app) return;
    bongo_cat_model_refresh_invalidate(app);
    BongoCatModelCatalog *previous = model_catalog_snapshot(&app->models);
    if (!previous) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Installed model refresh skipped: cannot allocate catalog snapshot");
        return;
    }
    BongoCatResult result = scan_owned_models(app, false);
    if (result != BONGO_CAT_OK) {
        restore_model_catalog(app, previous, NULL);
        free(previous);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Installed model refresh failed; keeping previous catalog");
        return;
    }
    for (size_t i = 0; i < previous->count &&
        app->models.count < BONGO_CAT_MODEL_CAP; ++i) {
        const BongoCatModelEntry *entry = &previous->entries[i];
        if (!entry->managed || bongo_cat_models_find(&app->models, entry->id))
            continue;
        app->models.entries[app->models.count++] = *entry;
    }
    free(previous);
    bongo_cat_model_catalog_finish(app);
}

void bongo_cat_app_refresh_nearby_models(BongoCatApp *app) {
    if (!app || SDL_getenv("BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN")) return;
    bongo_cat_model_refresh_invalidate(app);
    BongoCatModelCatalog *previous = model_catalog_snapshot(&app->models);
    if (!previous) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Nearby model refresh skipped: cannot allocate catalog snapshot");
        return;
    }
    char active_model_id[BONGO_CAT_ID_CAP];
    snprintf(active_model_id, sizeof(active_model_id), "%s",
        app->session.active_model_id);
    BongoCatResult result = bongo_cat_model_catalog_scan(app, false,
        app->nearby_root);
    if (result != BONGO_CAT_OK) {
        restore_model_catalog(app, previous, active_model_id);
        free(previous);
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Nearby model refresh failed; keeping previous catalog");
        return;
    }
    free(previous);
    snprintf(app->session.active_model_id,
        sizeof(app->session.active_model_id), "%s", active_model_id);
    bongo_cat_model_catalog_finish(app);
}
