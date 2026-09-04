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

static bool shortcut_model_exists(const BongoCatModelCatalog *models,
    const char *shortcut_id) {
    const char *separator = shortcut_id ? strchr(shortcut_id, ':') : NULL;
    if (!separator) return false;
    size_t length = (size_t)(separator - shortcut_id);
    for (size_t i = 0; i < models->count; ++i) {
        const char *id = models->entries[i].id;
        if (strlen(id) == length && !strncmp(id, shortcut_id, length))
            return true;
    }
    return false;
}

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

static void prune_behavior_shortcuts(BongoCatApp *app) {
    size_t output = 0;
    for (size_t i = 0; i < app->settings.behavior_shortcut_count; ++i) {
        BongoCatBehaviorShortcut *value = &app->settings.behavior_shortcuts[i];
        if (!shortcut_model_exists(&app->models, value->id)) continue;
        if (output != i) app->settings.behavior_shortcuts[output] = *value;
        output++;
    }
    app->settings.behavior_shortcut_count = output;
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

static BongoCatResult scan_owned_storage(BongoCatApp *app, bool cleanup,
    BongoCatError *error) {
    BongoCatResult result = bongo_cat_model_install_builtins(app->asset_root,
        app->models_root, error);
    if (result != BONGO_CAT_OK) return result;
    if (cleanup && !bongo_cat_model_cleanup_imports(app->models_root, error))
        return error && error->code ? error->code : BONGO_CAT_ERROR_IO;
    return bongo_cat_models_scan(&app->models, app->models_root, false,
        error);
}

static BongoCatResult scan_owned_models(BongoCatApp *app, bool cleanup) {
    BongoCatError error = {0};
    bongo_cat_models_init(&app->models);
    bongo_cat_import_storage_lock();
    BongoCatResult result = scan_owned_storage(app, cleanup, &error);
    bongo_cat_import_storage_unlock();
    if (result != BONGO_CAT_OK) {
        if (error.message[0]) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "%s", error.message);
        return result;
    }
    error = (BongoCatError){0};
    result = bongo_cat_import_installed_models(app, app->models_root, &error);
    if (result != BONGO_CAT_OK && error.message[0])
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", error.message);
    return result;
}

void bongo_cat_model_catalog_finish(BongoCatApp *app) {
    if (!app) return;
    prune_behavior_shortcuts(app);
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
    if (result != BONGO_CAT_OK) return result;
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
    return BONGO_CAT_OK;
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
    BongoCatResult result = scan_owned_models(app, false);
    if (result == BONGO_CAT_OK)
        result = scan_nearby_root(app, app->nearby_root);
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
